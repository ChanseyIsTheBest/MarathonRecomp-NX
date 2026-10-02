#include <stdafx.h>
#include <bit>
#include <cstdint>
#include "heap.h"
#include "memory.h"
#include "function.h"
#include "xdm.h"

#if defined(__SWITCH__)
#include <switch.h>
#include <os/logger.h>
#endif

constexpr size_t USER_HEAP_BEGIN = 0x20000;
constexpr size_t RESERVED_BEGIN = 0x7FEA0000;
constexpr size_t RESERVED_END = 0xA0000000;
constexpr size_t GUEST_ADDRESS_SPACE_SIZE = 0x100000000ull;
constexpr size_t USER_HEAP_SIZE = RESERVED_BEGIN - USER_HEAP_BEGIN;
constexpr size_t PHYSICAL_HEAP_SIZE = GUEST_ADDRESS_SPACE_SIZE - RESERVED_END;

#if defined(__SWITCH__)
namespace
{
constexpr size_t SWITCH_HEAP_INITIAL_COMMIT = 1 * 1024 * 1024;
constexpr size_t SWITCH_HEAP_COMMIT_GRANULARITY = 16 * 1024 * 1024;

size_t AlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// The fragment o1heap takes for an allocation: the smallest power of two that holds it and its header. One
// instruction (count leading zeros) instead of a doubling loop of up to ~30 iterations inside the heap lock; the
// same value for every size (std::bit_ceil is the smallest power of two >= its argument, and the argument is >= 1).
size_t RoundHeapFragmentSize(size_t size)
{
    return std::bit_ceil(std::max<size_t>(1, size) + O1HEAP_ALIGNMENT);
}

// [Switch] Commits end on absolute 16 MB boundaries of the guest window (not 16 MB past the range's start, which is
// 0x20000 for the user heap), and a commit that starts off a 2 MB boundary is split there: every later commit then
// starts 2 MB-aligned, which lets MapSwitchProcessMemoryRange take 2 MB-aligned backing and code memory, so the kernel
// can map the game's heap with 2 MB blocks instead of 4 KB pages (TLB reach; the perf5 profile's hot lines include many
// first loads of heap objects). The same guest addresses are committed, zero-filled, at the same allocations; only how
// the pages are grouped into commits changes.
constexpr size_t SWITCH_MAP_BLOCK_SIZE = 2 * 1024 * 1024;

bool EnsureCommittedPrefix(size_t rangeStart, size_t rangeSize, size_t& committedPrefix, size_t requestedPrefix)
{
    const size_t requestedEnd = AlignUp(rangeStart + std::min(rangeSize, requestedPrefix), SWITCH_HEAP_COMMIT_GRANULARITY);
    requestedPrefix = std::min(rangeSize, requestedEnd - rangeStart);

    if (requestedPrefix <= committedPrefix)
        return true;

    const size_t from = rangeStart + committedPrefix;
    const size_t to = rangeStart + requestedPrefix;
    const size_t boundary = AlignUp(from, SWITCH_MAP_BLOCK_SIZE);
    if (from < boundary && boundary < to)
    {
        if (!g_memory.CommitRange(from, boundary - from))
            return false;

        committedPrefix = boundary - rangeStart;
    }

    if (!g_memory.CommitRange(rangeStart + committedPrefix, requestedPrefix - committedPrefix))
        return false;

    committedPrefix = requestedPrefix;
    return true;
}

// Commit enough to cover an allocation landing at the frontier. o1heap reuses
// freed low fragments, so most allocations do not advance the frontier.
bool PreCommitForAllocation(size_t rangeStart, size_t rangeSize, size_t& committedPrefix, size_t frontier, size_t fragmentSize)
{
    const size_t needed = std::min(rangeSize, frontier + fragmentSize + O1HEAP_ALIGNMENT);
    if (needed <= committedPrefix)
        return true;

    return EnsureCommittedPrefix(rangeStart, rangeSize, committedPrefix, needed);
}

// End offset of the returned fragment. The frontier is the max of these, so
// freed-and-reused low space does not inflate the committed prefix.
size_t FragmentEndOffset(const void* arenaStart, const void* ptr, size_t fragmentSize)
{
    const size_t offset = static_cast<size_t>(static_cast<const uint8_t*>(ptr) - static_cast<const uint8_t*>(arenaStart));
    return offset - O1HEAP_ALIGNMENT + fragmentSize;
}
}
#endif

