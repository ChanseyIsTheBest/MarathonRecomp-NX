#include <stdafx.h>
#include "memory.h"

#if defined(__SWITCH__)
#include <cstring>
#include <limits>
#include <switch.h>
#include <malloc.h>
#include <mutex>
#include <unordered_set>
#include <os/logger.h>

namespace
{
constexpr size_t SWITCH_PAGE_SIZE = 0x1000;
constexpr size_t SWITCH_LOW_MEMORY_COMMIT_END = 0x20000;
constexpr size_t SWITCH_XMAIO_BEGIN = 0x7FEA0000;
constexpr size_t SWITCH_XMAIO_SIZE = 0x10000;
constexpr unsigned SWITCH_SVC_MAP_PROCESS_MEMORY = 0x74;
constexpr unsigned SWITCH_SVC_UNMAP_PROCESS_MEMORY = 0x75;
constexpr unsigned SWITCH_SVC_MAP_PROCESS_CODE_MEMORY = 0x77;
constexpr unsigned SWITCH_SVC_UNMAP_PROCESS_CODE_MEMORY = 0x78;

constexpr uintptr_t AlignUp(uintptr_t value, uintptr_t alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

constexpr uintptr_t AlignDown(uintptr_t value, uintptr_t alignment) noexcept
{
    return value & ~(alignment - 1);
}

bool AddOverflows(uintptr_t value, size_t addend) noexcept
{
    return value > std::numeric_limits<uintptr_t>::max() - addend;
}

void CaptureSwitchMemoryRegions(Memory& memory) noexcept
{
    svcGetInfo(&memory.switchAliasBase, InfoType_AliasRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchAliasSize, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchAslrBase, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchAslrSize, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchHeapBase, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchHeapSize, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
}

bool HasSwitchProcessMemorySyscalls() noexcept
{
    return envIsSyscallHinted(SWITCH_SVC_MAP_PROCESS_MEMORY) &&
        envIsSyscallHinted(SWITCH_SVC_UNMAP_PROCESS_MEMORY) &&
        envIsSyscallHinted(SWITCH_SVC_MAP_PROCESS_CODE_MEMORY) &&
        envIsSyscallHinted(SWITCH_SVC_UNMAP_PROCESS_CODE_MEMORY);
}

void CaptureSwitchCommitFailure(Memory& memory, uintptr_t guestOffset, uintptr_t address, Result result) noexcept
{
    memory.switchInitResult = static_cast<uint32_t>(result);
    memory.switchCommitFailureOffset = guestOffset;
    memory.switchCommitFailureAddress = address;

    MemoryInfo info{};
    u32 pageInfo = 0;
    if (R_SUCCEEDED(svcQueryMemory(&info, &pageInfo, address)))
    {
        memory.switchCommitFailureMemoryBase = info.addr;
        memory.switchCommitFailureMemorySize = info.size;
        memory.switchCommitFailureMemoryType = info.type;
        memory.switchCommitFailureMemoryAttr = info.attr;
        memory.switchCommitFailureMemoryPerm = info.perm;
        memory.switchCommitFailurePageInfo = pageInfo;
    }
}

bool MapSwitchProcessMemoryRange(Memory& memory, size_t offset, size_t size) noexcept
{
    if (size == 0)
        return true;

    if (AddOverflows(offset, size))
        return false;

    const size_t alignedOffset = AlignDown(offset, SWITCH_PAGE_SIZE);
    const size_t alignedEnd = AlignUp(offset + size, SWITCH_PAGE_SIZE);
    const size_t alignedSize = alignedEnd - alignedOffset;
    const uintptr_t destination = reinterpret_cast<uintptr_t>(memory.base) + alignedOffset;

    // Commit = memalign backing from the newlib heap, mapped as code memory and
    // mirrored into the guest window. This is the only mechanism available to
    // hbl processes (svcMapPhysicalMemory needs a system resource we lack) and
    // is capped at ~2.25 GB of total mappings. 2 MB-align large chunks so the
    // kernel can use block descriptors.
    constexpr size_t SWITCH_MAP_BLOCK_SIZE = 0x200000;
    size_t mapAlign =
        (alignedSize >= SWITCH_MAP_BLOCK_SIZE && (alignedOffset & (SWITCH_MAP_BLOCK_SIZE - 1)) == 0)
            ? SWITCH_MAP_BLOCK_SIZE
            : SWITCH_PAGE_SIZE;

    void* backing = memalign(mapAlign, alignedSize);
    // [Switch] A 2 MB-aligned block may not be available where a 4 KB-aligned one is: then the commit is mapped with
    // 4 KB pages, as before, rather than failing.
    if (backing == nullptr && mapAlign != SWITCH_PAGE_SIZE)
    {
        mapAlign = SWITCH_PAGE_SIZE;
        backing = memalign(mapAlign, alignedSize);
    }
    if (backing == nullptr)
    {
        memory.switchInitFailureReason = "Switch backing allocation failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, destination, 0);
        return false;
    }

    std::memset(backing, 0, alignedSize);

    void* codeAlias = nullptr;
    bool codeAliasMapped = false;
    Result rc = 0;

    virtmemLock();
    codeAlias = virtmemFindCodeMemory(alignedSize, mapAlign);
    if (codeAlias == nullptr && mapAlign != SWITCH_PAGE_SIZE)
        codeAlias = virtmemFindCodeMemory(alignedSize, SWITCH_PAGE_SIZE);
    if (codeAlias != nullptr)
    {
        rc = svcMapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), reinterpret_cast<uintptr_t>(backing), alignedSize);
        if (R_SUCCEEDED(rc))
        {
            codeAliasMapped = true;
            rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), alignedSize, Perm_Rw);
        }
    }
    virtmemUnlock();

    if (codeAlias == nullptr)
    {
        free(backing);
        memory.switchInitFailureReason = "virtmemFindCodeMemory failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, destination, 0);
        return false;
    }

    if (R_FAILED(rc))
    {
        if (codeAliasMapped)
            svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), reinterpret_cast<uintptr_t>(backing), alignedSize);

        free(backing);
        memory.switchInitFailureReason = "code memory alias setup failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, reinterpret_cast<uintptr_t>(codeAlias), rc);
        return false;
    }

    rc = svcMapProcessMemory(reinterpret_cast<void*>(destination), envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), alignedSize);
    if (R_FAILED(rc))
    {
        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), reinterpret_cast<uintptr_t>(backing), alignedSize);
        free(backing);
        memory.switchInitFailureReason = "svcMapProcessMemory failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, destination, rc);
        return false;
    }

    try
    {
        memory.switchCommitChunks.push_back({ alignedOffset, alignedSize, backing, codeAlias });
    }
    catch (...)
    {
        svcUnmapProcessMemory(reinterpret_cast<void*>(destination), envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), alignedSize);
        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), reinterpret_cast<uintptr_t>(backing), alignedSize);
        free(backing);
        memory.switchInitFailureReason = "commit chunk tracking allocation failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, destination, 0);
        return false;
    }

    return true;
}
}
#endif

