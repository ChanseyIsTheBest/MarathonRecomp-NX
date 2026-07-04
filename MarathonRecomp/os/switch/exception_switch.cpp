#if defined(__SWITCH__)

#include <stdafx.h>
#include <atomic>
#include <cstring>

#include <switch.h>

#include <kernel/memory.h>

// PC keeps the guest window committed-zero, so an uncommitted guest read yields
// 0 and an uncommitted write lands on a throwaway page. The Switch commits
// sparsely and cannot commit from the libnx exception context, so reproduce
// that behaviour by decoding the faulting access and stepping past it: a LOAD
// zeroes its destination register(s), a STORE is skipped. This covers stray
// pointers and the Xbox 360 physical-mirror windows without syscalls. Live
// stack/heap is always committed on its allocation path, so a fault is
// necessarily to an address PC would have absorbed. Undecoded accesses (SIMD,
// atomics) stay fatal, with insn/fault/offset in x24-x26 for the crash report.

namespace
{
    void ZeroReg(ThreadExceptionDump* ctx, uint32_t reg)
    {
        if (reg < 29)
            ctx->cpu_gprs[reg].x = 0;
        else if (reg == 29)
            ctx->fp.x = 0;
        else if (reg == 30)
            ctx->lr.x = 0;
        // reg == 31 is the zero register: nothing to write.
    }

    void ZeroFpu(ThreadExceptionDump* ctx, uint32_t reg)
    {
        if (reg < 32)
            std::memset(&ctx->fpu_gprs[reg], 0, sizeof(ctx->fpu_gprs[reg]));
    }

    // GPR read where reg 31 is the zero register (e.g. Rm of a post-index form).
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

    // Base-register accessors where reg 31 means SP (address bases/writeback).
    uint64_t ReadRegOrSp(const ThreadExceptionDump* ctx, uint32_t reg)
    {
        return reg == 31 ? ctx->sp.x : ReadReg(ctx, reg);
    }

    void WriteRegOrSp(ThreadExceptionDump* ctx, uint32_t reg, uint64_t value)
    {
        if (reg < 29)
            ctx->cpu_gprs[reg].x = value;
        else if (reg == 29)
            ctx->fp.x = value;
        else if (reg == 30)
            ctx->lr.x = value;
        else
            ctx->sp.x = value;
    }

