#include <cstdio>
#include <cerrno>
#include <stdafx.h>
#include "guest_thread.h"
#include <kernel/memory.h>
#include <kernel/heap.h>
#include <kernel/function.h>
#include "ppc_context.h"
#if defined(__SWITCH__)
#include <os/logger.h>
#include <os/switch_cpu_profiler.h>
// os/switch/runtime_switch.cpp — see comment there for the priority scheme.
extern "C" void SwitchSetCurrentThreadPriority(int priority);
static constexpr int SWITCH_GUEST_THREAD_PRIORITY = 0x3B; // HOS preemptive slot

// Minimal libnx declarations (avoids pulling <switch.h> macros into this TU).
extern "C"
{
    uint32_t svcSetThreadCoreMask(uint32_t handle, int32_t preferredCore, uint32_t affinityMask);
    uint32_t svcGetInfo(uint64_t* out, uint32_t id0, uint32_t handle, uint64_t id1);
    uint32_t threadGetCurHandle(void);
}

// [Switch] SwitchThreadIdealCores (off unless the configuration turns it on; set by SwitchPerfInitKernel before any
// guest code runs). libnx's pthread_create already allows every thread on all of the process's cores, with no
// preferred core; this only picks the core a game thread starts on. It spreads the game's threads over the cores
// on purpose, so it is meant to be tried once the recompiled code's sync/lwsync are real fences.
bool g_threadIdealCores = false;

// [Switch] SwitchJobWorkersOffMainCore (perf6 A/B, on by default since perf7): the game's job worker threads (all
// started at sub_825866A8) prefer cores 1 and 2, in turn, instead of the default core 0, where the game thread runs.
// They stay allowed on every core. Only where the scheduler first places them changes.
bool g_jobWorkersOffMainCore = false;
static constexpr uint32_t JOB_WORKER_ENTRY = 0x825866A8;

// [Switch] SwitchSoundThreadOffMainCore: the game's sound thread (the CRI mixer, started at sub_8255B848) prefers core 2,
// where the host audio threads run (SwitchAudioThreadCores), instead of core 0. It stays allowed on every core.
bool g_soundThreadOffMainCore = false;
static constexpr uint32_t SOUND_THREAD_ENTRY = 0x8255B848;

// The thread starts on the given core and may still run on every core of the process.
static void ApplyIdealCore(uint32_t handle, int32_t core)
{
    static std::atomic<uint64_t> s_processCores = 0;
    uint64_t processCores = s_processCores.load(std::memory_order_relaxed);
    if (processCores == 0)
    {
        if (svcGetInfo(&processCores, 0 /* InfoType_CoreMask */, 0xFFFF8001 /* CUR_PROCESS_HANDLE */, 0) != 0 || processCores == 0)
            processCores = 0x7;

        s_processCores.store(processCores, std::memory_order_relaxed);
    }

    if ((processCores & (uint64_t(1) << core)) != 0)
        svcSetThreadCoreMask(handle, core, uint32_t(processCores));
}
#endif

constexpr size_t PCR_SIZE = 0xAB0;
constexpr size_t TLS_SIZE = 0x100;
constexpr size_t TEB_SIZE = 0x2E0;
constexpr size_t STACK_SIZE = 0x80000;
constexpr size_t TOTAL_SIZE = PCR_SIZE + TLS_SIZE + TEB_SIZE + STACK_SIZE;

constexpr size_t TEB_OFFSET = PCR_SIZE + TLS_SIZE;

