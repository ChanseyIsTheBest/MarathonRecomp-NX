#if defined(__SWITCH__)

#include <stdafx.h>
#include <os/switch/perf/native_hooks.h>
#include <os/switch_cpu_profiler.h>

#include <atomic>
#include <cstdio>
#include <cstring>

// [Switch] SwitchNativeRtti, SwitchNativeDynamicCast (native_hooks.h): the MSVC CRT's RTTI functions as native code.
// All of the game's RTTI data (complete object locators, class hierarchy and base class descriptors in .rdata, type
// descriptors in .data) lies in the executable's image and is never written, apart from a type descriptor's spare
// word (+4, type_info::name's cache of the undecorated name), which nothing here reads.

namespace
{
    bool InImage(uint32_t address, uint32_t size)
    {
        return address >= PPC_IMAGE_BASE && uint64_t(address) + size <= PPC_IMAGE_BASE + PPC_IMAGE_SIZE;
    }

    // The guest's name loops (below) stop at the end of the first name or at the first difference and return equal
    // exactly when the two strings are the same, which is strcmp(...) == 0.
    bool NamesEqual(uint8_t* base, uint32_t first, uint32_t second)
    {
        return strcmp(reinterpret_cast<const char*>(base + first), reinterpret_cast<const char*>(base + second)) == 0;
    }
}

// type_info::operator== (MSVC CRT, 1,124 call sites): the decorated names, from offset 9 of each type_info (after
// the '.'), compared byte by byte up to the end of the second; 1 when they are the same string:
//     addi r10,r3,9; addi r11,r4,9; lbz r9,0(r11); lbz r8,0(r10); ...; cntlzw; rlwinm 27,31,31; clrlwi r3,r11,24
// The results for pairs of type_infos in the image, whose names never change, are remembered in one lock-free table
// of 64-bit words: the pair (the second address is 4-byte aligned and carries the result in bit 0), so a reader sees
// a whole entry or none. The same object is the same name.
static std::atomic<uint64_t> g_typeInfoComparisons[1024];

PPC_FUNC_IMPL(__imp__sub_826DE850);
PPC_FUNC(sub_826DE850)
{
    if (!g_switchNativeRtti)
    {
        __imp__sub_826DE850(ctx, base);
        return;
    }

    const uint32_t a = ctx.r3.u32;
    const uint32_t b = ctx.r4.u32;
    if (a == b)
    {
        ctx.r3.u64 = 1;
        return;
    }

    const bool cacheable = InImage(a, 256) && InImage(b, 256) && ((a | b) & 3) == 0;
    const uint64_t key = (uint64_t(a) << 32) | b;
    std::atomic<uint64_t>& slot = g_typeInfoComparisons[((a * 0x9E3779B1u) ^ (b * 0x85EBCA77u)) >> 22];
    if (cacheable)
    {
        const uint64_t entry = slot.load(std::memory_order_relaxed);
        if ((entry & ~uint64_t(1)) == key)
        {
            ctx.r3.u64 = entry & 1;
            return;
        }
    }

    const uint32_t equal = NamesEqual(base, a + 9, b + 9) ? 1 : 0;
    if (cacheable)
        slot.store(key | equal, std::memory_order_relaxed);

    ctx.r3.u64 = equal;
}

// __RTtypeid (MSVC CRT): the type descriptor of the complete object r3 points to, from its vftable's complete object
// locator (offset 12). A null object or a locator without one throws: the recompiled code does that.
PPC_FUNC_IMPL(__imp__sub_826DEE58);
PPC_FUNC(sub_826DEE58)
{
    if (g_switchNativeRtti && ctx.r3.u32 != 0)
    {
        const uint32_t locator = PPC_LOAD_U32(PPC_LOAD_U32(ctx.r3.u32) - 4);
        const uint32_t typeDescriptor = PPC_LOAD_U32(locator + 12);
        if (typeDescriptor != 0)
        {
            ctx.r3.u64 = typeDescriptor;
            return;
        }
    }

    __imp__sub_826DEE58(ctx, base);
}

