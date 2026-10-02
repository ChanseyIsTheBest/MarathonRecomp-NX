#if defined(__SWITCH__)

#include <stdafx.h>
#include <atomic>
#include <cstring>

#include <switch.h>

#include <kernel/memory.h>
#include <os/switch_crash.h>

// PC keeps the guest window committed-zero, so an uncommitted guest read yields
// 0 and an uncommitted write lands on a throwaway page. The Switch commits
// sparsely and cannot commit from the libnx exception context, so reproduce
// that behaviour by decoding the faulting access and stepping past it: a LOAD
// reads 0 from every uncommitted byte, a STORE to an uncommitted byte is
// skipped. This covers stray pointers and the Xbox 360 physical-mirror windows
// without syscalls. Live stack/heap is always committed on its allocation path,
// so a fault is necessarily to an address PC would have absorbed. Undecoded
// accesses (atomics, exclusives) stay fatal, with insn/fault/offset in x24-x26
// for the crash report.
//
// The access is emulated byte by byte against the committed-page table: an
// access the compiler merged from two guest accesses (a pair, a wider load or
// store) that straddles a committed and an uncommitted page reads and writes
// its committed part for real, as the separate accesses would have. Every
// AArch64 load/store the compiler emits for guest memory is decoded: register,
// immediate (scaled, unscaled, pre/post-index), pair (all index modes, with
// write-back), SIMD&FP scalar, ASIMD multiple and single structures (LD1-4,
// ST1-4, single lanes, LD1R-LD4R) and DC ZVA (tools/switch-check-fault-emulation.py
// lists anything else it finds in the recompiled code).

namespace
{
    constexpr uint64_t kGuestPageSize = 0x1000;

    struct Access
    {
        uint64_t address = 0;   // host address of the first byte
        uint32_t size = 0;      // bytes
        uint32_t reg = 0;       // Rt (31 = XZR for integer registers)
        uint32_t laneOffset = 0;// byte offset in the vector register
        bool vector = false;    // SIMD&FP register
        bool keepRest = false;  // vector: the other lanes stay (single-lane load)
        uint32_t replicate = 0; // vector: LD1R-LD4R, bytes the element fills (8 or 16), the rest cleared
        uint8_t signExtendTo = 0;// integer: 0 = zero-extend, 32 or 64 = sign-extend to that width
        bool wide = false;      // integer: the destination is an X register (else W, upper half zeroed)
    };

    struct Decoded
    {
        bool load = false;
        bool zeroBlock = false; // DC ZVA
        uint32_t count = 0;
        Access accesses[64];    // LD4 {v.16b} x4: 64 byte elements
        uint32_t clearCount = 0;// vector registers a multiple-structure load writes whole (cleared first)
        uint32_t clear[4] = {};
        bool writeback = false;
        uint32_t rn = 0;
        uint64_t newRn = 0;
    };

    // GPR read where reg 31 is the zero register (Rt of a store, Rm).
    uint64_t ReadReg(const ThreadExceptionDump* ctx, uint32_t reg)
    {
        if (reg < 29)
            return ctx->cpu_gprs[reg].x;
        if (reg == 29)
            return ctx->fp.x;
        if (reg == 30)
            return ctx->lr.x;
        return 0;
    }

    void WriteReg(ThreadExceptionDump* ctx, uint32_t reg, uint64_t value)
    {
        if (reg < 29)
            ctx->cpu_gprs[reg].x = value;
        else if (reg == 29)
            ctx->fp.x = value;
        else if (reg == 30)
            ctx->lr.x = value;
        // reg == 31 is the zero register: nothing to write.
    }

    // Base-register accessors where reg 31 means SP (address bases/writeback).
    uint64_t ReadRegOrSp(const ThreadExceptionDump* ctx, uint32_t reg)
    {
        return reg == 31 ? ctx->sp.x : ReadReg(ctx, reg);
    }

    void WriteRegOrSp(ThreadExceptionDump* ctx, uint32_t reg, uint64_t value)
    {
        if (reg == 31)
            ctx->sp.x = value;
        else
            WriteReg(ctx, reg, value);
    }

    uint8_t* VectorBytes(ThreadExceptionDump* ctx, uint32_t reg)
    {
        return reinterpret_cast<uint8_t*>(&ctx->fpu_gprs[reg & 31]);
    }