GuestThreadContext::GuestThreadContext(uint32_t cpuNumber)
{
    assert(thread == nullptr);

    thread = (uint8_t*)g_userHeap.Alloc(TOTAL_SIZE);
    // printf("TOTAL_SIZE: %x %x %d\n", thread, TOTAL_SIZE, TOTAL_SIZE);
    memset(thread, 0, TOTAL_SIZE);

    *(uint32_t*)thread = ByteSwap(g_memory.MapVirtual(thread + PCR_SIZE)); // tls pointer
    *(uint32_t*)(thread + 0x100) = ByteSwap(g_memory.MapVirtual(thread + PCR_SIZE + TLS_SIZE)); // teb pointer
    *(thread + 0x10C) = cpuNumber;

    *(uint32_t*)(thread + PCR_SIZE + 0x10) = 0xFFFFFFFF; // that one TLS entry that felt quirky
    *(uint32_t*)(thread + PCR_SIZE + TLS_SIZE + 0x14C) = ByteSwap(GuestThread::GetCurrentThreadId()); // thread id

    ppcContext.r1.u64 = g_memory.MapVirtual(thread + PCR_SIZE + TLS_SIZE + TEB_SIZE + STACK_SIZE); // stack pointer
    ppcContext.r13.u64 = g_memory.MapVirtual(thread);
    ppcContext.fpscr.loadFromHost();

    assert(GetPPCContext() == nullptr);
    SetPPCContext(ppcContext);
}

GuestThreadContext::~GuestThreadContext()
{
    g_userHeap.Free(thread);
}

#ifdef USE_PTHREAD
static size_t GetStackSize()
{
#if defined(__SWITCH__)
    // HOST stack for the recompiled code's C++ frames only (the guest PPC stack
    // is allocated separately in guest memory). libnx maps every pthread stack
    // into the process's Stack region, which measured FAR smaller than assumed:
    // with 2 MB stacks creation still hit ENOMEM at ~23 and ~46 live threads
    // (~46-92 MB), so the region is on the order of 64 MB. 1 MB base + the
    // ENOMEM retry ladder in GuestThreadHandle keeps creation working under
    // pressure.
    return 1 * 1024 * 1024;
#else
    // Cache as this should not change.
    static size_t stackSize = 0;
    if (stackSize == 0)
    {
        // 8 MiB is a typical default.
        constexpr auto defaultSize = 8 * 1024 * 1024;
        struct rlimit lim;
        const auto ret = getrlimit(RLIMIT_STACK, &lim);
        if (ret == 0 && lim.rlim_cur < defaultSize)
        {
            // Use what the system allows.
            stackSize = lim.rlim_cur;
        }
        else
        {
            stackSize = defaultSize;
        }
    }
    return stackSize;
#endif
}

static void* GuestThreadFunc(void* arg)
{
    GuestThreadHandle* hThread = (GuestThreadHandle*)arg;
#else
static void* GuestThreadFunc(GuestThreadHandle* hThread)
{
#endif
#if defined(__SWITCH__)
    SwitchSetCurrentThreadPriority(SWITCH_GUEST_THREAD_PRIORITY);
    if (g_jobWorkersOffMainCore && hThread->params.function == JOB_WORKER_ENTRY)
    {
        static std::atomic<uint32_t> s_jobWorkers{ 0 };
        ApplyIdealCore(threadGetCurHandle(), int32_t(1 + s_jobWorkers.fetch_add(1, std::memory_order_relaxed) % 2));
    }
    if (g_soundThreadOffMainCore && hThread->params.function == SOUND_THREAD_ENTRY)
        ApplyIdealCore(threadGetCurHandle(), 2);
    if (g_threadIdealCores)
    {
        // Published before the pending core is read, and SetThreadIdealProcessor stores the core before it reads
        // the handle (both sequentially consistent): at least one of the two sees the other and places the thread.
        const uint32_t handle = threadGetCurHandle();
        hThread->kernelHandle.store(handle);
        const int32_t pendingCore = hThread->pendingIdealCore.load();
        if (pendingCore >= 0)
            ApplyIdealCore(handle, pendingCore);
    }
#endif
    hThread->WaitUntilResumed();
#if defined(__SWITCH__)
    // Named by the guest function the thread runs, which tells the game's threads apart.
    os::switch_cpu_profiler::RegisterCurrentThreadWithAddress("guest", hThread->params.function);
#endif
    GuestThread::Start(hThread->params);
#if defined(__SWITCH__)
    os::switch_cpu_profiler::UnregisterCurrentThread();
    hThread->kernelHandle.store(0);
#endif
    // HACK(1)
    hThread->isFinished = true;
    return nullptr;
}

GuestThreadHandle::GuestThreadHandle(const GuestThreadParams& params)
    : params(params), suspended((params.flags & 0x1) != 0)
#ifdef USE_PTHREAD
{
    static std::atomic<uint32_t> s_guestThreadsCreated{ 0 };

    size_t stackSize = GetStackSize();
    int ret = 0;
    for (;;)
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        const auto stackResult = pthread_attr_setstacksize(&attr, stackSize);
        if (stackResult != 0)
            fprintf(stderr, "pthread_attr_setstacksize failed with error code 0x%X.\n", stackResult);

        ret = pthread_create(&thread, &attr, GuestThreadFunc, this);
        pthread_attr_destroy(&attr);

        if (ret == 0)
            break;

#if defined(__SWITCH__)
        // The Stack region is a small fixed VA budget shared by every live
        // thread's stack; under load creation fails with ENOMEM. Retry with a
        // smaller stack instead of handing the game a dead thread handle — a
        // missing worker deadlocks the game's job system (and with it the
        // audio-callback thread), which presents as a whole-console freeze.
        if (ret == ENOMEM && stackSize > 256 * 1024)
        {
            stackSize /= 2;
            LOGFN_WARNING("guest pthread_create ENOMEM, retrying with stackSize=0x{:X} ({} threads created so far)",
                stackSize, s_guestThreadsCreated.load(std::memory_order_relaxed));
            continue;
        }
#endif
        break;
    }

    if (ret != 0) {
        fprintf(stderr, "pthread_create failed with error code 0x%X.\n", ret);
#if defined(__SWITCH__)
        LOGFN_ERROR("!!! guest pthread_create FAILED: rc=0x{:X} after {} guest threads (stackSize=0x{:X})",
            ret, s_guestThreadsCreated.load(std::memory_order_relaxed), stackSize);
#endif
        return;
    }

    s_guestThreadsCreated.fetch_add(1, std::memory_order_relaxed);
    threadCreated = true;
}
#else
, thread(GuestThreadFunc, this)
{
}
#endif