// __RTDynamicCast(object r3, vfDelta r4, sourceType r5, targetType r6, isReference r7) (MSVC CRT, 1,157 call sites).
// Its finders compare the decorated names inline (from offset 8), not through type_info::operator==, so the memo
// above does not help them. Native here for the classes whose hierarchy has single inheritance (attribute bit 0
// clear: FindSITargetTypeInstance, sub_826DEF38), the game's usual case:
// - the complete object is object - locator.offset, minus *(object - locator.cdOffset) when cdOffset is not 0;
// - the finder walks the base class array for the target, then on from there for the source, and gives up at a base
//   class descriptor whose attribute 4 is set; its result is the target's descriptor or none;
// - the result is complete + mdisp, plus the virtual base displacement read from the object when pdisp >= 0;
//   none is 0, or bad_cast for a reference.
// The arithmetic is the guest's (64-bit adds, 32-bit loads). The finder's answer depends only on the hierarchy and the
// two type descriptors, all in the image and never written, so it is remembered per (hierarchy, source, target).
// Multiple and virtual inheritance, casts that throw, and RTTI data outside the image run the recompiled code, which
// SwitchVerifyNativeDynamicCast also runs for every native cast, comparing the results. Compared on the build PC with
// the recompiled code: identical for an object of each of the executable's 5,106 classes cast between the types of
// its hierarchy and others (567,480 casts), computed and remembered.
namespace
{
    // Result byte of a memo entry: 0 empty, 1-254 the target's index in the base class array + 1, 255 no instance.
    constexpr uint32_t kMaxBaseClasses = 254;
    constexpr uint32_t kNoInstance = 0xFF;
    constexpr uint32_t kUnknown = 0x100;

    // One 64-bit word per entry, written and read whole: the hierarchy and the source as 22-bit word offsets into the
    // image (bits 41-62 and 19-40), the target's word offset above the 11 bits the slot index already fixes (bits
    // 8-18), and the result byte. The slot is the hash of hierarchy and source XOR the target's low 11 bits, so an
    // entry whose stored bits match was stored for exactly the same three descriptors.
    std::atomic<uint64_t> g_dynamicCastMemo[2048];

    uint32_t ImageWord(uint32_t address)
    {
        return (address - uint32_t(PPC_IMAGE_BASE)) >> 2;
    }

    // FindSITargetTypeInstance (sub_826DEF38) as an index into the base class array: kNoInstance for its 0, kUnknown
    // when any descriptor lies outside the image or the array is too long to remember.
    uint32_t FindSITargetIndex(uint8_t* base, uint32_t hierarchy, uint32_t source, uint32_t target)
    {
        const uint32_t count = PPC_LOAD_U32(hierarchy + 8);
        const uint32_t array = PPC_LOAD_U32(hierarchy + 12);
        if (count > kMaxBaseClasses || !InImage(array, count * 4))
            return kUnknown;

        uint32_t targetIndex = 0;
        for (; targetIndex < count; targetIndex++)
        {
            const uint32_t descriptor = PPC_LOAD_U32(array + targetIndex * 4);
            if (!InImage(descriptor, 24))
                return kUnknown;

            const uint32_t type = PPC_LOAD_U32(descriptor);
            if (type == target)
                break;
            if (!InImage(type, 9))
                return kUnknown;
            if (NamesEqual(base, type + 8, target + 8))
                break;
        }

        if (targetIndex == count)
            return kNoInstance;

        for (uint32_t i = targetIndex + 1; i < count; i++)
        {
            const uint32_t descriptor = PPC_LOAD_U32(array + i * 4);
            if (!InImage(descriptor, 24))
                return kUnknown;
            if ((PPC_LOAD_U32(descriptor + 20) & 4) != 0)
                return kNoInstance;

            const uint32_t type = PPC_LOAD_U32(descriptor);
            if (type == source)
                return targetIndex;
            if (!InImage(type, 9))
                return kUnknown;
            if (NamesEqual(base, type + 8, source + 8))
                return targetIndex;
        }

        return kNoInstance;
    }

