#if defined(__SWITCH__)

#include <stdafx.h>
#include <os/switch/perf/native_hooks.h>

#include <cstring>

// [Switch] SwitchNativeCrtStrings (native_hooks.h). Small MSVC CRT routines the game calls a lot, which the recompiled
// code runs a byte at a time through volatile guest loads and stores, as native code with the same results: the same
// bytes written, r3 as the recompiled code leaves it (all 64 bits), the other registers left alone (volatile; the
// same assumption as the memcpy/memset hooks in misc_impl.cpp). Guest addresses wrap around the 4 GB guest space the
// way the recompiled code's 32-bit address arithmetic does: ranges that would wrap run the recompiled code.
namespace
{
    bool Overlap(uint64_t a, uint64_t aSize, uint64_t b, uint64_t bSize)
    {
        return a < b + bSize && b < a + aSize;
    }

    bool InGuestSpace(uint64_t address, uint64_t size)
    {
        return address + size <= PPC_MEMORY_SIZE;
    }

    // The 8 bytes at the guest address, read at once only while they lie in one 4 KB page: the page of the first byte,
    // which the guest reads itself, so no page the guest would not touch is read (guest memory is committed in whole
    // pages; an uncommitted one would fault).
    bool LoadWord(const uint8_t* base, uint32_t address, uint64_t& word)
    {
        if ((address & 0xFFF) > 0xFF8)
            return false;

        memcpy(&word, base + address, sizeof(word));
        return true;
    }

    bool HasZeroByte(uint64_t word)
    {
        return ((word - 0x0101010101010101ull) & ~word & 0x8080808080808080ull) != 0;
    }

    // Unaligned accesses of guest memory as one load or store each.
    typedef uint64_t UnalignedU64 __attribute__((may_alias, aligned(1)));
    typedef uint32_t UnalignedU32 __attribute__((may_alias, aligned(1)));
    typedef uint16_t UnalignedU16 __attribute__((may_alias, aligned(1)));

    uint64_t Load64(const uint8_t* address)
    {
        return *reinterpret_cast<const UnalignedU64*>(address);
    }

    void Store64(uint8_t* address, uint64_t value)
    {
        *reinterpret_cast<UnalignedU64*>(address) = value;
    }

    // 1, 2, 4, 8, 16, 32 or 64 bytes, all loaded before any is stored.
    template<size_t Size>
    void CopyBlock(uint8_t* to, const uint8_t* from)
    {
        if constexpr (Size == 1)
        {
            *to = *from;
        }
        else if constexpr (Size == 2)
        {
            *reinterpret_cast<UnalignedU16*>(to) = *reinterpret_cast<const UnalignedU16*>(from);
        }
        else if constexpr (Size == 4)
        {
            *reinterpret_cast<UnalignedU32*>(to) = *reinterpret_cast<const UnalignedU32*>(from);
        }
        else if constexpr (Size == 8)
        {
            Store64(to, Load64(from));
        }
        else if constexpr (Size == 16)
        {
            const uint64_t a = Load64(from);
            const uint64_t b = Load64(from + 8);
            Store64(to, a);
            Store64(to + 8, b);
        }
        else if constexpr (Size == 32)
        {
            const uint64_t a = Load64(from);
            const uint64_t b = Load64(from + 8);
            const uint64_t c = Load64(from + 16);
            const uint64_t d = Load64(from + 24);
            Store64(to, a);
            Store64(to + 8, b);
            Store64(to + 16, c);
            Store64(to + 24, d);
        }
        else
        {
            static_assert(Size == 64);
            const uint64_t a = Load64(from);
            const uint64_t b = Load64(from + 8);
            const uint64_t c = Load64(from + 16);
            const uint64_t d = Load64(from + 24);
            const uint64_t e = Load64(from + 32);
            const uint64_t f = Load64(from + 40);
            const uint64_t g = Load64(from + 48);
            const uint64_t h = Load64(from + 56);
            Store64(to, a);
            Store64(to + 8, b);
            Store64(to + 16, c);
            Store64(to + 24, d);
            Store64(to + 32, e);
            Store64(to + 40, f);
            Store64(to + 48, g);
            Store64(to + 56, h);
        }
    }