#if defined(__SWITCH__)
// A guest indirect call (bctrl) whose target is not a recompiled function:
//   - target INSIDE the code range but never recompiled -> its lookup-table slot
//     is pre-filled with MissingFunctionTrap (below), OR
//   - target OUTSIDE the code range (NULL, or a corrupted/zero-filled vtable
//     slot) -> the recompiler's PPC_CALL_INDIRECT_FUNC bounds-checks and calls
//     this directly, because such a target would index the lookup table out of
//     bounds and fault with no diagnostic.
// Either way we run in normal guest-thread context (unlike the fault handler),
// so we log the guest target + caller (once per unique target) and return a null
// result, letting the game limp past an unresolved virtual call instead of
// crashing. This is on all indirect calls, so keep the fast path branch-light.
extern "C" void PPCIndirectCallTrap(PPCContext& ctx, uint8_t* base, uint32_t target)
{
    (void)base;
#if !defined(PPC_CONFIG_SKIP_LR)
    const uint32_t caller = static_cast<uint32_t>(ctx.lr);
#else
    // Generated with XENON_RECOMP_REGISTER_LOCALS=1 (skip_lr): the link register is not kept.
    const uint32_t caller = 0;
#endif

    static std::mutex s_mutex;
    static std::unordered_set<uint32_t> s_seen;
    bool firstSeen = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_seen.size() < 4096 && s_seen.insert(target).second)
            firstSeen = true;
    }
    if (firstSeen)
        LOGFN_ERROR("!!! Bad indirect call: target=0x{:08X} (from lr=0x{:08X})", target, caller);

    ctx.r3.u64 = 0;
}