GuestThreadHandle::~GuestThreadHandle()
{
#ifdef USE_PTHREAD
    if (threadCreated && !joined.exchange(true))
        pthread_join(thread, nullptr);
#else
    if (thread.joinable())
        thread.join();
#endif
}

template <typename ThreadType>
static uint32_t CalcThreadId(const ThreadType& id)
{
    if constexpr (sizeof(id) == 4)
        return *reinterpret_cast<const uint32_t*>(&id);
    else
        return XXH32(&id, sizeof(id), 0);
}

uint32_t GuestThreadHandle::GetThreadId() const
{
#ifdef USE_PTHREAD
    return CalcThreadId(thread);
#else
    return CalcThreadId(thread.get_id());
#endif
}

void GuestThreadHandle::Suspend()
{
    suspended.store(true, std::memory_order_release);
}

void GuestThreadHandle::Resume()
{
    suspended.store(false, std::memory_order_release);
    suspendCv.notify_all();
}

void GuestThreadHandle::WaitUntilResumed()
{
    if (!suspended.load(std::memory_order_acquire))
        return;

    std::unique_lock lock(suspendMutex);
    suspendCv.wait(lock, [&]
    {
        return !suspended.load(std::memory_order_acquire);
    });
}

uint32_t GuestThreadHandle::Wait(uint32_t timeout)
{
    if (timeout == INFINITE || isFinished.load()) // HACK(1): isFinished
    {
#ifdef USE_PTHREAD
        if (threadCreated && !joined.exchange(true))
            pthread_join(thread, nullptr);
#else
        if (thread.joinable())
            thread.join();
#endif

        return STATUS_WAIT_0;
    }
    else if (timeout == 0)
    {
#ifndef USE_PTHREAD
        if (thread.joinable())
            return STATUS_TIMEOUT;
#endif

        return STATUS_WAIT_0;
    }
    else
    {
#ifdef USE_PTHREAD
        if (threadCreated && !joined.exchange(true))
            pthread_join(thread, nullptr);
#else
        auto start = std::chrono::steady_clock::now();
        while (thread.joinable())
        {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= timeout)
                return STATUS_TIMEOUT;

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#endif

        return STATUS_WAIT_0;
    }
}