    // The cast's result in `result`; false for what the recompiled code has to do.
    bool NativeDynamicCast(PPCContext& ctx, uint8_t* base, uint64_t& result)
    {
        const uint32_t locator = PPC_LOAD_U32(PPC_LOAD_U32(ctx.r3.u32) - 4);
        if (!InImage(locator, 20))
            return false;

        const uint32_t hierarchy = PPC_LOAD_U32(locator + 16);
        const uint32_t source = ctx.r5.u32;
        const uint32_t target = ctx.r6.u32;
        if (!InImage(hierarchy, 16) || !InImage(source, 9) || !InImage(target, 9) || ((hierarchy | source | target) & 3) != 0)
            return false;

        if ((PPC_LOAD_U32(hierarchy + 4) & 1) != 0)
            return false;

        const uint32_t hierarchyWord = ImageWord(hierarchy);
        const uint32_t sourceWord = ImageWord(source);
        const uint32_t targetWord = ImageWord(target);
        const uint32_t slotIndex = (((hierarchyWord * 0x9E3779B1u) ^ (sourceWord * 0x85EBCA77u)) >> 21 ^ targetWord) & 2047;
        const uint64_t key = (uint64_t(hierarchyWord) << 41) | (uint64_t(sourceWord) << 19) | (uint64_t(targetWord >> 11) << 8);
        std::atomic<uint64_t>& slot = g_dynamicCastMemo[slotIndex];

        uint32_t found;
        const uint64_t entry = slot.load(std::memory_order_relaxed);
        if ((entry & ~uint64_t(0xFF)) == key && (entry & 0xFF) != 0)
        {
            found = uint32_t(entry & 0xFF);
            found = found == kNoInstance ? kNoInstance : found - 1;
        }
        else
        {
            found = FindSITargetIndex(base, hierarchy, source, target);
            if (found == kUnknown)
                return false;

            slot.store(key | (found == kNoInstance ? kNoInstance : found + 1), std::memory_order_relaxed);
        }

        if (found == kNoInstance)
        {
            // A reference cast throws bad_cast.
            if (ctx.r7.s32 != 0)
                return false;

            result = 0;
            return true;
        }

        uint64_t complete = ctx.r3.u64 - uint64_t(PPC_LOAD_U32(locator + 4));
        const uint32_t completeDisplacement = PPC_LOAD_U32(locator + 8);
        if (completeDisplacement != 0)
            complete -= PPC_LOAD_U32(uint32_t(ctx.r3.u64 - completeDisplacement));

        const uint32_t descriptor = PPC_LOAD_U32(PPC_LOAD_U32(hierarchy + 12) + found * 4);
        uint64_t displacement = 0;
        const uint64_t pdisp = PPC_LOAD_U32(descriptor + 12);
        if (int32_t(pdisp) >= 0)
        {
            const uint64_t vdisp = PPC_LOAD_U32(descriptor + 16);
            const uint64_t vbtable = PPC_LOAD_U32(uint32_t(pdisp + complete));
            displacement = uint64_t(PPC_LOAD_U32(uint32_t(vbtable + vdisp))) + pdisp;
        }

        result = uint64_t(PPC_LOAD_U32(descriptor + 8)) + displacement + complete;
        return true;
    }

    std::atomic<uint64_t> g_dynamicCastsCompared;
    std::atomic<uint32_t> g_dynamicCastMismatches;
}

PPC_FUNC_IMPL(__imp__sub_826DF418);
PPC_FUNC(sub_826DF418)
{
    if (!g_switchNativeDynamicCast)
    {
        __imp__sub_826DF418(ctx, base);
        return;
    }

    // A null object: 0, r3 as it is.
    if (ctx.r3.u32 == 0)
        return;

    uint64_t result;
    if (!NativeDynamicCast(ctx, base, result))
    {
        __imp__sub_826DF418(ctx, base);
        return;
    }

    if (!g_switchVerifyNativeDynamicCast)
    {
        ctx.r3.u64 = result;
        return;
    }

    const uint32_t object = ctx.r3.u32;
    const uint32_t source = ctx.r5.u32;
    const uint32_t target = ctx.r6.u32;
    __imp__sub_826DF418(ctx, base);

    const uint64_t compared = ++g_dynamicCastsCompared;
    if (ctx.r3.u64 != result)
    {
        const uint32_t mismatches = ++g_dynamicCastMismatches;
        if (mismatches <= 20)
        {
            char line[192];
            const int length = snprintf(line, sizeof(line), "[rtti] MISMATCH #%u: dynamic_cast of 0x%08X from 0x%08X to 0x%08X: "
                "game 0x%llX, native 0x%llX\n", mismatches, object, source, target, (unsigned long long)ctx.r3.u64, (unsigned long long)result);
            os::switch_cpu_profiler::WriteLog(line, size_t(std::max(length, 0)));
        }
    }

    if ((compared & ((1u << 20) - 1)) == 0)
    {
        char line[160];
        const int length = snprintf(line, sizeof(line), "[rtti] %llu native dynamic_casts compared with the game's, %s\n",
            (unsigned long long)compared, g_dynamicCastMismatches.load() == 0 ? "all identical" : "MISMATCHES");
        os::switch_cpu_profiler::WriteLog(line, size_t(std::max(length, 0)));
    }
}

#endif