    int64_t SignExtend(uint64_t value, uint32_t bits)
    {
        const uint64_t sign = uint64_t(1) << (bits - 1);
        return int64_t((value & ((sign << 1) - 1)) ^ sign) - int64_t(sign);
    }

    // A guest byte that may be read or written for real: its page is committed. The table is sized once
    // at start-up and a page's flag is set after its mapping and never cleared, so it can be read here
    // without the commit mutex (which the faulting thread may hold). Bytes outside the window are not
    // guest memory and are treated as uncommitted.
    bool IsCommitted(uint64_t host)
    {
        const uint64_t base = reinterpret_cast<uint64_t>(g_memory.base);
        if (base == 0 || host < base || host - base >= PPC_MEMORY_SIZE)
            return false;

        const uint64_t page = (host - base) / kGuestPageSize;
        if (page >= g_memory.committedPages.size())
            return false;

        return __atomic_load_n(g_memory.committedPages.data() + page, __ATOMIC_ACQUIRE) != 0;
    }

    // SWITCH_WIDE_DFORM (ppc_context.h): a register + displacement access computed in 64 bits that went past the 4 GB
    // window lands in the guard reserved after it (Memory::SWITCH_WRAP_GUARD_SIZE); the 32-bit sum the guest computes
    // wraps to the start of the window, so its bytes are the ones at the wrapped address.
    uint64_t WrapGuestByte(uint64_t host)
    {
        const uint64_t base = reinterpret_cast<uint64_t>(g_memory.base);
        if (base != 0 && host >= base + PPC_MEMORY_SIZE && host - base - PPC_MEMORY_SIZE < Memory::SWITCH_WRAP_GUARD_SIZE)
            return host - PPC_MEMORY_SIZE;
        return host;
    }

    void ReadBytes(uint64_t address, uint8_t* out, uint32_t size)
    {
        for (uint32_t i = 0; i < size; i++)
        {
            const uint64_t host = WrapGuestByte(address + i);
            out[i] = IsCommitted(host) ? *reinterpret_cast<const volatile uint8_t*>(host) : 0;
        }
    }

    void WriteBytes(uint64_t address, const uint8_t* in, uint32_t size)
    {
        for (uint32_t i = 0; i < size; i++)
        {
            const uint64_t host = WrapGuestByte(address + i);
            if (IsCommitted(host))
                *reinterpret_cast<volatile uint8_t*>(host) = in[i];
        }
    }

    bool Add(Decoded& d, const Access& access)
    {
        if (d.count >= sizeof(d.accesses) / sizeof(d.accesses[0]))
            return false;
        d.accesses[d.count++] = access;
        return true;
    }

    // Load/store register (integer or SIMD&FP): unsigned immediate, register offset, unscaled immediate,
    // unprivileged, pre-index and post-index.
    bool DecodeSingleRegister(const ThreadExceptionDump* ctx, uint32_t insn, Decoded& d)
    {
        if ((insn & 0x3A000000u) != 0x38000000u)   // bits 29:27 = 111, bit 25 = 0 (bit 26 = V)
            return false;

        const uint32_t size = insn >> 30;
        const uint32_t opc = (insn >> 22) & 3u;
        const bool vector = ((insn >> 26) & 1u) != 0;
        const uint32_t rn = (insn >> 5) & 31u;
        const uint32_t rt = insn & 31u;

        Access access;
        access.reg = rt;
        access.vector = vector;

        uint32_t scale;
        if (vector)
        {
            if (size == 0 && (opc & 2u) != 0)       // 128-bit STR/LDR Q
                scale = 4;
            else if ((opc & 2u) == 0)
                scale = size;
            else
                return false;                     // unallocated
            d.load = (opc & 1u) != 0;
        }
        else
        {
            scale = size;
            if (opc == 0)
            {
                d.load = false;
            }
            else if (opc == 1)
            {
                d.load = true;
                access.wide = size == 3;
            }
            else if (opc == 2)
            {
                if (size == 3)
                    return false;                 // PRFM: never faults
                d.load = true;
                access.signExtendTo = 64;
                access.wide = true;
            }
            else
            {
                if (size >= 2)
                    return false;                 // unallocated
                d.load = true;
                access.signExtendTo = 32;
            }
        }
        access.size = 1u << scale;

        const uint64_t base = ReadRegOrSp(ctx, rn);
        if ((insn & 0x01000000u) != 0)             // unsigned immediate
        {
            access.address = base + (uint64_t((insn >> 10) & 0xFFFu) << scale);
        }
        else if ((insn & 0x00200000u) != 0)         // register offset
        {
            if (((insn >> 10) & 3u) != 2u)
                return false;                     // atomic memory operations (LDADD...) and others
            const uint32_t option = (insn >> 13) & 7u;
            const uint64_t rm = ReadReg(ctx, (insn >> 16) & 31u);
            uint64_t offset;
            switch (option)
            {
            case 2: offset = uint32_t(rm); break;                        // UXTW
            case 3: offset = rm; break;                                  // LSL/UXTX
            case 6: offset = uint64_t(int64_t(int32_t(uint32_t(rm)))); break; // SXTW
            case 7: offset = rm; break;                                  // SXTX
            default: return false;
            }
            if ((insn & 0x1000u) != 0)
                offset <<= scale;
            access.address = base + offset;
        }
        else
        {
            const uint64_t simm9 = uint64_t(SignExtend((insn >> 12) & 0x1FFu, 9));
            switch ((insn >> 10) & 3u)
            {
            case 0: // unscaled
            case 2: // unprivileged (EL0: as unscaled)
                access.address = base + simm9;
                break;
            case 1: // post-index
                access.address = base;
                d.writeback = true;
                d.rn = rn;
                d.newRn = base + simm9;
                break;
            case 3: // pre-index
                access.address = base + simm9;
                d.writeback = true;
                d.rn = rn;
                d.newRn = base + simm9;
                break;
            }
        }

        return Add(d, access);
    }