    // A block forwards (the pointers then move past it) or backwards (they move below it first).
    template<size_t Size>
    void CopyForwards(uint8_t*& to, const uint8_t*& from)
    {
        CopyBlock<Size>(to, from);
        to += Size;
        from += Size;
    }

    template<size_t Size>
    void CopyBackwards(uint8_t*& to, const uint8_t*& from)
    {
        to -= Size;
        from -= Size;
        CopyBlock<Size>(to, from);
    }

    // strncpy of ranges that neither overlap nor wrap (see GuestMemmove for why not the C library's strnlen, memcpy
    // and memset): eight bytes at a time while they hold no terminator (read as LoadWord does), else one byte, until
    // the terminator or n bytes are copied; then zeros up to n.
    __attribute__((noinline, optimize("no-tree-loop-distribute-patterns")))
    void CopyString(uint8_t* base, uint32_t destination, uint32_t source, uint32_t count)
    {
        uint8_t* to = base + destination;
        uint32_t copied = 0;
        for (;;)
        {
            uint64_t word;
            while (count - copied >= 8 && LoadWord(base, source + copied, word) && !HasZeroByte(word))
            {
                Store64(to + copied, word);
                copied += 8;
            }

            if (copied == count)
                return;

            const uint8_t value = base[source + copied];
            to[copied++] = value;
            if (value == 0)
                break;
        }

        for (; count - copied >= 8; copied += 8)
            Store64(to + copied, 0);
        for (; copied != count; copied++)
            to[copied] = 0;
    }
}