void Heap::Init()
{
#if defined(__SWITCH__)
    committedHeapPrefix = 0;
    committedPhysicalHeapPrefix = 0;
    touchedHeapPrefix = 0;
    touchedPhysicalHeapPrefix = 0;

    // Commit lazily: a small initial prefix here, grown on demand in
    // Alloc/AllocPhysical. The hbl process-mapping budget (~2.25 GB) is shared
    // with the GPU driver, so committing the full arenas up front starves the
    // GPU; the full-size arenas cost no budget beyond what is touched.
    if (!EnsureCommittedPrefix(USER_HEAP_BEGIN, USER_HEAP_SIZE, committedHeapPrefix, SWITCH_HEAP_INITIAL_COMMIT) ||
        !EnsureCommittedPrefix(RESERVED_END, PHYSICAL_HEAP_SIZE, committedPhysicalHeapPrefix, SWITCH_HEAP_INITIAL_COMMIT))
    {
        heap = nullptr;
        physicalHeap = nullptr;
        return;
    }

    heap = o1heapInit(g_memory.Translate(USER_HEAP_BEGIN), USER_HEAP_SIZE);
    physicalHeap = o1heapInit(g_memory.Translate(RESERVED_END), PHYSICAL_HEAP_SIZE);
#else
    heap = o1heapInit(g_memory.Translate(USER_HEAP_BEGIN), USER_HEAP_SIZE);
    physicalHeap = o1heapInit(g_memory.Translate(RESERVED_END), PHYSICAL_HEAP_SIZE);
#endif
}

void* Heap::Alloc(size_t size)
{
    size = std::max<size_t>(1, size);
#if defined(__SWITCH__)
    const size_t fragmentSize = RoundHeapFragmentSize(size); // Depends on the size alone: outside the lock.
#endif

    std::lock_guard lock(mutex);
    if (heap == nullptr)
        return nullptr;

#if defined(__SWITCH__)
    if (!PreCommitForAllocation(USER_HEAP_BEGIN, USER_HEAP_SIZE, committedHeapPrefix, touchedHeapPrefix, fragmentSize))
    {
        LOGFN_ERROR("Switch user heap commit failed: size={}, fragment={}, frontier={}, committed={}", size, fragmentSize, touchedHeapPrefix, committedHeapPrefix);
        return nullptr;
    }
#endif

    void* ptr = o1heapAllocate(heap, size);

#if defined(__SWITCH__)
    if (ptr == nullptr)
        LOGFN_ERROR("Switch user heap allocation failed: size={}, frontier={}, committed={}", size, touchedHeapPrefix, committedHeapPrefix);
    else
        touchedHeapPrefix = std::max(touchedHeapPrefix, FragmentEndOffset(g_memory.Translate(USER_HEAP_BEGIN), ptr, fragmentSize));
#endif

    return ptr;
}

void* Heap::AllocPhysical(size_t size, size_t alignment)
{
    size = std::max<size_t>(1, size);
    alignment = alignment == 0 ? 0x1000 : std::max<size_t>(16, alignment);
    const size_t allocationSize = size + alignment;
#if defined(__SWITCH__)
    const size_t fragmentSize = RoundHeapFragmentSize(allocationSize); // Depends on the size alone: outside the lock.
#endif

    std::lock_guard lock(physicalMutex);
    if (physicalHeap == nullptr)
        return nullptr;

#if defined(__SWITCH__)
    if (!PreCommitForAllocation(RESERVED_END, PHYSICAL_HEAP_SIZE, committedPhysicalHeapPrefix, touchedPhysicalHeapPrefix, fragmentSize))
    {
        LOGFN_ERROR("Switch physical heap commit failed: size={}, fragment={}, frontier={}, committed={}", size, fragmentSize, touchedPhysicalHeapPrefix, committedPhysicalHeapPrefix);
        return nullptr;
    }
#endif

    void* ptr = o1heapAllocate(physicalHeap, allocationSize);
    if (ptr == nullptr)
    {
#if defined(__SWITCH__)
        LOGFN_ERROR("Switch physical heap allocation failed: size={}, frontier={}, committed={}", size, touchedPhysicalHeapPrefix, committedPhysicalHeapPrefix);
#endif
        return nullptr;
    }

#if defined(__SWITCH__)
    touchedPhysicalHeapPrefix = std::max(touchedPhysicalHeapPrefix, FragmentEndOffset(g_memory.Translate(RESERVED_END), ptr, fragmentSize));
#endif

    size_t aligned = ((size_t)ptr + alignment) & ~(alignment - 1);

    *((void**)aligned - 1) = ptr;
    *((size_t*)aligned - 2) = size + O1HEAP_ALIGNMENT;

    return (void*)aligned;
}