    // Emulate a faulting load/store as PC would resolve it: a load reads zero
    // (destination register[s] cleared), a store is skipped. Returns false for
    // encodings we do not handle so they remain fatal (and visible).
    bool EmulateAccess(ThreadExceptionDump* ctx, uint32_t insn)
    {
        // Load/store register, integer (bit 26 = 0): unsigned-immediate,
        // register-offset, or unscaled-immediate.
        const bool uimm = (insn & 0x3F000000u) == 0x39000000u;
        const bool rreg = (insn & 0x3F200C00u) == 0x38200800u;
        const bool unsc = (insn & 0x3F200C00u) == 0x38000000u;
        if (uimm || rreg || unsc)
        {
            if (((insn >> 22) & 0x3u) != 0u)      // opc != 00 -> load
                ZeroReg(ctx, insn & 0x1Fu);
            ctx->pc.x += 4;                       // store: step over the write
            return true;
        }

        // Load/store register, SIMD/FP (bit 26 = 1): same three addressing forms.
        const bool suimm = (insn & 0x3F000000u) == 0x3D000000u;
        const bool srreg = (insn & 0x3F200C00u) == 0x3C200800u;
        const bool sunsc = (insn & 0x3F200C00u) == 0x3C000000u;
        if (suimm || srreg || sunsc)
        {
            if (((insn >> 22) & 0x1u) != 0u)      // opc<0> == 1 -> load
                ZeroFpu(ctx, insn & 0x1Fu);
            ctx->pc.x += 4;
            return true;
        }

        // Load/store pair, integer (bits[29:25] = 10100), any index mode.
        if ((insn & 0x3E000000u) == 0x28000000u)
        {
            if (((insn >> 22) & 0x1u) != 0u)      // L == 1 -> load pair
            {
                ZeroReg(ctx, insn & 0x1Fu);
                ZeroReg(ctx, (insn >> 10) & 0x1Fu);
            }
            ctx->pc.x += 4;
            return true;
        }

        // Load/store pair, SIMD/FP (bits[29:25] = 10110), any index mode.
        if ((insn & 0x3E000000u) == 0x2C000000u)
        {
            if (((insn >> 22) & 0x1u) != 0u)      // L == 1 -> load pair
            {
                ZeroFpu(ctx, insn & 0x1Fu);
                ZeroFpu(ctx, (insn >> 10) & 0x1Fu);
            }
            ctx->pc.x += 4;
            return true;
        }

        // ASIMD load/store multiple structures (LD1..LD4/ST1..ST4 {vN..vM}),
        // no-writeback and post-index forms — vectorized copies in recompiled
        // code use these (e.g. ld1 {v1.16b, v2.16b}, [x1]). Loads zero the
        // destination vector run; stores are skipped; post-index writes back Rn.
        const bool asimdMulti   = (insn & 0xBFBF0000u) == 0x0C000000u;
        const bool asimdMultiWb = (insn & 0xBFA00000u) == 0x0C800000u;
        if (asimdMulti || asimdMultiWb)
        {
            // opcode[15:12] -> register count (0 = encoding we do not handle)
            static constexpr uint8_t kRegCount[16] = {
                4, 0, 4, 0, 3, 0, 3, 1,   // LD4/ST4, LD1x4, LD3/ST3, LD1x3, LD1x1
                2, 0, 2, 0, 0, 0, 0, 0,   // LD2/ST2, LD1x2
            };
            const uint32_t count = kRegCount[(insn >> 12) & 0xFu];
            if (count != 0)
            {
                if (((insn >> 22) & 0x1u) != 0u)  // L == 1 -> load
                {
                    const uint32_t rt = insn & 0x1Fu;
                    for (uint32_t i = 0; i < count; i++)
                        ZeroFpu(ctx, (rt + i) & 0x1Fu);
                }

                if (asimdMultiWb)                 // post-index: Rn += Rm or imm
                {
                    const uint32_t rn = (insn >> 5) & 0x1Fu;
                    const uint32_t rm = (insn >> 16) & 0x1Fu;
                    uint64_t increment;
                    if (rm != 31)
                        increment = ReadReg(ctx, rm);
                    else
                        increment = uint64_t(count) * ((((insn >> 30) & 1u) != 0u) ? 16 : 8);
                    WriteRegOrSp(ctx, rn, ReadRegOrSp(ctx, rn) + increment);
                }

                ctx->pc.x += 4;
                return true;
            }
        }

        return false;
    }
}

// libnx has already consumed the kernel exception by the time this handler runs
// (its entry svcReturnFromException's into the returnentry trampoline), so we
// cannot ask the kernel to resume — svcReturnFromException here is a Bad SVC. The
// thread is running normally on the exception stack, so resume the guest by hand:
// reload every register / SP / PC / flags / NEON from the fixed-up dump and branch
// back. Offsets are ThreadExceptionDump fields: gprs @16+8n, fp@248, lr@256,
// sp@264, pc@272, NEON@288+16n, pstate@800. x16 is used as the branch scratch (it
// is IP0, dead across the recompiler's load/compare sequences).
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
    const bool inWindow = base != 0 && fault >= base && fault < base + PPC_MEMORY_SIZE;
    uint32_t insn = 0;

    if (inWindow)
    {
        // Whole guest window including page 0: a zero-filled load can yield a
        // guest NULL that later code dereferences (base + 0), and PC treats that
        // low page as ordinary committed-zero memory, so emulate it here too.
        insn = *reinterpret_cast<const uint32_t*>(pc);

        // Backstop against a runaway fixup loop (a guest spin that keeps
        // reading the same uncommitted address expecting it to change).
        if (EmulateAccess(ctx, insn) &&
            s_emulated.fetch_add(1, std::memory_order_relaxed) < 200000000ull)
        {
            // Resume the guest from the fixed-up context (pc stepped, dest
            // register zeroed). Does not return.
            ResumeGuestContext(ctx);
        }
    }

    // Fatal: out-of-window, an encoding we do not emulate, or the loop backstop.
    // Break with the fault values in registers so an Atmosphère crash report
    // captures them (far near 0 = NULL deref; otherwise a wild guest address).
    const uint64_t guestOff = fault - base;
    FatalFaultBreak(fault, pc, insn, guestOff);
}

#endif