namespace
{
// Pre-filled into every lookup-table slot; the recompiled bctrl passes the
// resolved target in ctr, so forward it to the shared trap.
void MissingFunctionTrap(PPCContext& __restrict__ ctx, uint8_t* base)
{
#if !defined(PPC_CONFIG_CTR_AS_LOCAL)
    PPCIndirectCallTrap(ctx, base, ctx.ctr.u32);
#else
    // Generated with XENON_RECOMP_REGISTER_LOCALS=1: ctr is a local of the caller, the target is unknown here
    // (logged as 0). The trap still returns a null result, as before.
    PPCIndirectCallTrap(ctx, base, 0);
#endif
}
}
#endif

Memory::Memory()
{
#ifdef _WIN32
    base = (uint8_t*)VirtualAlloc((void*)0x100000000ull, PPC_MEMORY_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (base == nullptr)
        base = (uint8_t*)VirtualAlloc(nullptr, PPC_MEMORY_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (base == nullptr)
        return;

    DWORD oldProtect;
    VirtualProtect(base, 4096, PAGE_NOACCESS, &oldProtect);
#elif defined(__SWITCH__)
    CaptureSwitchMemoryRegions(*this);

    const size_t lookupTableSize = static_cast<size_t>(PPC_CODE_SIZE) * 2;
    const size_t imageAndLookupSize = static_cast<size_t>(PPC_IMAGE_SIZE) + lookupTableSize;

    if (!HasSwitchProcessMemorySyscalls())
    {
        switchInitFailureReason = "process memory syscalls are not hinted";
        return;
    }

    virtmemLock();
    // 2 MB-aligned so large commits can map with 2 MB blocks where the
    // physical layout allows (see MapSwitchProcessMemoryRange).
    // One more 64 KB after the 4 GB window, reserved and never committed: see SWITCH_WRAP_GUARD_SIZE.
    base = static_cast<uint8_t*>(virtmemFindAslr(PPC_MEMORY_SIZE + SWITCH_WRAP_GUARD_SIZE, 0x200000));
    if (base != nullptr)
        reservation = virtmemAddReservation(base, PPC_MEMORY_SIZE + SWITCH_WRAP_GUARD_SIZE);
    virtmemUnlock();

    if (base == nullptr)
    {
        switchInitFailureReason = "virtmemFindAslr 4GB window failed";
        return;
    }

    switchSelectedBase = reinterpret_cast<uintptr_t>(base);

    if (reservation == nullptr)
    {
        switchInitFailureReason = "virtmemAddReservation failed";
        base = nullptr;
        return;
    }

    try
    {
        committedPages.assign(PPC_MEMORY_SIZE / SWITCH_PAGE_SIZE, 0);
    }
    catch (...)
    {
        switchInitFailureReason = "committed page table allocation failed";
        base = nullptr;
        return;
    }

    if (!CommitRange(SWITCH_PAGE_SIZE, SWITCH_LOW_MEMORY_COMMIT_END - SWITCH_PAGE_SIZE))
    {
        switchInitFailureReason = "initial low memory commit failed";
        base = nullptr;
        return;
    }

    if (!CommitRange(PPC_IMAGE_BASE, imageAndLookupSize))
    {
        switchInitFailureReason = "initial image/lookup commit failed";
        base = nullptr;
        return;
    }

    if (!CommitRange(SWITCH_XMAIO_BEGIN, SWITCH_XMAIO_SIZE))
    {
        switchInitFailureReason = "initial XMA IO commit failed";
        base = nullptr;
        return;
    }

    switchInitFailureReason = nullptr;
#else
    base = (uint8_t*)mmap((void*)0x100000000ull, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (base == (uint8_t*)MAP_FAILED)
        base = (uint8_t*)mmap(NULL, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (base == nullptr)
        return;

    mprotect(base, 4096, PROT_NONE);
#endif

#if defined(__SWITCH__)
    // Pre-fill the entire guest function lookup table with the missing-function
    // trap; the InsertFunction loop below overwrites the slots that XenonRecomp
    // actually recompiled, leaving the trap in place only for the gaps.
    if (base != nullptr)
    {
        PPCFunc** lookupTable = reinterpret_cast<PPCFunc**>(base + PPC_IMAGE_BASE + PPC_IMAGE_SIZE);
        const size_t lookupSlots = (static_cast<size_t>(PPC_CODE_SIZE) * 2) / sizeof(PPCFunc*);
        for (size_t i = 0; i < lookupSlots; i++)
            lookupTable[i] = MissingFunctionTrap;
    }
#endif

    for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
    {
        if (PPCFuncMappings[i].host != nullptr)
            InsertFunction(PPCFuncMappings[i].guest, PPCFuncMappings[i].host);
    }
}

#if defined(__SWITCH__)
bool Memory::CommitRange(size_t offset, size_t size) noexcept
{
    if (base == nullptr || size == 0)
        return base != nullptr;

    if (offset >= PPC_MEMORY_SIZE || size > PPC_MEMORY_SIZE - offset)
        return false;

    const size_t begin = AlignDown(offset, SWITCH_PAGE_SIZE);
    const size_t end = AlignUp(offset + size, SWITCH_PAGE_SIZE);

    std::lock_guard lock(commitMutex);

    size_t pageOffset = begin;
    while (pageOffset < end)
    {
        const size_t pageIndex = pageOffset / SWITCH_PAGE_SIZE;
        if (committedPages[pageIndex])
        {
            pageOffset += SWITCH_PAGE_SIZE;
            continue;
        }

        const size_t runStart = pageOffset;
        do
        {
            pageOffset += SWITCH_PAGE_SIZE;
        }
        while (pageOffset < end && !committedPages[pageOffset / SWITCH_PAGE_SIZE]);

        if (!MapSwitchProcessMemoryRange(*this, runStart, pageOffset - runStart))
            return false;

        for (size_t committedOffset = runStart; committedOffset < pageOffset; committedOffset += SWITCH_PAGE_SIZE)
            committedPages[committedOffset / SWITCH_PAGE_SIZE] = 1;
    }

    return true;
}

bool Memory::IsRangeCommitted(size_t offset, size_t size) const noexcept
{
    if (size == 0 || offset >= PPC_MEMORY_SIZE || size > PPC_MEMORY_SIZE - offset)
        return false;

    const size_t firstPage = offset / SWITCH_PAGE_SIZE;
    const size_t lastPage = (offset + size - 1) / SWITCH_PAGE_SIZE;

    std::lock_guard lock(commitMutex);

    if (lastPage >= committedPages.size())
        return false;

    for (size_t page = firstPage; page <= lastPage; ++page)
    {
        if (!committedPages[page])
            return false;
    }

    return true;
}

bool Memory::CommitHostRange(const void* host, size_t size) noexcept
{
    if (host == nullptr || size == 0)
        return true;

    if (!IsInMemoryRange(host))
        return false;

    return CommitRange(static_cast<const uint8_t*>(host) - base, size);
}
#endif

void* MmGetHostAddress(uint32_t ptr)
{
    return g_memory.Translate(ptr);
}