uint32_t GuestThread::Start(const GuestThreadParams& params)
{
#if defined(__SWITCH__)
    // The calling thread becomes a guest thread (it may spin like one).
    SwitchSetCurrentThreadPriority(SWITCH_GUEST_THREAD_PRIORITY);
#endif
    const auto procMask = (uint8_t)(params.flags >> 24);
    const auto cpuNumber = procMask == 0 ? 0 : 7 - std::countl_zero(procMask);

    GuestThreadContext ctx(cpuNumber);
    ctx.ppcContext.r3.u64 = params.value;

    g_memory.FindFunction(params.function)(ctx.ppcContext, g_memory.base);

    return ctx.ppcContext.r3.u32;
}

GuestThreadHandle* GuestThread::Start(const GuestThreadParams& params, uint32_t* threadId)
{
    auto hThread = CreateKernelObject<GuestThreadHandle>(params);

    if (threadId != nullptr)
    {
        *threadId = hThread->GetThreadId();
    }

    return hThread;
}

uint32_t GuestThread::GetCurrentThreadId()
{
#ifdef USE_PTHREAD
    return CalcThreadId(pthread_self());
#else
    return CalcThreadId(std::this_thread::get_id());
#endif
}

void GuestThread::SetLastError(uint32_t error)
{
    auto* thread = (char*)g_memory.Translate(GetPPCContext()->r13.u32);
    if (*(uint32_t*)(thread + 0x150))
    {
        // Program doesn't want errors
        return;
    }

    // TEB + 0x160 : Win32LastError
    *(uint32_t*)(thread + TEB_OFFSET + 0x160) = ByteSwap(error);
}

#ifdef _WIN32
void GuestThread::SetThreadName(uint32_t threadId, const char* name)
{
#pragma pack(push,8)
    const DWORD MS_VC_EXCEPTION = 0x406D1388;

    typedef struct tagTHREADNAME_INFO
    {
        DWORD dwType; // Must be 0x1000.
        LPCSTR szName; // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags; // Reserved for future use, must be zero.
    } THREADNAME_INFO;
#pragma pack(pop)

    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = name;
    info.dwThreadID = threadId;
    info.dwFlags = 0;

    __try
    {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
#endif

void SetThreadNameImpl(uint32_t a1, uint32_t threadId, uint32_t* name)
{
#ifdef _WIN32
    GuestThread::SetThreadName(threadId, (const char*)g_memory.Translate(ByteSwap(*name)));
#endif
}

int GetThreadPriorityImpl(GuestThreadHandle* hThread)
{
#ifdef _WIN32
    return GetThreadPriority(hThread == GetKernelObject(CURRENT_THREAD_HANDLE) ? GetCurrentThread() : hThread->thread.native_handle());
#else 
    return 0;
#endif
}

uint32_t SetThreadIdealProcessorImpl(GuestThreadHandle* hThread, uint32_t dwIdealProcessor)
{
#if defined(__SWITCH__)
    // [Switch] SwitchThreadIdealCores. The Xbox 360 has three cores of two hardware threads each (0-5); the Switch
    // gives the game three cores. Only where the thread prefers to run changes: it stays allowed on every core, and
    // the guest still gets 0.
    if (g_threadIdealCores && dwIdealProcessor < 6)
    {
        const int32_t core = int32_t(dwIdealProcessor / 2);
        if (hThread == GetKernelObject(CURRENT_THREAD_HANDLE))
        {
            ApplyIdealCore(threadGetCurHandle(), core);
        }
        else if (hThread != nullptr)
        {
            hThread->pendingIdealCore.store(core);
            const uint32_t handle = hThread->kernelHandle.load();
            if (handle != 0)
                ApplyIdealCore(handle, core);
        }
    }
#endif
    return 0;
}

// GUEST_FUNCTION_HOOK(sub_82DFA2E8, SetThreadNameImpl);
// GUEST_FUNCTION_HOOK(sub_82BD57A8, GetThreadPriorityImpl);
GUEST_FUNCTION_HOOK(sub_82537F80, SetThreadIdealProcessorImpl);

// GUEST_FUNCTION_STUB(sub_82BD58F8); // Some function that updates the TEB, don't really care since the field is not set