    // Load/store pair (integer or SIMD&FP), all index modes.
    bool DecodePair(const ThreadExceptionDump* ctx, uint32_t insn, Decoded& d)
    {
        if ((insn & 0x3A000000u) != 0x28000000u)   // bits 29:27 = 101, bit 25 = 0 (bit 26 = V)
            return false;

        const uint32_t opc = insn >> 30;
        const bool vector = ((insn >> 26) & 1u) != 0;
        const uint32_t mode = (insn >> 23) & 3u;   // 0 no-allocate, 1 post, 2 offset, 3 pre
        d.load = ((insn >> 22) & 1u) != 0;

        uint32_t scale;
        uint8_t signExtendTo = 0;
        bool wide = false;
        if (vector)
        {
            if (opc == 3)
                return false;
            scale = 2 + opc;
        }
        else if (opc == 0)
        {
            scale = 2;
        }
        else if (opc == 1)
        {
            if (!d.load)
                return false;                     // STGP (memory tagging)
            scale = 2;                            // LDPSW
            signExtendTo = 64;
            wide = true;
        }
        else if (opc == 2)
        {
            scale = 3;
            wide = true;
        }
        else
        {
            return false;
        }

        const uint32_t rn = (insn >> 5) & 31u;
        const uint64_t base = ReadRegOrSp(ctx, rn);
        const uint64_t offset = uint64_t(SignExtend((insn >> 15) & 0x7Fu, 7)) << scale;
        const uint64_t address = mode == 1 ? base : base + offset;
        if (mode == 1 || mode == 3)
        {
            d.writeback = true;
            d.rn = rn;
            d.newRn = base + offset;
        }

        for (uint32_t i = 0; i < 2; i++)
        {
            Access access;
            access.address = address + (uint64_t(i) << scale);
            access.size = 1u << scale;
            access.reg = i == 0 ? (insn & 31u) : ((insn >> 10) & 31u);
            access.vector = vector;
            access.signExtendTo = signExtendTo;
            access.wide = wide;
            if (!Add(d, access))
                return false;
        }
        return true;
    }