// [Switch] memmove for the hooks that copy guest memory (misc_impl.cpp, native_*.cpp) in place of the C library's.
// A host access to an uncommitted guest page faults into os/switch/exception_switch.cpp, which emulates it as PC
// would (zeros read, the store dropped) and resumes through x16: only code built with -ffixed-x16, the app's, keeps
// x16 free. The prebuilt newlib is not; its memcpy (= memmove) holds source bytes in x16/x17 for 65-128-byte copies,
// so a fault resumed there stores the resume address into guest memory, or crashes on the load into x16. Same bytes
// as memmove for every input, overlap included. The optimize attribute keeps GCC from turning the loop back into a
// memcpy/memmove call, and noinline keeps it in this function, with that option.
__attribute__((noinline, optimize("no-tree-loop-distribute-patterns")))
void* GuestMemmove(void* destination, const void* source, size_t size)
{
    uint8_t* to = static_cast<uint8_t*>(destination);
    const uint8_t* from = static_cast<const uint8_t*>(source);

    // Up to 64 bytes: all loaded, from both ends, before anything is stored.
    if (size <= 16)
    {
        if (size >= 8)
        {
            const uint64_t a = Load64(from);
            const uint64_t b = Load64(from + size - 8);
            Store64(to, a);
            Store64(to + size - 8, b);
        }
        else if (size >= 4)
        {
            const uint32_t a = *reinterpret_cast<const UnalignedU32*>(from);
            const uint32_t b = *reinterpret_cast<const UnalignedU32*>(from + size - 4);
            *reinterpret_cast<UnalignedU32*>(to) = a;
            *reinterpret_cast<UnalignedU32*>(to + size - 4) = b;
        }
        else if (size != 0)
        {
            const uint8_t a = from[0];
            const uint8_t b = from[size / 2];
            const uint8_t c = from[size - 1];
            to[0] = a;
            to[size / 2] = b;
            to[size - 1] = c;
        }

        return destination;
    }

    if (size <= 32)
    {
        const uint64_t a = Load64(from);
        const uint64_t b = Load64(from + 8);
        const uint64_t c = Load64(from + size - 16);
        const uint64_t d = Load64(from + size - 8);
        Store64(to, a);
        Store64(to + 8, b);
        Store64(to + size - 16, c);
        Store64(to + size - 8, d);
        return destination;
    }

    if (size <= 64)
    {
        const uint64_t a = Load64(from);
        const uint64_t b = Load64(from + 8);
        const uint64_t c = Load64(from + 16);
        const uint64_t d = Load64(from + 24);
        const uint64_t e = Load64(from + size - 32);
        const uint64_t f = Load64(from + size - 24);
        const uint64_t g = Load64(from + size - 16);
        const uint64_t h = Load64(from + size - 8);
        Store64(to, a);
        Store64(to + 8, b);
        Store64(to + 16, c);
        Store64(to + 24, d);
        Store64(to + size - 32, e);
        Store64(to + size - 24, f);
        Store64(to + size - 16, g);
        Store64(to + size - 8, h);
        return destination;
    }

    // Longer: block by block, each loaded whole before it is stored. Forwards unless the destination starts inside
    // the source, so a store only lands on source bytes already loaded. The destination is first aligned to 16 bytes
    // (newlib does too: the Cortex-A57 is slower at stores across 16 bytes), then 64 bytes at a time, then the rest.
    const uintptr_t distance = reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(from);
    if (distance == 0)
        return destination;

    if (distance >= size)
    {
        const size_t head = -reinterpret_cast<uintptr_t>(to) & 15;
        size -= head;
        if (head & 1) CopyForwards<1>(to, from);
        if (head & 2) CopyForwards<2>(to, from);
        if (head & 4) CopyForwards<4>(to, from);
        if (head & 8) CopyForwards<8>(to, from);

        for (; size >= 64; size -= 64)
            CopyForwards<64>(to, from);

        if (size & 32) CopyForwards<32>(to, from);
        if (size & 16) CopyForwards<16>(to, from);
        if (size & 8) CopyForwards<8>(to, from);
        if (size & 4) CopyForwards<4>(to, from);
        if (size & 2) CopyForwards<2>(to, from);
        if (size & 1) CopyForwards<1>(to, from);
    }
    else
    {
        to += size;
        from += size;
        const size_t tail = reinterpret_cast<uintptr_t>(to) & 15;
        size -= tail;
        if (tail & 1) CopyBackwards<1>(to, from);
        if (tail & 2) CopyBackwards<2>(to, from);
        if (tail & 4) CopyBackwards<4>(to, from);
        if (tail & 8) CopyBackwards<8>(to, from);

        for (; size >= 64; size -= 64)
            CopyBackwards<64>(to, from);

        if (size & 32) CopyBackwards<32>(to, from);
        if (size & 16) CopyBackwards<16>(to, from);
        if (size & 8) CopyBackwards<8>(to, from);
        if (size & 4) CopyBackwards<4>(to, from);
        if (size & 2) CopyBackwards<2>(to, from);
        if (size & 1) CopyBackwards<1>(to, from);
    }

    return destination;
}

// _stricmp (491 call sites). Per byte: b's, then a's; r3 = a - b (sign-extended). The end of b returns that raw
// difference; equal bytes go on; otherwise both are folded to lower case ('A'-'Z' | 0x20 only) and a non-zero
// difference is returned, else it goes on.
//     lbzu r6,1(r4); lbzu r5,1(r9); cmpwi cr7,r6,0; subf. r3,r6,r5; beq cr7,end; beq loop; <fold r6, r5>; subf. r3,r6,r5
PPC_FUNC_IMPL(__imp__sub_826E6BB0);
PPC_FUNC(sub_826E6BB0)
{
    if (!g_switchNativeCrtStrings)
    {
        __imp__sub_826E6BB0(ctx, base);
        return;
    }

    uint32_t a = ctx.r3.u32;
    uint32_t b = ctx.r4.u32;
    for (;; a++, b++)
    {
        // Eight equal bytes, none of them b's end, are eight turns of the loop that go on.
        uint64_t wordA, wordB;
        while (LoadWord(base, a, wordA) && LoadWord(base, b, wordB) && wordA == wordB && !HasZeroByte(wordB))
        {
            a += 8;
            b += 8;
        }

        const int32_t cb = base[b];
        const int32_t ca = base[a];
        if (cb == 0)
        {
            ctx.r3.s64 = ca;
            return;
        }

        if (ca == cb)
            continue;

        const int32_t fb = (cb >= 'A' && cb <= 'Z') ? (cb | 0x20) : cb;
        const int32_t fa = (ca >= 'A' && ca <= 'Z') ? (ca | 0x20) : ca;
        if (fa != fb)
        {
            ctx.r3.s64 = fa - fb;
            return;
        }
    }
}