void Heap::Free(void* ptr)
{
    if (ptr == nullptr)
        return;

    if (physicalHeap != nullptr && reinterpret_cast<uintptr_t>(ptr) >= reinterpret_cast<uintptr_t>(physicalHeap))
    {
        std::lock_guard lock(physicalMutex);
        o1heapFree(physicalHeap, *((void**)ptr - 1));
    }
    else
    {
        std::lock_guard lock(mutex);
        if (heap != nullptr)
            o1heapFree(heap, ptr);
    }
}

size_t Heap::Size(void* ptr)
{
    if (ptr)
        return *((size_t*)ptr - 2) - O1HEAP_ALIGNMENT; // relies on fragment header in o1heap.c

    return 0;
}

uint32_t RtlAllocateHeap(uint32_t heapHandle, uint32_t flags, uint32_t size)
{
    void* ptr = g_userHeap.Alloc(size);
    assert(ptr);
    if (ptr == nullptr)
        return 0;

    if ((flags & 0x8) != 0)
        memset(ptr, 0, size);

    return g_memory.MapVirtual(ptr);
}

uint32_t RtlReAllocateHeap(uint32_t heapHandle, uint32_t flags, uint32_t memoryPointer, uint32_t size)
{
    void* ptr = g_userHeap.Alloc(size);
    assert(ptr);
    if (ptr == nullptr)
        return 0;

    if ((flags & 0x8) != 0)
        memset(ptr, 0, size);

    if (memoryPointer != 0)
    {
        void* oldPtr = g_memory.Translate(memoryPointer);
        memcpy(ptr, oldPtr, std::min<size_t>(size, g_userHeap.Size(oldPtr)));
        g_userHeap.Free(oldPtr);
    }

    return g_memory.MapVirtual(ptr);
}

uint32_t RtlFreeHeap(uint32_t heapHandle, uint32_t flags, uint32_t memoryPointer)
{
    if (memoryPointer != NULL)
        g_userHeap.Free(g_memory.Translate(memoryPointer));

    return true;
}

uint32_t RtlSizeHeap(uint32_t heapHandle, uint32_t flags, uint32_t memoryPointer)
{
    if (memoryPointer != NULL)
        return (uint32_t)g_userHeap.Size(g_memory.Translate(memoryPointer));

    return 0;
}

uint32_t XAllocMem(uint32_t size, uint32_t flags)
{
    void* ptr = (flags & 0x80000000) != 0 ?
        g_userHeap.AllocPhysical(size, (1ull << ((flags >> 24) & 0xF))) :
        g_userHeap.Alloc(size);

    assert(ptr);
    if (ptr == nullptr)
        return 0;

    if ((flags & 0x40000000) != 0)
        memset(ptr, 0, size);

    return g_memory.MapVirtual(ptr);
}

void XFreeMem(uint32_t baseAddress, uint32_t flags)
{
    if (baseAddress != NULL)
        g_userHeap.Free(g_memory.Translate(baseAddress));
}

uint32_t XVirtualAlloc(void *lpAddress, unsigned int dwSize, unsigned int flAllocationType, unsigned int flProtect)
{
    assert(!lpAddress);
    return g_memory.MapVirtual(g_userHeap.Alloc(dwSize));
}

uint32_t XVirtualFree(uint32_t lpAddress, unsigned int dwSize, unsigned int dwFreeType)
{
    if ((dwFreeType & 0x8000) != 0 && dwSize)
        return FALSE;

    if (lpAddress)
        g_userHeap.Free(g_memory.Translate(lpAddress));

    return TRUE;
}

GUEST_FUNCTION_HOOK(sub_82915668, XVirtualAlloc);
GUEST_FUNCTION_HOOK(sub_829156B8, XVirtualFree);

GUEST_FUNCTION_STUB(sub_82535588); // HeapCreate // replaced
// GUEST_FUNCTION_STUB(sub_82BD9250); // HeapDestroy

GUEST_FUNCTION_HOOK(sub_82535B38, RtlAllocateHeap); // repalced
GUEST_FUNCTION_HOOK(sub_82536420, RtlFreeHeap); // replaced
GUEST_FUNCTION_HOOK(sub_82536708, RtlReAllocateHeap); // replaced
GUEST_FUNCTION_HOOK(sub_82534DD0, RtlSizeHeap); // replaced

GUEST_FUNCTION_HOOK(sub_82537E70, XAllocMem); // replaced
GUEST_FUNCTION_HOOK(sub_82537F08, XFreeMem); // replaced