    // ASIMD load/store multiple structures (LD1-LD4/ST1-ST4), no write-back and post-index.
    bool DecodeMultipleStructures(const ThreadExceptionDump* ctx, uint32_t insn, Decoded& d)
    {
        const bool noWriteback = (insn & 0xBFBF0000u) == 0x0C000000u;
        const bool postIndex = (insn & 0xBFA00000u) == 0x0C800000u;
        if (!noWriteback && !postIndex)
            return false;

        uint32_t rpt, selem;
        switch ((insn >> 12) & 0xFu)
        {
        case 0x0: rpt = 1; selem = 4; break;   // LD4/ST4
        case 0x2: rpt = 4; selem = 1; break;   // LD1/ST1, 4 registers
        case 0x4: rpt = 1; selem = 3; break;   // LD3/ST3
        case 0x6: rpt = 3; selem = 1; break;   // LD1/ST1, 3 registers
        case 0x7: rpt = 1; selem = 1; break;   // LD1/ST1, 1 register
        case 0x8: rpt = 1; selem = 2; break;   // LD2/ST2
        case 0xA: rpt = 2; selem = 1; break;   // LD1/ST1, 2 registers
        default: return false;
        }

        const uint32_t size = (insn >> 10) & 3u;
        const bool q = ((insn >> 30) & 1u) != 0;
        if (size == 3 && !q && selem != 1)
            return false;                         // reserved

        d.load = ((insn >> 22) & 1u) != 0;
        const uint32_t rn = (insn >> 5) & 31u;
        const uint32_t rt = insn & 31u;
        const uint64_t address = ReadRegOrSp(ctx, rn);
        const uint32_t ebytes = 1u << size;
        const uint32_t elements = (q ? 16u : 8u) / ebytes;

        // Arm ARM order: for each repetition, element, structure member.
        uint64_t offs = 0;
        for (uint32_t r = 0; r < rpt; r++)
        {
            for (uint32_t e = 0; e < elements; e++)
            {
                uint32_t tt = (rt + r) & 31u;
                for (uint32_t s = 0; s < selem; s++)
                {
                    Access access;
                    access.address = address + offs;
                    access.size = ebytes;
                    access.reg = tt;
                    access.laneOffset = e * ebytes;
                    access.vector = true;
                    access.keepRest = true;       // the register is cleared first (see clear)
                    if (!Add(d, access))
                        return false;
                    offs += ebytes;
                    tt = (tt + 1) & 31u;
                }
            }
        }

        if (postIndex)
        {
            const uint32_t rm = (insn >> 16) & 31u;
            d.writeback = true;
            d.rn = rn;
            d.newRn = address + (rm != 31 ? ReadReg(ctx, rm) : offs);
        }

        // A load writes every lane of its registers and clears the upper half of a 64-bit (Q=0) one.
        if (d.load)
        {
            d.clearCount = rpt * selem;
            for (uint32_t i = 0; i < d.clearCount; i++)
                d.clear[i] = (rt + i) & 31u;
        }
        return true;
    }

    // ASIMD load/store single structure (one lane, LD1-LD4/ST1-ST4) and load-and-replicate (LD1R-LD4R),
    // no write-back and post-index.
    bool DecodeSingleStructure(const ThreadExceptionDump* ctx, uint32_t insn, Decoded& d)
    {
        const bool noWriteback = (insn & 0xBF9F0000u) == 0x0D000000u;
        const bool postIndex = (insn & 0xBF800000u) == 0x0D800000u;
        if (!noWriteback && !postIndex)
            return false;

        const bool q = ((insn >> 30) & 1u) != 0;
        const bool l = ((insn >> 22) & 1u) != 0;
        const uint32_t r = (insn >> 21) & 1u;
        const uint32_t opcode = (insn >> 13) & 7u;
        const uint32_t s = (insn >> 12) & 1u;
        const uint32_t size = (insn >> 10) & 3u;
        const uint32_t selem = (((opcode & 1u) << 1) | r) + 1;

        uint32_t scale = opcode >> 1;
        uint32_t index = 0;
        bool replicate = false;
        switch (scale)
        {
        case 3:
            if (!l || s != 0)
                return false;
            scale = size;
            replicate = true;
            break;
        case 0:
            index = (uint32_t(q) << 3) | (s << 2) | size;
            break;
        case 1:
            if ((size & 1u) != 0)
                return false;
            index = (uint32_t(q) << 2) | (s << 1) | (size >> 1);
            break;
        case 2:
            if ((size & 2u) != 0)
                return false;
            if ((size & 1u) == 0)
            {
                index = (uint32_t(q) << 1) | s;
            }
            else
            {
                if (s != 0)
                    return false;
                index = q;
                scale = 3;
            }
            break;
        }

        d.load = l;
        const uint32_t rn = (insn >> 5) & 31u;
        const uint32_t rt = insn & 31u;
        const uint64_t address = ReadRegOrSp(ctx, rn);
        const uint32_t ebytes = 1u << scale;

        uint64_t offs = 0;
        for (uint32_t i = 0; i < selem; i++)
        {
            Access access;
            access.address = address + offs;
            access.size = ebytes;
            access.reg = (rt + i) & 31u;
            access.vector = true;
            if (replicate)
            {
                access.replicate = q ? 16 : 8;    // the rest of the register is cleared
            }
            else
            {
                access.laneOffset = index * ebytes;
                access.keepRest = true;           // the other lanes (and the upper half) stay
            }
            if (!Add(d, access))
                return false;
            offs += ebytes;
        }

        if (postIndex)
        {
            const uint32_t rm = (insn >> 16) & 31u;
            d.writeback = true;
            d.rn = rn;
            d.newRn = address + (rm != 31 ? ReadReg(ctx, rm) : offs);
        }
        return true;
    }