// strncpy(dst r3, src r4, n r5) (109 call sites): copies up to n bytes, the terminator included, then zero-fills the
// rest of n; returns dst (r3 untouched). The guest copies a byte at a time, forwards; overlapping ranges are copied the
// same way here, byte by byte.
PPC_FUNC_IMPL(__imp__sub_826DFCE0);
PPC_FUNC(sub_826DFCE0)
{
    if (!g_switchNativeCrtStrings)
    {
        __imp__sub_826DFCE0(ctx, base);
        return;
    }

    const uint32_t destination = ctx.r3.u32;
    const uint32_t source = ctx.r4.u32;
    const uint32_t count = ctx.r5.u32;
    if (count == 0)
        return;

    if (InGuestSpace(destination, count) && InGuestSpace(source, count) && !Overlap(destination, count, source, count))
    {
        CopyString(base, destination, source, count);
        return;
    }

    uint32_t to = destination;
    uint32_t from = source;
    uint32_t remaining = count;
    for (;;)
    {
        const uint8_t value = base[from++];
        base[to++] = value;
        if (value == 0)
            break;
        if (--remaining == 0)
            return;
    }

    // The terminator is counted in `remaining`.
    for (uint32_t zeros = remaining - 1; zeros != 0; zeros--)
        base[to++] = 0;
}

// strchr(s r3, c r4) (39 call sites): the first byte equal to c (compared as a 32-bit int) before the terminator,
// or for c == 0 the terminator itself. Found, r3's low word becomes its address (lbzu writes only those 32 bits);
// not found, r3 = 0. A c outside 0-255, which no byte equals, runs the recompiled code.
PPC_FUNC_IMPL(__imp__sub_826E49D0);
PPC_FUNC(sub_826E49D0)
{
    const int32_t character = ctx.r4.s32;
    if (!g_switchNativeCrtStrings || character < 0 || character > 0xFF)
    {
        __imp__sub_826E49D0(ctx, base);
        return;
    }

    const uint64_t pattern = 0x0101010101010101ull * uint32_t(character);
    uint32_t address = ctx.r3.u32;
    for (;; address++)
    {
        // Eight bytes that are neither c nor the terminator are eight turns of the loop that go on.
        uint64_t word;
        while (LoadWord(base, address, word) && !HasZeroByte(word) && !HasZeroByte(word ^ pattern))
            address += 8;

        const int32_t value = base[address];
        if (value == character)
        {
            ctx.r3.u32 = address;
            return;
        }

        if (value == 0)
        {
            ctx.r3.u64 = 0;
            return;
        }
    }
}

// The CRT's second memcpy (36 call sites; sub_826DF680 is the main one): bytes up to an aligned destination, then
// words, then bytes, forwards; returns dst (r3 untouched). For ranges that do not overlap that is memcpy; overlapping
// ones run the recompiled code, whose result then depends on its copy granularity.
PPC_FUNC_IMPL(__imp__sub_826DFAA0);
PPC_FUNC(sub_826DFAA0)
{
    const uint64_t destination = ctx.r3.u32;
    const uint64_t source = ctx.r4.u32;
    const uint64_t count = ctx.r5.u32;
    if (!g_switchNativeCrtStrings || !InGuestSpace(destination, count) || !InGuestSpace(source, count) ||
        Overlap(destination, count, source, count))
    {
        __imp__sub_826DFAA0(ctx, base);
        return;
    }

    GuestMemmove(base + destination, base + source, count);
}

#endif