    // DC ZVA: zeroes one naturally aligned block (a store of zeros).
    bool DecodeZeroBlock(const ThreadExceptionDump* ctx, uint32_t insn, Decoded& d)
    {
        if ((insn & 0xFFFFFFE0u) != 0xD50B7420u)
            return false;

        uint64_t dczid;
        __asm__ __volatile__("mrs %0, dczid_el0" : "=r"(dczid));
        if ((dczid & 0x10u) != 0)
            return false;                         // DC ZVA prohibited: it cannot have faulted as a store

        const uint32_t blockSize = 4u << (dczid & 0xFu);
        Access access;
        access.address = ReadReg(ctx, insn & 31u) & ~uint64_t(blockSize - 1);
        access.size = blockSize;
        access.reg = 31;
        d.load = false;
        d.zeroBlock = true;
        return Add(d, access);
    }

    // Emulate a faulting load/store as PC would resolve it: every uncommitted byte reads as zero and is not
    // written, every committed byte is accessed for real. Returns false for encodings we do not handle and for
    // an access the fault address is not part of, so they remain fatal (and visible).
    bool EmulateAccess(ThreadExceptionDump* ctx, uint32_t insn, uint64_t fault)
    {
        // The classes are disjoint: at most one decoder accepts an encoding.
        static constexpr bool (*kDecoders[])(const ThreadExceptionDump*, uint32_t, Decoded&) = {
            DecodeSingleRegister, DecodePair, DecodeMultipleStructures, DecodeSingleStructure, DecodeZeroBlock,
        };
        Decoded d;
        bool decoded = false;
        for (auto* decoder : kDecoders)
        {
            d = Decoded();
            if (decoder(ctx, insn, d))
            {
                decoded = true;
                break;
            }
        }
        if (!decoded)
            return false;

        // A decoding mistake must not step over an access it did not model: the fault address has to lie
        // in one of the bytes the instruction accesses.
        bool faultInside = false;
        for (uint32_t i = 0; i < d.count; i++)
        {
            const Access& a = d.accesses[i];
            if (a.size != 0 && fault >= a.address && fault - a.address < a.size)
                faultInside = true;
        }
        if (!faultInside)
            return false;

        // ResumeGuestContext branches through x16, which then holds the resume address: an access that
        // must leave a value in x16 (a load into it, a write-back of it) cannot be resumed correctly.
        // (The recompiled code is built with -ffixed-x16 and never names it.)
        if (d.writeback && d.rn == 16)
            return false;
        if (d.load)
        {
            for (uint32_t i = 0; i < d.count; i++)
            {
                if (!d.accesses[i].vector && d.accesses[i].reg == 16)
                    return false;
            }
        }

        if (d.load)
        {
            for (uint32_t i = 0; i < d.clearCount; i++)
                std::memset(VectorBytes(ctx, d.clear[i]), 0, 16);

            for (uint32_t i = 0; i < d.count; i++)
            {
                const Access& a = d.accesses[i];
                if (a.vector)
                {
                    uint8_t* v = VectorBytes(ctx, a.reg);
                    uint8_t element[16];
                    ReadBytes(a.address, element, a.size);
                    if (a.replicate != 0)
                    {
                        std::memset(v, 0, 16);
                        for (uint32_t offset = 0; offset + a.size <= a.replicate; offset += a.size)
                            std::memcpy(v + offset, element, a.size);
                    }
                    else if (a.keepRest)
                    {
                        std::memcpy(v + a.laneOffset, element, a.size);
                    }
                    else
                    {
                        std::memset(v, 0, 16);
                        std::memcpy(v, element, a.size);
                    }
                }
                else
                {
                    uint8_t bytes[8] = {};
                    ReadBytes(a.address, bytes, a.size);
                    uint64_t value = 0;
                    std::memcpy(&value, bytes, a.size);
                    if (a.signExtendTo != 0)
                        value = uint64_t(SignExtend(value, a.size * 8));
                    if (!a.wide)
                        value = uint32_t(value);
                    WriteReg(ctx, a.reg, value);
                }
            }
        }
        else
        {
            for (uint32_t i = 0; i < d.count; i++)
            {
                const Access& a = d.accesses[i];
                if (d.zeroBlock)
                {
                    static const uint8_t zeros[2048] = {};
                    if (a.size > sizeof(zeros))
                        return false;
                    WriteBytes(a.address, zeros, a.size);
                }
                else if (a.vector)
                {
                    WriteBytes(a.address, VectorBytes(ctx, a.reg) + a.laneOffset, a.size);
                }
                else
                {
                    const uint64_t value = ReadReg(ctx, a.reg);
                    uint8_t bytes[8];
                    std::memcpy(bytes, &value, sizeof(bytes));
                    WriteBytes(a.address, bytes, a.size);
                }
            }
        }

        if (d.writeback)
            WriteRegOrSp(ctx, d.rn, d.newRn);

        ctx->pc.x += 4;
        return true;
    }
}

// libnx has already consumed the kernel exception by the time this handler runs
// (its entry svcReturnFromException's into the returnentry trampoline), so we
// cannot ask the kernel to resume — svcReturnFromException here is a Bad SVC. The
// thread is running normally on the exception stack, so resume the guest by hand:
// reload every register / SP / PC / flags / NEON from the fixed-up dump and branch
// back. Offsets are ThreadExceptionDump fields: gprs @16+8n, fp@248, lr@256,
// sp@264, pc@272, NEON@288+16n, pstate@800. The branch needs one general register
// (AArch64 has no branch to memory, and user mode no eret): x16 is left holding
// the resume address. GCC allocates x16-x18 like any other register, so the
// recompiled code (and the app) are compiled with -ffixed-x16
// (MARATHON_RECOMP_SWITCH_FIXED_X16), which keeps every value out of it there;
// EmulateAccess refuses the accesses that would have to leave a value in x16.
extern "C" [[noreturn]] void ResumeGuestContext(ThreadExceptionDump* ctx);

__asm__(
    ".text\n"
    ".global ResumeGuestContext\n"
    ".type ResumeGuestContext, %function\n"
    "ResumeGuestContext:\n"
    "    ldr  w1, [x0, #800]\n"
    "    msr  nzcv, x1\n"
    "    add  x1, x0, #288\n"
    "    ldp  q0,  q1,  [x1, #0]\n"
    "    ldp  q2,  q3,  [x1, #32]\n"
    "    ldp  q4,  q5,  [x1, #64]\n"
    "    ldp  q6,  q7,  [x1, #96]\n"
    "    ldp  q8,  q9,  [x1, #128]\n"
    "    ldp  q10, q11, [x1, #160]\n"
    "    ldp  q12, q13, [x1, #192]\n"
    "    ldp  q14, q15, [x1, #224]\n"
    "    ldp  q16, q17, [x1, #256]\n"
    "    ldp  q18, q19, [x1, #288]\n"
    "    ldp  q20, q21, [x1, #320]\n"
    "    ldp  q22, q23, [x1, #352]\n"
    "    ldp  q24, q25, [x1, #384]\n"
    "    ldp  q26, q27, [x1, #416]\n"
    "    ldp  q28, q29, [x1, #448]\n"
    "    ldp  q30, q31, [x1, #480]\n"
    "    ldr  x1,  [x0, #264]\n"
    "    mov  sp,  x1\n"
    "    ldr  x16, [x0, #272]\n"
    "    ldr  x29, [x0, #248]\n"
    "    ldr  x30, [x0, #256]\n"
    "    ldr  x1,  [x0, #24]\n"
    "    ldr  x2,  [x0, #32]\n"
    "    ldr  x3,  [x0, #40]\n"
    "    ldr  x4,  [x0, #48]\n"
    "    ldr  x5,  [x0, #56]\n"
    "    ldr  x6,  [x0, #64]\n"
    "    ldr  x7,  [x0, #72]\n"
    "    ldr  x8,  [x0, #80]\n"
    "    ldr  x9,  [x0, #88]\n"
    "    ldr  x10, [x0, #96]\n"
    "    ldr  x11, [x0, #104]\n"
    "    ldr  x12, [x0, #112]\n"
    "    ldr  x13, [x0, #120]\n"
    "    ldr  x14, [x0, #128]\n"
    "    ldr  x15, [x0, #136]\n"
    "    ldr  x17, [x0, #152]\n"
    "    ldr  x18, [x0, #160]\n"
    "    ldr  x19, [x0, #168]\n"
    "    ldr  x20, [x0, #176]\n"
    "    ldr  x21, [x0, #184]\n"
    "    ldr  x22, [x0, #192]\n"
    "    ldr  x23, [x0, #200]\n"
    "    ldr  x24, [x0, #208]\n"
    "    ldr  x25, [x0, #216]\n"
    "    ldr  x26, [x0, #224]\n"
    "    ldr  x27, [x0, #232]\n"
    "    ldr  x28, [x0, #240]\n"
    "    ldr  x0,  [x0, #16]\n"
    "    br   x16\n"
);

// Terminate into the Atmosphère crash report. The kernel always reports
// svcBreak's arguments, so route the two most useful values through them:
// Break Address = far, Break Size = host PC (addr2line -> faulting function).
// insn/guestOff go in x24/x25 as best effort.
extern "C" [[noreturn]] void FatalFaultBreak(uint64_t far_, uint64_t hostPc, uint64_t insn, uint64_t guestOff);

__asm__(
    ".text\n"
    ".global FatalFaultBreak\n"
    ".type FatalFaultBreak, %function\n"
    "FatalFaultBreak:\n"    // x0=far, x1=hostPc, x2=insn, x3=guestOff
    "    mov x24, x2\n"     // insn      (best effort; may be clobbered by the SVC)
    "    mov x25, x3\n"     // guestOff  (best effort)
    "    mov x2, x1\n"      // svcBreak size    = host PC
    "    mov x1, x0\n"      // svcBreak address = far
    "    mov x0, #0\n"      // BreakReason_Panic
    "    bl  svcBreak\n"
    "    brk #0\n"
);

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx)
{
    static std::atomic<uint64_t> s_emulated{ 0 };

    const uint64_t fault = ctx->far.x;
    const uint64_t pc = ctx->pc.x;
    const uint8_t* memBase = g_memory.base;
    const uint64_t base = memBase != nullptr ? reinterpret_cast<uint64_t>(memBase) : 0;
    // The guard after the window too (SWITCH_WIDE_DFORM, WrapGuestByte).
    const bool inWindow = base != 0 && fault >= base && fault < base + PPC_MEMORY_SIZE + Memory::SWITCH_WRAP_GUARD_SIZE;
    uint32_t insn = 0;

    if (inWindow)
    {
        // Whole guest window including page 0: a zero-filled load can yield a
        // guest NULL that later code dereferences (base + 0), and PC treats that
        // low page as ordinary committed-zero memory, so emulate it here too.
        insn = *reinterpret_cast<const uint32_t*>(pc);

        // Backstop against a runaway fixup loop (a guest spin that keeps
        // reading the same uncommitted address expecting it to change). It is
        // checked before EmulateAccess, which only changes the dump when it
        // succeeds, so the fatal path below reports the faulting state as is.
        if (s_emulated.load(std::memory_order_relaxed) < 200000000ull &&
            EmulateAccess(ctx, insn, fault))
        {
            s_emulated.fetch_add(1, std::memory_order_relaxed);

            // Resume the guest from the fixed-up context (pc stepped, loaded
            // registers set, base written back). Does not return.
            ResumeGuestContext(ctx);
        }
    }

    // Fatal: out-of-window, an encoding we do not emulate, or the loop backstop.
    // The crash report module (if linked) writes its report first; it returns.
    if (os::switch_crash::WriteCpuExceptionReport)
        os::switch_crash::WriteCpuExceptionReport(ctx);

    // Break with the fault values in registers so an Atmosphère crash report
    // captures them (far near 0 = NULL deref; otherwise a wild guest address).
    const uint64_t guestOff = fault - base;
    FatalFaultBreak(fault, pc, insn, guestOff);
}

#endif
