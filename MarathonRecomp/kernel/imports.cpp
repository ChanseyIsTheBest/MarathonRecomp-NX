#include <cstdint>
#include <cstdio>
#include <stdafx.h>
#include <cpu/ppc_context.h>
#include <cpu/guest_thread.h>
#include <apu/audio.h>
#include "function.h"
#include "xex.h"
#include "xbox.h"
#include "heap.h"
#include "memory.h"
#include <memory>
#include "xam.h"
#include "xdm.h"
#include <user/config.h>
#include <os/logger.h>

#ifdef _WIN32
#include <ntstatus.h>
#endif

#include <condition_variable>

std::unordered_map<uint32_t, uint32_t> g_handleDuplicates{};

#if defined(__SWITCH__)
// libnx system calls, declared here: <switch.h> would bring libnx's Event and Semaphore types, which collide with the
// dispatcher objects below.
extern "C"
{
    uint32_t svcWaitForAddress(void* address, uint32_t arbType, int64_t value, int64_t timeoutNs);
    uint32_t svcSignalToAddress(void* address, uint32_t signalType, int32_t value, int32_t count);
    void svcSleepThread(int64_t nano);
    void* __aarch64_read_tp(void);
}

// [Switch] Guest-kernel switches ([Switch] keys of user/switch/config_kernel.inl), set once by SwitchPerfInitKernel
// (os/switch/perf/kernel_switch.cpp) after the configuration is loaded, before any guest code runs.

// SwitchFastEvents. A condition variable's notify is a system call on Horizon (svcSignalProcessWideKey) whether a
// thread waits on it or not, and setting an event or releasing a semaphore made two (the object's, then the
// dispatcher's for KeWaitForMultipleObjects). With this on, each wait counts its waiters under its mutex, and a
// notify happens only when there are some: a thread counted is inside the wait, or will check its condition under
// the mutex (after the change, which it then sees) before it waits.
bool g_fastEvents = false;
// SwitchSemaphoreWakeCount. Releasing N units of a semaphore woke every waiter (notify_all), and all but N of them
// found the count at 0 again and went back to sleep. With this on, a release of fewer units than there are
// waiters wakes N of them, one notify each (see Semaphore::Release).
bool g_semaphoreWakeCount = false;
// SwitchTargetedDispatcherWakeups (UnleashedRecomp-NX round 14). Every event set and semaphore release advanced the
// dispatcher generation under its global mutex and woke every KeWaitForMultipleObjects caller, whatever it waited on:
// the audio pump waits there all the time, so every Set and Release of the game (thousands a frame, on the game
// thread too) took that mutex and made a wake-up system call, and the pump woke to find its own objects unchanged.
// Each event and semaphore now counts the KeWaitForMultipleObjects calls that wait on it (registered under its mutex
// before they look at it), and only a signal of an object with such waiters advances the generation. A waiter
// registered before the signal's critical section is notified; one registered after it sees the new state when it
// looks. Resets and waits never make a waiter's condition true, so nothing else needs the generation.
bool g_targetedDispatcherWakeups = false;
// SwitchEventSpin: a wait without timeout on an event that is not set watches it (reads only, ~2 us) before it takes
// the event's mutex and sleeps. The game thread waits for its job workers' events every frame (2.1 ms a frame in the
// perf5 profile), and a job that is about to finish then costs neither the sleep's system calls nor the wake-up. The
// wait itself is unchanged: it takes the mutex and checks the event as before.
bool g_eventSpin = false;
#else
static constexpr bool g_fastEvents = false;
static constexpr bool g_semaphoreWakeCount = false;
static constexpr bool g_targetedDispatcherWakeups = false;
static constexpr bool g_eventSpin = false;
#endif

// std::atomic wait/notify is not reliable on Horizon's newlib, so all of the
// dispatcher objects are built on mutex + condition variable instead.
static std::atomic<uint32_t> g_dispatcherGeneration;
static std::mutex g_dispatcherMutex;
static std::condition_variable g_dispatcherCv;
static uint32_t g_dispatcherWaiters; // Under g_dispatcherMutex.

static void NotifyDispatcherWaiters()
{
    // Advance the generation under the mutex so KeWaitForMultipleObjects waiters reliably observe it: with the
    // atomic alone, the increment and notify could land between a waiter's check of the generation (under the
    // mutex) and its sleep, and the waiter (whose wait has no timeout) never woke.
    bool notify;
    {
        std::lock_guard lock(g_dispatcherMutex);
        g_dispatcherGeneration.fetch_add(1, std::memory_order_release);
        notify = !g_fastEvents || g_dispatcherWaiters != 0;
    }

    if (notify)
        g_dispatcherCv.notify_all();
}

static void WaitDispatcherGeneration(uint32_t generation)
{
    std::unique_lock lock(g_dispatcherMutex);
    g_dispatcherWaiters++;
    g_dispatcherCv.wait(lock, [&]
    {
        return g_dispatcherGeneration.load(std::memory_order_acquire) != generation;
    });
    g_dispatcherWaiters--;
}

struct Event final : KernelObject, HostObject<XKEVENT>
{
    bool manualReset;
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool signaled;
    uint32_t waiters = 0; // Under mutex (SwitchFastEvents).
    uint32_t dispatcherWaiters = 0; // Under mutex (SwitchTargetedDispatcherWakeups).
    std::atomic<bool> signaledHint; // SwitchEventSpin: `signaled`, written with it under the mutex, read without it.

    Event(XKEVENT* header)
        : manualReset(!header->Type), signaled(!!header->SignalState), signaledHint(!!header->SignalState)
    {
    }

    Event(bool manualReset, bool initialState)
        : manualReset(manualReset), signaled(initialState), signaledHint(initialState)
    {
    }

    void SetSignaled(bool value) // Under mutex.
    {
        signaled = value;
        signaledHint.store(value, std::memory_order_release);
    }

    uint32_t Wait(uint32_t timeout) override
    {
#if defined(__SWITCH__)
        if (g_eventSpin && timeout == INFINITE)
        {
            for (uint32_t i = 0; i < 512 && !signaledHint.load(std::memory_order_acquire); i++)
                __asm__ __volatile__("yield");
        }
#endif
        std::unique_lock lock(mutex);

        if (timeout == 0)
        {
            if (!signaled)
                return STATUS_TIMEOUT;

            if (!manualReset)
                SetSignaled(false);
        }
        else if (timeout == INFINITE)
        {
            waiters++;
            cv.wait(lock, [&] { return signaled; });
            waiters--;

            if (!manualReset)
                SetSignaled(false);
        }
        else
        {
            assert(false && "Unhandled timeout value.");
        }

        return STATUS_SUCCESS;
    }

    bool IsSignaled() const override
    {
        std::lock_guard lock(mutex);
        return signaled;
    }

    bool Set()
    {
        bool previousState;
        bool notify;
        bool notifyDispatcher;
        {
            std::lock_guard lock(mutex);
            previousState = signaled;
            SetSignaled(true);
            notify = !g_fastEvents || waiters != 0;
            notifyDispatcher = !g_targetedDispatcherWakeups || dispatcherWaiters != 0;
        }

        if (notify)
        {
            if (manualReset)
                cv.notify_all();
            else
                cv.notify_one();
        }

        if (notifyDispatcher)
            NotifyDispatcherWaiters();

        return previousState;
    }

    bool Reset()
    {
        std::lock_guard lock(mutex);
        const bool previousState = signaled;
        SetSignaled(false);
        return previousState;
    }
};

struct Semaphore final : KernelObject, HostObject<XKSEMAPHORE>
{
    mutable std::mutex mutex;
    std::condition_variable cv;
    uint32_t count;
    uint32_t maximumCount;
    uint32_t waiters = 0; // Under mutex (SwitchFastEvents, SwitchSemaphoreWakeCount).
    uint32_t dispatcherWaiters = 0; // Under mutex (SwitchTargetedDispatcherWakeups).

    Semaphore(XKSEMAPHORE* semaphore)
        : count(semaphore->Header.SignalState), maximumCount(semaphore->Limit)
    {
    }

    Semaphore(uint32_t count, uint32_t maximumCount)
        : count(count), maximumCount(maximumCount)
    {
    }

    uint32_t Wait(uint32_t timeout) override
    {
        std::unique_lock lock(mutex);

        if (timeout == 0)
        {
            if (count != 0)
            {
                --count;
                return STATUS_SUCCESS;
            }

            return STATUS_TIMEOUT;
        }
        else if (timeout == INFINITE)
        {
            waiters++;
            cv.wait(lock, [&] { return count != 0; });
            waiters--;
            --count;

            return STATUS_SUCCESS;
        }
        else
        {
            assert(false && "Unhandled timeout value.");
            return STATUS_TIMEOUT;
        }
    }

    bool IsSignaled() const override
    {
        std::lock_guard lock(mutex);
        return count != 0;
    }

    bool IsSignaledWithinLimit(uint32_t releaseCount) const
    {
        std::lock_guard lock(mutex);
        return count + releaseCount <= maximumCount;
    }

    void Release(uint32_t releaseCount, uint32_t* previousCount)
    {
        uint32_t waitersNow;
        bool notifyDispatcher;
        {
            std::lock_guard lock(mutex);
            assert(releaseCount <= maximumCount - count);

            if (previousCount != nullptr)
                *previousCount = count;

            count += releaseCount;
            waitersNow = waiters;
            notifyDispatcher = !g_targetedDispatcherWakeups || dispatcherWaiters != 0;
        }

        if (!g_fastEvents || waitersNow != 0)
        {
            // SwitchSemaphoreWakeCount: each unit wakes one waiter. A counted waiter is asleep in the wait or will
            // check the count under the mutex before it sleeps, and a woken one takes its unit under the mutex; a
            // notify that finds nobody asleep is not needed (everyone counted is about to look), and the waiters
            // left asleep would have found the count at 0 after the others. Which waiters win was always up to
            // the scheduler.
            if (g_semaphoreWakeCount && releaseCount < waitersNow)
            {
                for (uint32_t i = 0; i < releaseCount; i++)
                    cv.notify_one();
            }
            else
            {
                cv.notify_all();
            }
        }

        if (notifyDispatcher)
            NotifyDispatcherWaiters();
    }
};

inline void CloseKernelObject(XDISPATCHER_HEADER& header)
{
    if (header.WaitListHead.Flink != OBJECT_SIGNATURE)
    {
        return;
    }

    DestroyKernelObject(header.WaitListHead.Blink);
}

uint32_t GuestTimeoutToMilliseconds(be<int64_t>* timeout)
{
    return timeout ? (*timeout * -1) / 10000 : INFINITE;
}

static bool TryWriteGuestU32(uint32_t guestAddress, uint32_t value)
{
    if (guestAddress < 0x1000 || (guestAddress & 3) != 0 || guestAddress > PPC_MEMORY_SIZE - sizeof(be<uint32_t>))
        return false;

#if defined(__SWITCH__)
    if (!g_memory.CommitRange(guestAddress, sizeof(be<uint32_t>)))
        return false;
#endif

    *reinterpret_cast<be<uint32_t>*>(g_memory.Translate(guestAddress)) = value;
    return true;
}

void VdHSIOCalibrationLock()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeCertMonitorData()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XexExecutableModuleHandle()
{
    LOG_UTILITY("!!! STUB !!!");
}

void ExLoadedCommandLine()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeDebugMonitorData()
{
    LOG_UTILITY("!!! STUB !!!");
}

void ExThreadObjectType()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeTimeStampBundle()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XboxHardwareInfo()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XGetVideoMode()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t XGetGameRegion()
{
    if (Config::Language == ELanguage::Japanese)
        return 0x0101;

    return 0x03FF;
}

uint32_t XMsgStartIORequest(uint32_t App, uint32_t Message, XXOVERLAPPED* lpOverlapped, void* Buffer, uint32_t szBuffer)
{
    return STATUS_SUCCESS;
}

uint32_t XamUserGetSigninState(uint32_t userIndex)
{
    return true;
}

uint32_t XamGetSystemVersion()
{
    return 0;
}

void XamContentDelete()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t XamContentGetCreator(uint32_t userIndex, const XCONTENT_DATA* contentData, be<uint32_t>* isCreator, be<uint64_t>* xuid, XXOVERLAPPED* overlapped)
{
    if (isCreator)
        *isCreator = true;

    if (xuid)
        *xuid = 0xB13EBABEBABEBABE;

    return 0;
}

uint32_t XamContentGetDeviceState()
{
    return 0;
}

uint32_t XamUserGetSigninInfo(uint32_t userIndex, uint32_t flags, XUSER_SIGNIN_INFO* info)
{
    if (userIndex == 0)
    {
        memset(info, 0, sizeof(*info));
        info->xuid = 0xB13EBABEBABEBABE;
        info->SigninState = 1;
        strcpy(info->Name, "SWA");
        return 0;
    }

    return 0x00000525; // ERROR_NO_SUCH_USER
}

void XamShowSigninUI()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t XamShowDeviceSelectorUI
(
    uint32_t userIndex,
    uint32_t contentType,
    uint32_t contentFlags,
    uint64_t totalRequested,
    be<uint32_t>* deviceId,
    XXOVERLAPPED* overlapped
)
{
    XamNotifyEnqueueEvent(9, true);
    *deviceId = 1;
    XamNotifyEnqueueEvent(9, false);
    return 0;
}

void XamShowDirtyDiscErrorUI()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XamEnableInactivityProcessing()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XamResetInactivity()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XamShowMessageBoxUIEx()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t XGetLanguage()
{
    return (uint32_t)Config::Language.Value;
}

uint32_t XGetAVPack()
{
    return 0;
}

void XamLoaderTerminateTitle()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XamGetExecutionId()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XamLoaderLaunchTitle()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NtOpenFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlInitAnsiString(XANSI_STRING* destination, char* source)
{
    const uint16_t length = source ? (uint16_t)strlen(source) : 0;
    destination->Length = length;
    destination->MaximumLength = length + 1;
    destination->Buffer = source;
}

uint32_t NtCreateFile
(
    be<uint32_t>* FileHandle,
    uint32_t DesiredAccess,
    XOBJECT_ATTRIBUTES* Attributes,
    XIO_STATUS_BLOCK* IoStatusBlock,
    uint64_t* AllocationSize,
    uint32_t FileAttributes,
    uint32_t ShareAccess,
    uint32_t CreateDisposition,
    uint32_t CreateOptions
)
{
    LOG_UTILITY("!!! STUB !!!");
    return 0;
}

uint32_t NtClose(uint32_t handle)
{
    if (handle == GUEST_INVALID_HANDLE_VALUE)
        return 0xFFFFFFFF;

    if (IsKernelObject(handle))
    {
        // If the handle was duplicated, just decrement the duplication count. Otherwise, delete the object.
        const auto& it = g_handleDuplicates.find(handle);
        if (it == g_handleDuplicates.end() || it->second == 0)
            DestroyKernelObject(handle);
        else if (--it->second == 0)
            g_handleDuplicates.erase(it);

        return 0;
    }
    else
    {
        assert(false && "Unrecognized kernel object.");
        return 0xFFFFFFFF;
    }
}

void NtSetInformationFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t FscSetCacheElementCount()
{
    return 0;
}

uint32_t FscGetCacheElementCount()
{
    return 0;
}

uint32_t XamLoaderGetLaunchDataSize()
{
    return 0;
}

uint32_t XamLoaderGetLaunchData()
{
    return 0;
}

uint32_t XamLoaderSetLaunchData()
{
    return 0;
}

uint32_t NtWaitForSingleObjectEx(uint32_t Handle, uint32_t WaitMode, uint32_t Alertable, be<int64_t>* Timeout)
{
    if (Handle == GUEST_INVALID_HANDLE_VALUE)
        return 0xFFFFFFFF;

    uint32_t timeout = GuestTimeoutToMilliseconds(Timeout);
    // assert(timeout == 0 || timeout == INFINITE);

    if (IsKernelObject(Handle))
    {
        return GetKernelObject(Handle)->Wait(timeout);
    }
    else
    {
        assert(false && "Unrecognized handle value.");
    }

    return STATUS_TIMEOUT;
}

void NtWriteFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

void vsprintf_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t ExGetXConfigSetting(uint16_t Category, uint16_t Setting, void* Buffer, uint16_t SizeOfBuffer, be<uint32_t>* RequiredSize)
{
    uint32_t data[4]{};

    switch (Category)
    {
        // XCONFIG_SECURED_CATEGORY
        case 0x0002:
        {
            switch (Setting)
            {
                // XCONFIG_SECURED_AV_REGION
                case 0x0002:
                    data[0] = ByteSwap(0x00001000); // USA/Canada
                    break;

                default:
                    return 1;
            }
        }

        case 0x0003:
        {
            switch (Setting)
            {
                case 0x0001: // XCONFIG_USER_TIME_ZONE_BIAS
                case 0x0002: // XCONFIG_USER_TIME_ZONE_STD_NAME
                case 0x0003: // XCONFIG_USER_TIME_ZONE_DLT_NAME
                case 0x0004: // XCONFIG_USER_TIME_ZONE_STD_DATE
                case 0x0005: // XCONFIG_USER_TIME_ZONE_DLT_DATE
                case 0x0006: // XCONFIG_USER_TIME_ZONE_STD_BIAS
                case 0x0007: // XCONFIG_USER_TIME_ZONE_DLT_BIAS
                    data[0] = 0;
                    break;

                // XCONFIG_USER_LANGUAGE
                case 0x0009:
                    data[0] = ByteSwap((uint32_t)Config::Language.Value);
                    break;

                // XCONFIG_USER_VIDEO_FLAGS
                case 0x000A:
                    data[0] = ByteSwap(0x00040000);
                    break;

                // XCONFIG_USER_RETAIL_FLAGS
                case 0x000C:
                    data[0] = ByteSwap(1);
                    break;

                // XCONFIG_USER_COUNTRY
                case 0x000E:
                    data[0] = ByteSwap(103);
                    break;

                default:
                    return 1;
            }
        }
    }

    *RequiredSize = 4;
    memcpy(Buffer, data, std::min((size_t)SizeOfBuffer, sizeof(data)));

    return 0;
}

void NtQueryVirtualMemory()
{
    LOG_UTILITY("!!! STUB !!!");
}

void MmQueryStatistics()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t NtCreateEvent(be<uint32_t>* handle, void* objAttributes, uint32_t eventType, uint32_t initialState)
{
    *handle = GetKernelHandle(CreateKernelObject<Event>(!eventType, !!initialState));
    return 0;
}

uint32_t XexCheckExecutablePrivilege()
{
    return 0;
}

void DbgPrint()
{
    LOG_UTILITY("!!! STUB !!!");
}

void __C_specific_handler_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t RtlNtStatusToDosError(uint32_t Status)
{
    // See https://github.com/wine-mirror/wine/blob/master/dlls/ntdll/error.c#L47-L64
    if (Status == 0 || (Status & 0x20000000) != 0)
        return Status;

    if ((Status & 0xF0000000) == 0xD0000000)
        Status &= ~0x10000000;

    const uint32_t hi = (Status >> 16) & 0xFFFF;
    if (hi == 0x8007 || hi == 0xC001 || hi == 0xC007)
        return Status & 0xFFFF;

    switch (Status)
    {
    case uint32_t(STATUS_NOT_IMPLEMENTED):
        return ERROR_CALL_NOT_IMPLEMENTED;
    case uint32_t(STATUS_SEMAPHORE_LIMIT_EXCEEDED):
        return ERROR_TOO_MANY_POSTS;
    default:
        LOGF_WARNING("Unimplemented NtStatus translation: {:#08x}", Status);
        return Status;
    }
}

void XexGetProcedureAddress()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XexGetModuleSection()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t RtlUnicodeToMultiByteN(char* MultiByteString, uint32_t MaxBytesInMultiByteString, be<uint32_t>* BytesInMultiByteString, const be<uint16_t>* UnicodeString, uint32_t BytesInUnicodeString)
{
    const auto reqSize = BytesInUnicodeString / sizeof(uint16_t);

    if (BytesInMultiByteString)
        *BytesInMultiByteString = reqSize;

    if (reqSize > MaxBytesInMultiByteString)
        return STATUS_FAIL_CHECK;

    for (size_t i = 0; i < reqSize; i++)
    {
        const auto c = UnicodeString[i].get();

        MultiByteString[i] = c < 256 ? c : '?';
    }

    return STATUS_SUCCESS;
}

uint32_t KeDelayExecutionThread(uint32_t WaitMode, bool Alertable, be<int64_t>* Timeout)
{
    // We don't do async file reads.
    if (Alertable)
        return STATUS_USER_APC;

    uint32_t timeout = GuestTimeoutToMilliseconds(Timeout);

#ifdef _WIN32
    Sleep(timeout);
#else
    if (timeout == 0)
        std::this_thread::yield();
    else
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
#endif

    return STATUS_SUCCESS;
}

void ExFreePool()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NtQueryInformationFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NtQueryVolumeInformationFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NtQueryDirectoryFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NtReadFileScatter()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NtReadFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t NtDuplicateObject(uint32_t SourceHandle, be<uint32_t>* TargetHandle, uint32_t Options)
{
    if (SourceHandle == GUEST_INVALID_HANDLE_VALUE)
        return 0xFFFFFFFF;

    if (IsKernelObject(SourceHandle))
    {
        // Increment handle duplicate count.
        const auto& it = g_handleDuplicates.find(SourceHandle);
        if (it != g_handleDuplicates.end())
            ++it->second;
        else
            g_handleDuplicates[SourceHandle] = 1;

        *TargetHandle = SourceHandle;
        return 0;
    }
    else
    {
        assert(false && "Unrecognized kernel object.");
        return 0xFFFFFFFF;
    }
}

void NtAllocateVirtualMemory()
{
    __builtin_trap();
    LOG_UTILITY("!!! STUB !!!");
}

void NtFreeVirtualMemory()
{
    LOG_UTILITY("!!! STUB !!!");
}

void ObDereferenceObject()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeSetBasePriorityThread(GuestThreadHandle* hThread, int priority)
{
#ifdef _WIN32
    if (priority == 16)
    {
        priority = 15;
    }
    else if (priority == -16)
    {
        priority = -15;
    }

    SetThreadPriority(hThread == GetKernelObject(CURRENT_THREAD_HANDLE) ? GetCurrentThread() : hThread->thread.native_handle(), priority);
#endif
}

uint32_t ObReferenceObjectByHandle(uint32_t handle, uint32_t objectType, be<uint32_t>* object)
{
    *object = handle;
    return 0;
}

void KeQueryBasePriorityThread()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t NtSuspendThread(GuestThreadHandle* hThread, uint32_t* suspendCount)
{
    assert(hThread != GetKernelObject(CURRENT_THREAD_HANDLE) && hThread->GetThreadId() == GuestThread::GetCurrentThreadId());

    hThread->Suspend();
    hThread->WaitUntilResumed();

    return S_OK;
}

uint32_t KeSetAffinityThread(uint32_t Thread, uint32_t Affinity, be<uint32_t>* lpPreviousAffinity)
{
    if (lpPreviousAffinity)
        *lpPreviousAffinity = 2;

    return 0;
}

#if defined(__SWITCH__)
// [Switch] SwitchSleepingGuestLocks. A guest thread that finds a critical section taken retried with
// std::this_thread::yield(), and a guest spin lock the same way. On Horizon a yield only lets threads of the same
// priority run on that core: a waiter above the owner's priority (the 0x2B audio pump, which runs the game's mixer,
// against the 0x3B game threads) spun, one system call per attempt, until the owner ran elsewhere.
// With this on, a critical section waiter sleeps on the owner word (svcWaitForAddress, woken by the final leave,
// with a 1 ms safety timeout that turns a lost wake-up into a retry), and a spin lock waiter sleeps 10 µs, which
// lets any thread run. The acquiring compare-and-swap, the owner and recursion values are the same.
bool g_sleepingGuestLocks = false;

// SwitchFastCriticalSections (only with SwitchSleepingGuestLocks). The final leave then wakes a waiter with a
// system call (svcSignalToAddress) whether anyone waits or not, and the game takes critical sections thousands of
// times a frame. With this on, a thread about to wait counts itself in the critical section's LockCount (a field
// the guest never reads; RtlInitializeCriticalSection* sets it to -1, so -1 means nobody waits) and the leave only
// makes the call when someone does. The waiter counts itself before it compares the owner (the kernel's
// wait-if-equal), the leaver clears the owner before it reads the count, with full barriers in between: a leaver
// that sees no waiter cleared the owner before the waiter compared it, and the waiter does not sleep. A critical
// section whose LockCount did not start at -1 just keeps getting the call; the 1 ms timeout bounds any miss.
bool g_fastCriticalSections = false;

// SwitchLeanCriticalSectionLeave (with SwitchFastCriticalSections; UnleashedRecomp-NX round 12): the final leave
// clears the owner with a sequentially consistent store and then reads the waiter count with a sequentially consistent
// load. Those two are already ordered (the single total order of seq_cst operations; on AArch64 an STLR and a later
// LDAR are never reordered), so the full fence between them, a DMB on every final leave, adds nothing. The waiter's
// side keeps its fence: the kernel's compare is not a C++ load.
bool g_leanCriticalSectionLeave = false;

// SwitchGuestSpinBeforeSleep. A spin lock's owner that runs on another core usually holds it for a short while, so
// the waiter watches the lock word (reads only) for up to ~2 µs before its yield or sleep, and tries again as soon
// as it clears. The acquisition is still the same compare-and-swap.
bool g_guestSpinBeforeSleep = false;

// SwitchCriticalSectionSpin (UnleashedRecomp-NX round 14; with SwitchSleepingGuestLocks): a critical section held by
// another thread is watched (reads only) for ~2 us before the kernel wait, as the guest spin locks do: most are held
// for less than that (the profile had the game thread waiting in the kernel for the sound thread's critical sections,
// each wait a system call, and each release then another one to wake it). The acquisition is the same compare-and-swap.
bool g_criticalSectionSpin = false;
static bool SpinUntilFree(std::atomic_ref<uint32_t>& lock);

// 32-bit wait on a guest word through Horizon's address arbiter: returns when the word no longer holds `undesired`,
// when another thread signals the address, or after the safety timeout.
static void SwitchAtomicWait32(void* address, uint32_t undesired)
{
    constexpr uint32_t ARBITRATION_WAIT_IF_EQUAL = 2;
    constexpr int64_t SAFETY_TIMEOUT_NS = 1'000'000; // 1 ms
    constexpr uint32_t RESULT_TIMED_OUT = 0xEA01;
    constexpr uint32_t RESULT_INVALID_STATE = 0xFA01; // The word did not hold `undesired`.

    const uint32_t result = svcWaitForAddress(address, ARBITRATION_WAIT_IF_EQUAL,
        static_cast<int64_t>(static_cast<int32_t>(undesired)), SAFETY_TIMEOUT_NS);

    // Any other failure returns at once: yield as before rather than retry in a tight loop.
    if (result != 0 && result != RESULT_TIMED_OUT && result != RESULT_INVALID_STATE)
        std::this_thread::yield();
}

static void SwitchAtomicNotifyOne32(void* address)
{
    constexpr uint32_t SIGNAL_TYPE_SIGNAL = 0;
    svcSignalToAddress(address, SIGNAL_TYPE_SIGNAL, 0, 1);
}
#endif

void RtlLeaveCriticalSection(XRTL_CRITICAL_SECTION* cs)
{
    // printf("RtlLeaveCriticalSection");
    cs->RecursionCount = cs->RecursionCount.get() - 1;

    if (cs->RecursionCount.get() != 0)
        return;

    std::atomic_ref owningThread(cs->OwningThread);
    owningThread.store(0);
#if defined(__SWITCH__)
    if (g_sleepingGuestLocks)
    {
        if (g_fastCriticalSections)
        {
            if (!g_leanCriticalSectionLeave)
                std::atomic_thread_fence(std::memory_order_seq_cst);
            if (std::atomic_ref(cs->LockCount).load(std::memory_order_seq_cst) == -1)
                return;
        }

        SwitchAtomicNotifyOne32(&cs->OwningThread);
    }
#else
    owningThread.notify_one();
#endif
}

#if defined(__SWITCH__)
// [Switch] The calling thread's id (its r13) from the caller's own context (the hooks at the end of this file)
// instead of g_ppcContext, a thread-local variable whose every read is a call (-mtp=soft); the game enters critical
// sections thousands of times a frame. The same value: g_ppcContext is that context (GuestThreadContext and
// GuestToHostFunction set it to the context they run the guest code on, and r13 is copied into nested contexts).
static void RtlEnterCriticalSectionAs(XRTL_CRITICAL_SECTION* cs, uint32_t thisThread);

void RtlEnterCriticalSection(XRTL_CRITICAL_SECTION* cs)
{
    RtlEnterCriticalSectionAs(cs, g_ppcContext->r13.u32);
}

static void RtlEnterCriticalSectionAs(XRTL_CRITICAL_SECTION* cs, uint32_t thisThread)
{
#else
void RtlEnterCriticalSection(XRTL_CRITICAL_SECTION* cs)
{
    uint32_t thisThread = g_ppcContext->r13.u32;
#endif
    // printf("RtlEnterCriticalSection %x %x %x %x\n", thisThread, cs->OwningThread, cs->LockCount, cs->RecursionCount);
    assert(thisThread != NULL);

    std::atomic_ref owningThread(cs->OwningThread);

    while (true) 
    {
        uint32_t previousOwner = 0;

        if (owningThread.compare_exchange_weak(previousOwner, thisThread) || previousOwner == thisThread)
        {
            cs->RecursionCount = cs->RecursionCount.get() + 1;
            return;
        }

        // printf("wait start %x\n", cs);
#if defined(__SWITCH__)
        if (g_sleepingGuestLocks)
        {
            // A weak compare-and-swap may fail with the section free (previousOwner 0): try again at once rather
            // than sleep while the word holds 0.
            if (previousOwner == 0)
                continue;

            if (g_criticalSectionSpin && SpinUntilFree(owningThread))
                continue;

            if (g_fastCriticalSections)
            {
                std::atomic_ref waiters(cs->LockCount);
                waiters.fetch_add(1);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                SwitchAtomicWait32(&cs->OwningThread, previousOwner);
                waiters.fetch_sub(1);
                continue;
            }

            SwitchAtomicWait32(&cs->OwningThread, previousOwner);
            continue;
        }

        std::this_thread::yield();
#else
        owningThread.wait(previousOwner);
#endif
        // printf("wait end\n");
    }
}

void RtlImageXexHeaderField()
{
    LOG_UTILITY("!!! STUB !!!");
}

void HalReturnToFirmware()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlFillMemoryUlong()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeBugCheckEx()
{
    LOGN_ERROR("Guest KeBugCheckEx (fatal) called");
    __builtin_debugtrap();
}

uint32_t KeGetCurrentProcessType()
{
    return 1;
}

void RtlCompareMemoryUlong()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t RtlInitializeCriticalSection(XRTL_CRITICAL_SECTION* cs)
{
    // printf("RtlInitializeCriticalSection %x\n", cs);
    cs->Header.Absolute = 0;
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwningThread = 0;

    return 0;
}

void RtlRaiseException_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KfReleaseSpinLock(uint32_t* spinLock)
{
    std::atomic_ref spinLockRef(*spinLock);
    spinLockRef = 0;
}

#if defined(__SWITCH__)
// Waits (reading only) for the lock word to become 0, for a bounded number of iterations: true if it did.
static bool SpinUntilFree(std::atomic_ref<uint32_t>& lock)
{
    for (uint32_t i = 0; i < 512; i++)
    {
        if (lock.load(std::memory_order_relaxed) == 0)
            return true;

        __asm__ __volatile__("yield");
    }

    return false;
}

// KfAcquireSpinLock and KeAcquireSpinLockAtRaisedIrql. [Switch] The owner value (the caller's r13) is read once,
// from the caller's context (the hooks at the end of this file), instead of from g_ppcContext on every attempt: a
// thread-local read, which is a call with -mtp=soft. The same value, see RtlEnterCriticalSectionAs.
static void AcquireGuestSpinLock(uint32_t* spinLock, uint32_t thisThread)
{
    std::atomic_ref spinLockRef(*spinLock);

    while (true)
    {
        uint32_t expected = 0;
        if (spinLockRef.compare_exchange_weak(expected, thisThread))
            break;

        if (g_guestSpinBeforeSleep && SpinUntilFree(spinLockRef))
            continue;

        if (g_sleepingGuestLocks)
        {
            // A weak compare-and-swap may fail with the lock free: try again at once.
            if (expected == 0)
                continue;

            // yield() cannot run a lower-priority owner queued on this core; a real (bounded) sleep can.
            svcSleepThread(10000);
            continue;
        }

        std::this_thread::yield();
    }
}

void KfAcquireSpinLock(uint32_t* spinLock)
{
    AcquireGuestSpinLock(spinLock, g_ppcContext->r13.u32);
}
#else
void KfAcquireSpinLock(uint32_t* spinLock)
{
    std::atomic_ref spinLockRef(*spinLock);

    while (true)
    {
        uint32_t expected = 0;
        if (spinLockRef.compare_exchange_weak(expected, g_ppcContext->r13.u32))
            break;

        std::this_thread::yield();
    }
}
#endif

uint64_t KeQueryPerformanceFrequency()
{
    return 49875000;
}

void MmFreePhysicalMemory(uint32_t type, uint32_t guestAddress)
{
    if (guestAddress != NULL)
        g_userHeap.Free(g_memory.Translate(guestAddress));
}

bool VdPersistDisplay(uint32_t a1, uint32_t* a2)
{
    *a2 = NULL;
    return false;
}

void VdSwap()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdGetSystemCommandBuffer()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeReleaseSpinLockFromRaisedIrql(uint32_t* spinLock)
{
    std::atomic_ref spinLockRef(*spinLock);
    spinLockRef = 0;
}

#if defined(__SWITCH__)
void KeAcquireSpinLockAtRaisedIrql(uint32_t* spinLock)
{
    AcquireGuestSpinLock(spinLock, g_ppcContext->r13.u32);
}
#else
void KeAcquireSpinLockAtRaisedIrql(uint32_t* spinLock)
{
    std::atomic_ref spinLockRef(*spinLock);

    while (true)
    {
        uint32_t expected = 0;
        if (spinLockRef.compare_exchange_weak(expected, g_ppcContext->r13.u32))
            break;

        std::this_thread::yield();
    }
}
#endif

uint32_t KiApcNormalRoutineNop()
{
    return 0;
}

void VdEnableRingBufferRPtrWriteBack()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdInitializeRingBuffer()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t MmGetPhysicalAddress(uint32_t address)
{
    LOGF_UTILITY("0x{:x}", address);
    return address;
}

void VdSetSystemCommandBufferGpuIdentifierAddress()
{
    LOG_UTILITY("!!! STUB !!!");
}

void _vsnprintf_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void sprintf_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

int32_t ExRegisterTitleTerminateNotification(uint32_t* reg, uint32_t create)
{
    LOG_UTILITY("!!! STUB !!!");
    return 0;
}

void VdShutdownEngines()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdQueryVideoMode(XVIDEO_MODE* vm)
{
    memset(vm, 0, sizeof(XVIDEO_MODE));
    vm->DisplayWidth = 1280;
    vm->DisplayHeight = 720;
    vm->IsInterlaced = false;
    vm->IsWidescreen = true;
    vm->IsHighDefinition = true;
    vm->RefreshRate = 0x42700000;
    vm->VideoStandard = 1;
    vm->Unknown4A = 0x4A;
    vm->Unknown01 = 0x01;
}

void VdGetCurrentDisplayInformation()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdSetDisplayMode()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdSetGraphicsInterruptCallback()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t VdInitializeEngines()
{
    LOG_UTILITY("!!! STUB !!!");
    return 1;
}

void VdIsHSIOTrainingSucceeded()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdGetCurrentDisplayGamma()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdQueryVideoFlags()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdCallGraphicsNotificationRoutines()
{
    LOG_UTILITY("!!! STUB !!!");
}

void VdInitializeScalerCommandBuffer()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeLeaveCriticalRegion()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t VdRetrainEDRAM()
{
    return 0;
}

void VdRetrainEDRAMWorker()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeEnterCriticalRegion()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t MmAllocatePhysicalMemoryEx
(
    uint32_t flags,
    uint32_t size,
    uint32_t protect,
    uint32_t minAddress,
    uint32_t maxAddress,
    uint32_t alignment
)
{
    LOGF_UTILITY("0x{:x}, 0x{:x}, 0x{:x}, 0x{:x}, 0x{:x}, 0x{:x}", flags, size, protect, minAddress, maxAddress, alignment);
    void* ptr = g_userHeap.AllocPhysical(size, alignment);
    if (ptr == nullptr)
        LOGF_ERROR("MmAllocatePhysicalMemoryEx FAILED (out of physical heap) size=0x{:x}", size);
    return g_memory.MapVirtual(ptr);
}

void ObDeleteSymbolicLink()
{
    LOG_UTILITY("!!! STUB !!!");
}

void ObCreateSymbolicLink()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t MmQueryAddressProtect(uint32_t guestAddress)
{
    return PAGE_READWRITE;
}

void VdEnableDisableClockGating()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeBugCheck()
{
    LOGN_ERROR("Guest KeBugCheck (fatal) called");
    __builtin_debugtrap();
}

void KeLockL2()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeUnlockL2()
{
    LOG_UTILITY("!!! STUB !!!");
}

bool KeSetEvent(XKEVENT* pEvent, uint32_t Increment, bool Wait)
{
    return QueryKernelObject<Event>(*pEvent)->Set();
}

bool KeResetEvent(XKEVENT* pEvent)
{
    return QueryKernelObject<Event>(*pEvent)->Reset();
}

uint32_t KeWaitForSingleObject(XDISPATCHER_HEADER* Object, uint32_t WaitReason, uint32_t WaitMode, bool Alertable, be<int64_t>* Timeout)
{
    const uint32_t timeout = GuestTimeoutToMilliseconds(Timeout);
    assert(timeout == 0 || timeout == INFINITE);

    switch (Object->Type)
    {
        case 0:
        case 1:
            return QueryKernelObject<Event>(*Object)->Wait(timeout);

        case 5:
            return QueryKernelObject<Semaphore>(*Object)->Wait(timeout);

        default:
            assert(false && "Unrecognized kernel object type.");
            return STATUS_TIMEOUT;
    }
}

static std::vector<size_t> g_tlsFreeIndices;
static size_t g_tlsNextIndex = 0;
static RecompMutex g_tlsAllocationMutex;

#if defined(__SWITCH__)
// [Switch] SwitchFastGuestTls. KeTlsGetValue (1,967 call sites) and KeTlsSetValue went through a thread_local
// std::vector: two thread-pointer calls (-mtp=soft: __aarch64_read_tp) per access, for its guard and its address.
// The values stay exactly where they are, in that vector of the calling host thread; the hooks at the end of this
// file only find it without a call: KeTlsGetValueRef leaves its address in t_guestTlsValues (a plain pointer, no
// guard), which sits at a fixed distance from the thread pointer on every thread (local-exec TLS of the one
// module), and the thread pointer is what __aarch64_read_tp reads (libnx keeps it at TLS + 0x1F8). A thread that
// has no vector yet, or asks for an index past its end, takes the old path.
bool g_fastGuestTls = false;
static ptrdiff_t g_guestTlsOffset; // &t_guestTlsValues - thread pointer, the same on every thread.
static thread_local std::vector<uint32_t>* t_guestTlsValues;

static inline uint8_t* SwitchThreadPointer()
{
    uint8_t* tls;
    __asm__("mrs %x0, tpidrro_el0" : "=r"(tls));
    return *reinterpret_cast<uint8_t* const*>(tls + 0x1F8);
}

// Called once by SwitchPerfInitKernel on the main thread; false (and the fast path stays off) if the thread
// pointer is not where __aarch64_read_tp finds it.
bool SwitchInitFastGuestTls()
{
    uint8_t* threadPointer = SwitchThreadPointer();
    if (threadPointer != static_cast<uint8_t*>(__aarch64_read_tp()))
        return false;

    g_guestTlsOffset = reinterpret_cast<uint8_t*>(&t_guestTlsValues) - threadPointer;
    return true;
}

static inline std::vector<uint32_t>* FastGuestTlsValues()
{
    return *reinterpret_cast<std::vector<uint32_t>* const*>(SwitchThreadPointer() + g_guestTlsOffset);
}
#endif

static uint32_t& KeTlsGetValueRef(size_t index)
{
    // Having this a global thread_local variable
    // for some reason crashes on boot in debug builds.
    thread_local std::vector<uint32_t> s_tlsValues;

    if (s_tlsValues.size() <= index)
    {
        s_tlsValues.resize(index + 1, 0);
    }

#if defined(__SWITCH__)
    if (g_fastGuestTls)
    {
        assert(reinterpret_cast<uint8_t*>(&t_guestTlsValues) == SwitchThreadPointer() + g_guestTlsOffset);
        t_guestTlsValues = &s_tlsValues;
    }
#endif

    return s_tlsValues[index];
}

uint32_t KeTlsGetValue(uint32_t dwTlsIndex)
{
    return KeTlsGetValueRef(dwTlsIndex);
}

uint32_t KeTlsSetValue(uint32_t dwTlsIndex, uint32_t lpTlsValue)
{
    KeTlsGetValueRef(dwTlsIndex) = lpTlsValue;
    return TRUE;
}

uint32_t KeTlsAlloc()
{
    std::lock_guard<RecompMutex> lock(g_tlsAllocationMutex);
    if (!g_tlsFreeIndices.empty())
    {
        size_t index = g_tlsFreeIndices.back();
        g_tlsFreeIndices.pop_back();
        return index;
    }

    return g_tlsNextIndex++;
}

uint32_t KeTlsFree(uint32_t dwTlsIndex)
{
    std::lock_guard<RecompMutex> lock(g_tlsAllocationMutex);
    g_tlsFreeIndices.push_back(dwTlsIndex);
    return TRUE;
}

uint32_t XMsgInProcessCall(uint32_t app, uint32_t message, be<uint32_t>* param1, be<uint32_t>* param2)
{
    if (message == 0x7001B)
    {
        uint32_t* ptr = (uint32_t*)g_memory.Translate(param1[1]);
        ptr[0] = 0;
        ptr[1] = 0;
    }

    return 0;
}

void XamUserReadProfileSettings
(
    uint32_t titleId,
    uint32_t userIndex,
    uint32_t xuidCount,
    uint64_t* xuids,
    uint32_t settingCount,
    uint32_t* settingIds,
    be<uint32_t>* bufferSize,
    void* buffer,
    void* overlapped
)
{
    if (buffer != nullptr)
    {
        memset(buffer, 0, *bufferSize);
    }
    else
    {
        *bufferSize = 4;
    }
}

void NetDll_WSAStartup()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_WSACleanup()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_socket()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_closesocket()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_setsockopt()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_bind()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_connect()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_listen()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_accept()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_select()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_recv()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_send()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_inet_addr()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll___WSAFDIsSet()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XMsgStartIORequestEx()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XexGetModuleHandle()
{
    LOG_UTILITY("!!! STUB !!!");
}

#if defined(__SWITCH__)
static bool RtlTryEnterCriticalSectionAs(XRTL_CRITICAL_SECTION* cs, uint32_t thisThread);

bool RtlTryEnterCriticalSection(XRTL_CRITICAL_SECTION* cs)
{
    return RtlTryEnterCriticalSectionAs(cs, g_ppcContext->r13.u32);
}

static bool RtlTryEnterCriticalSectionAs(XRTL_CRITICAL_SECTION* cs, uint32_t thisThread)
{
#else
bool RtlTryEnterCriticalSection(XRTL_CRITICAL_SECTION* cs)
{
    uint32_t thisThread = g_ppcContext->r13.u32;
#endif
    // printf("RtlTryEnterCriticalSection\n");
    assert(thisThread != NULL);

    std::atomic_ref owningThread(cs->OwningThread);

    uint32_t previousOwner = 0;

    if (owningThread.compare_exchange_weak(previousOwner, thisThread) || previousOwner == thisThread)
    {
        cs->RecursionCount = cs->RecursionCount.get() + 1;
        return true;
    }

    return false;
}

void RtlInitializeCriticalSectionAndSpinCount(XRTL_CRITICAL_SECTION* cs, uint32_t spinCount)
{
    // printf("RtlInitializeCriticalSectionAndSpinCount\n");
    cs->Header.Absolute = (spinCount + 255) >> 8;
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwningThread = 0;
}

void _vswprintf_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void _vscwprintf_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void _swprintf_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void _snwprintf_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XeCryptBnQwBeSigVerify()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XeKeysGetKey()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XeCryptRotSumSha()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XeCryptSha()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeEnableFpuExceptions()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlUnwind_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlCaptureContext_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NtQueryFullAttributesFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t RtlMultiByteToUnicodeN(be<uint16_t>* UnicodeString, uint32_t MaxBytesInUnicodeString, be<uint32_t>* BytesInUnicodeString, const char* MultiByteString, uint32_t BytesInMultiByteString)
{
    uint32_t length = std::min(MaxBytesInUnicodeString / 2, BytesInMultiByteString);

    for (size_t i = 0; i < length; i++)
        UnicodeString[i] = MultiByteString[i];

    if (BytesInUnicodeString != nullptr)
        *BytesInUnicodeString = length * 2;

    return STATUS_SUCCESS;
}

void DbgBreakPoint()
{
    LOG_UTILITY("!!! STUB !!!");
}

void MmQueryAllocationSize()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t NtClearEvent(Event* handle)
{
    handle->Reset();
    return 0;
}

uint32_t NtResumeThread(GuestThreadHandle* hThread, uint32_t* suspendCount)
{
    assert(hThread != GetKernelObject(CURRENT_THREAD_HANDLE));

    hThread->Resume();

    return S_OK;
}

uint32_t NtSetEvent(Event* handle, uint32_t previousState)
{
    const bool previous = handle->Set();
    TryWriteGuestU32(previousState, previous ? 1u : 0u);
    return 0;
}

uint32_t NtCreateSemaphore(be<uint32_t>* Handle, XOBJECT_ATTRIBUTES* ObjectAttributes, uint32_t InitialCount, uint32_t MaximumCount)
{
    *Handle = GetKernelHandle(CreateKernelObject<Semaphore>(InitialCount, MaximumCount));
    return STATUS_SUCCESS;
}

uint32_t NtReleaseSemaphore(Semaphore* Handle, uint32_t ReleaseCount, uint32_t PreviousCount)
{
    // the game releases semaphore with 1 maximum number of releases more than once
    if (!Handle->IsSignaledWithinLimit(ReleaseCount))
        return STATUS_SEMAPHORE_LIMIT_EXCEEDED;

    uint32_t previousCount;
    Handle->Release(ReleaseCount, &previousCount);
    TryWriteGuestU32(PreviousCount, previousCount);

    return STATUS_SUCCESS;
}

void NtWaitForMultipleObjectsEx()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlCompareStringN()
{
    LOG_UTILITY("!!! STUB !!!");
}

void _snprintf_x()
{
    LOG_UTILITY("!!! STUB !!!");
}

void StfsControlDevice()
{
    LOG_UTILITY("!!! STUB !!!");
}

void StfsCreateDevice()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NtFlushBuffersFile()
{
    LOG_UTILITY("!!! STUB !!!");
}

void KeQuerySystemTime(be<uint64_t>* time)
{
    constexpr int64_t FILETIME_EPOCH_DIFFERENCE = 116444736000000000LL;

    auto now = std::chrono::system_clock::now();
    auto timeSinceEpoch = now.time_since_epoch();

    int64_t currentTime100ns = std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(timeSinceEpoch).count();
    currentTime100ns += FILETIME_EPOCH_DIFFERENCE;

    *time = currentTime100ns;
}

struct TIME_FIELDS {
    be<uint16_t> Year;
    be<uint16_t> Month;
    be<uint16_t> Day;
    be<uint16_t> Hour;
    be<uint16_t> Minute;
    be<uint16_t> Second;
    be<uint16_t> Milliseconds;
    be<uint16_t> Weekday;
};

void RtlTimeToTimeFields(const be<uint64_t>* time, TIME_FIELDS* timeFields)
{
    constexpr uint64_t TICKS_PER_MILLISECOND = 10000;
    constexpr uint64_t TICKS_PER_SECOND = 10000000;
    constexpr uint64_t TICKS_PER_MINUTE = 600000000;
    constexpr uint64_t TICKS_PER_HOUR = 36000000000;
    constexpr uint64_t TICKS_PER_DAY = 864000000000;

    static const int DaysInMonth[2][12] = {
            {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}, // Non-leap
            {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}  // Leap
    };

    // Calculate total days since January 1, 1601
    uint64_t days = *time / TICKS_PER_DAY;
    uint64_t remainingTicks = *time % TICKS_PER_DAY;

    timeFields->Hour = static_cast<uint16_t>(remainingTicks / TICKS_PER_HOUR);
    remainingTicks %= TICKS_PER_HOUR;

    timeFields->Minute = static_cast<uint16_t>(remainingTicks / TICKS_PER_MINUTE);
    remainingTicks %= TICKS_PER_MINUTE;

    timeFields->Second = static_cast<uint16_t>(remainingTicks / TICKS_PER_SECOND);
    remainingTicks %= TICKS_PER_SECOND;

    timeFields->Milliseconds = static_cast<uint16_t>(remainingTicks / TICKS_PER_MILLISECOND);

    // Calculate day of week (January 1, 1601 was a Monday = 1)
    timeFields->Weekday = static_cast<uint16_t>((days + 1) % 7);

    // Calculate year
    uint32_t year = 1601;

    // Each 400-year cycle has 146097 days
    uint32_t cycles400 = static_cast<uint32_t>(days / 146097);
    days %= 146097;
    year += cycles400 * 400;

    // Handle 100-year cycles (24 leap years + 76 normal years = 36524 days)
    // Except the 4th century which has 36525 days
    uint32_t cycles100 = static_cast<uint32_t>(days / 36524);
    if (cycles100 == 4) cycles100 = 3; // Last day of 400-year cycle
    days -= cycles100 * 36524;
    year += cycles100 * 100;

    // Handle 4-year cycles (1 leap year + 3 normal years = 1461 days)
    uint32_t cycles4 = static_cast<uint32_t>(days / 1461);
    days %= 1461;
    year += cycles4 * 4;

    // Handle individual years within 4-year cycle
    uint32_t yearInCycle = static_cast<uint32_t>(days / 365);
    if (yearInCycle == 4) yearInCycle = 3; // Last day of leap cycle
    days -= yearInCycle * 365;
    if (yearInCycle > 0) {
        // Account for leap days in previous years of this cycle
        days -= (yearInCycle - 1) / 4;
    }
    year += yearInCycle;

    timeFields->Year = static_cast<uint16_t>(year);

    // Determine if current year is a leap year
    bool isLeapYear = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);

    // Calculate month and day
    const int* monthDays = DaysInMonth[isLeapYear ? 1 : 0];
    uint32_t dayOfYear = static_cast<uint32_t>(days) + 1; // Convert to 1-based

    uint16_t month = 1;
    while (dayOfYear > static_cast<uint32_t>(monthDays[month - 1])) {
        dayOfYear -= monthDays[month - 1];
        month++;
    }

    timeFields->Month = month;
    timeFields->Day = static_cast<uint16_t>(dayOfYear);
}

void RtlFreeAnsiString()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlUnicodeStringToAnsiString()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlInitUnicodeString()
{
    LOG_UTILITY("!!! STUB !!!");
}

void ExTerminateThread()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t ExCreateThread(be<uint32_t>* handle, uint32_t stackSize, be<uint32_t>* threadId, uint32_t xApiThreadStartup, uint32_t startAddress, uint32_t startContext, uint32_t creationFlags)
{

    uint32_t hostThreadId;

    *handle = GetKernelHandle(GuestThread::Start({ startAddress, startContext, creationFlags }, &hostThreadId));
    LOGF_UTILITY("0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}, 0x{:X} {:X}",
        (intptr_t)handle, stackSize, (intptr_t)threadId, xApiThreadStartup, startAddress, startContext, creationFlags, hostThreadId);
    if (threadId != nullptr)
        *threadId = hostThreadId;

    return 0;
}

void IoInvalidDeviceRequest()
{
    LOG_UTILITY("!!! STUB !!!");
}

void ObReferenceObject()
{
    LOG_UTILITY("!!! STUB !!!");
}

void IoCreateDevice()
{
    LOG_UTILITY("!!! STUB !!!");
}

void IoDeleteDevice()
{
    LOG_UTILITY("!!! STUB !!!");
}

void ExAllocatePoolTypeWithTag()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlTimeFieldsToTime()
{
    LOG_UTILITY("!!! STUB !!!");
}

void IoCompleteRequest()
{
    LOG_UTILITY("!!! STUB !!!");
}

void RtlUpcaseUnicodeChar()
{
    LOG_UTILITY("!!! STUB !!!");
}

void ObIsTitleObject()
{
    LOG_UTILITY("!!! STUB !!!");
}

void IoCheckShareAccess()
{
    LOG_UTILITY("!!! STUB !!!");
}

void IoSetShareAccess()
{
    LOG_UTILITY("!!! STUB !!!");
}

void IoRemoveShareAccess()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_XNetStartup()
{
    LOG_UTILITY("!!! STUB !!!");
}

void NetDll_XNetGetTitleXnAddr()
{
    LOG_UTILITY("!!! STUB !!!");
}

// [Switch] SwitchTargetedDispatcherWakeups: a KeWaitForMultipleObjects call that may wait counts itself on each of its
// objects, under the object's mutex, before it first looks at them, and uncounts itself on every return.
struct DispatcherWaiterSlot
{
    std::mutex* mutex;
    uint32_t* count;
};

struct DispatcherWaiterRegistration
{
    std::vector<DispatcherWaiterSlot>& slots;

    DispatcherWaiterRegistration(std::vector<DispatcherWaiterSlot>& slots, uint32_t count, xpointer<XDISPATCHER_HEADER>* objects,
        KernelObject* const* kernelObjects)
        : slots(slots)
    {
        slots.clear();
        if (!g_targetedDispatcherWakeups)
            return;

        for (uint32_t i = 0; i < count; i++)
        {
            if (objects[i].get()->Type == 5)
            {
                auto* semaphore = static_cast<Semaphore*>(kernelObjects[i]);
                slots.push_back({ &semaphore->mutex, &semaphore->dispatcherWaiters });
            }
            else
            {
                auto* event = static_cast<Event*>(kernelObjects[i]);
                slots.push_back({ &event->mutex, &event->dispatcherWaiters });
            }
        }

        for (auto& slot : slots)
        {
            std::lock_guard lock(*slot.mutex);
            ++*slot.count;
        }
    }

    ~DispatcherWaiterRegistration()
    {
        for (auto& slot : slots)
        {
            std::lock_guard lock(*slot.mutex);
            --*slot.count;
        }
    }
};

uint32_t KeWaitForMultipleObjects(uint32_t Count, xpointer<XDISPATCHER_HEADER>* Objects, uint32_t WaitType, uint32_t WaitReason, uint32_t WaitMode, uint32_t Alertable, be<int64_t>* Timeout)
{
    const uint64_t timeout = GuestTimeoutToMilliseconds(Timeout);
    assert(timeout == 0 || timeout == INFINITE);

    auto queryObject = [](XDISPATCHER_HEADER& header) -> KernelObject*
    {
        switch (header.Type)
        {
            case 0:
            case 1:
                return QueryKernelObject<Event>(header);

            case 5:
                return QueryKernelObject<Semaphore>(header);

            default:
                assert(false && "Unrecognized kernel object type.");
                return nullptr;
        }
    };

    if (WaitType == 0) // Wait all
    {
        thread_local std::vector<KernelObject*> s_objects;
        s_objects.resize(Count);

        for (size_t i = 0; i < Count; i++)
        {
            auto* object = Objects[i].get();
            if (object == nullptr)
                return STATUS_TIMEOUT;

            s_objects[i] = queryObject(*object);
        }

        thread_local std::vector<DispatcherWaiterSlot> s_slots;
        DispatcherWaiterRegistration registration(s_slots, timeout != 0 ? Count : 0, Objects, s_objects.data());

        while (true)
        {
            const uint32_t generation = g_dispatcherGeneration.load(std::memory_order_acquire);
            bool allSignaled = true;

            for (size_t i = 0; i < Count; i++)
            {
                if (!s_objects[i]->IsSignaled())
                {
                    allSignaled = false;
                    break;
                }
            }

            if (allSignaled)
            {
                for (size_t i = 0; i < Count; i++)
                    s_objects[i]->Wait(0);

                return STATUS_SUCCESS;
            }

            if (timeout == 0)
                return STATUS_TIMEOUT;

            WaitDispatcherGeneration(generation);
        }
    }
    else
    {
        thread_local std::vector<KernelObject*> s_objects;
        s_objects.resize(Count);

        for (size_t i = 0; i < Count; i++)
        {
            auto* object = Objects[i].get();
            if (object == nullptr)
                return STATUS_TIMEOUT;

            s_objects[i] = queryObject(*object);
        }

        thread_local std::vector<DispatcherWaiterSlot> s_slots;
        DispatcherWaiterRegistration registration(s_slots, timeout != 0 ? Count : 0, Objects, s_objects.data());

        while (true)
        {
            uint32_t generation = g_dispatcherGeneration.load(std::memory_order_acquire);

            for (size_t i = 0; i < Count; i++)
            {
                if (s_objects[i]->Wait(0) == STATUS_SUCCESS)
                {
                    return STATUS_WAIT_0 + i;
                }
            }

            if (timeout == 0)
                return STATUS_TIMEOUT;

            WaitDispatcherGeneration(generation);
        }
    }

    return STATUS_SUCCESS;
}

uint32_t KeRaiseIrqlToDpcLevel()
{
    return 0;
}

void KfLowerIrql() { }

uint32_t KeReleaseSemaphore(XKSEMAPHORE* semaphore, uint32_t increment, uint32_t adjustment, uint32_t wait)
{
    auto* object = QueryKernelObject<Semaphore>(semaphore->Header);
    object->Release(adjustment, nullptr);
    return STATUS_SUCCESS;
}

void XAudioGetVoiceCategoryVolume()
{
    LOG_UTILITY("!!! STUB !!!");
}

uint32_t XAudioGetVoiceCategoryVolumeChangeMask(uint32_t Driver, be<uint32_t>* Mask)
{
    *Mask = 0;
    return 0;
}

uint32_t KeResumeThread(GuestThreadHandle* object)
{
    assert(object != GetKernelObject(CURRENT_THREAD_HANDLE));

    object->Resume();
    return 0;
}

void KeInitializeSemaphore(XKSEMAPHORE* semaphore, uint32_t count, uint32_t limit)
{
    semaphore->Header.Type = 5;
    semaphore->Header.SignalState = count;
    semaphore->Limit = limit;

    auto* object = QueryKernelObject<Semaphore>(semaphore->Header);
}

void XMAReleaseContext()
{
    LOG_UTILITY("!!! STUB !!!");
}

void XMACreateContext()
{
    LOG_UTILITY("!!! STUB !!!");
}

// uint32_t XAudioRegisterRenderDriverClient(be<uint32_t>* callback, be<uint32_t>* driver)
// {
//     //printf("XAudioRegisterRenderDriverClient(): %x %x\n");
// 
//     *driver = apu::RegisterClient(callback[0], callback[1]);
//     return 0;
// }

// void XAudioUnregisterRenderDriverClient()
// {
//     printf("!!! STUB !!! XAudioUnregisterRenderDriverClient\n");
// }

// uint32_t XAudioSubmitRenderDriverFrame(uint32_t driver, void* samples)
// {
//     // printf("!!! STUB !!! XAudioSubmitRenderDriverFrame\n");
//     apu::SubmitFrames(samples);
// 
//     return 0;
// }

void XapiInitProcess()
{
    printf("XapiInitProcess Invoked\n");

    int *XapiProcessHeap = (int *)g_memory.Translate(0x82D57540);

    *XapiProcessHeap = 1;
}

GUEST_FUNCTION_HOOK(sub_825383D8, XapiInitProcess)
GUEST_FUNCTION_HOOK(__imp__XGetVideoMode, VdQueryVideoMode); // XGetVideoMode
GUEST_FUNCTION_HOOK(__imp__XNotifyGetNext, XNotifyGetNext);
GUEST_FUNCTION_HOOK(__imp__XGetGameRegion, XGetGameRegion);
GUEST_FUNCTION_HOOK(__imp__XMsgStartIORequest, XMsgStartIORequest);
GUEST_FUNCTION_HOOK(__imp__XamUserGetSigninState, XamUserGetSigninState);
GUEST_FUNCTION_HOOK(__imp__XamGetSystemVersion, XamGetSystemVersion);
GUEST_FUNCTION_HOOK(__imp__XamContentCreateEx, XamContentCreateEx);
GUEST_FUNCTION_HOOK(__imp__XamContentDelete, XamContentDelete);
GUEST_FUNCTION_HOOK(__imp__XamContentClose, XamContentClose);
GUEST_FUNCTION_HOOK(__imp__XamContentGetCreator, XamContentGetCreator);
GUEST_FUNCTION_HOOK(__imp__XamContentCreateEnumerator, XamContentCreateEnumerator);
GUEST_FUNCTION_HOOK(__imp__XamContentGetDeviceState, XamContentGetDeviceState);
GUEST_FUNCTION_HOOK(__imp__XamContentGetDeviceData, XamContentGetDeviceData);
GUEST_FUNCTION_HOOK(__imp__XamEnumerate, XamEnumerate);
GUEST_FUNCTION_HOOK(__imp__XamNotifyCreateListener, XamNotifyCreateListener);
GUEST_FUNCTION_HOOK(__imp__XamUserGetSigninInfo, XamUserGetSigninInfo);
GUEST_FUNCTION_HOOK(__imp__XamShowSigninUI, XamShowSigninUI);
GUEST_FUNCTION_HOOK(__imp__XamShowDeviceSelectorUI, XamShowDeviceSelectorUI);
GUEST_FUNCTION_HOOK(__imp__XamShowMessageBoxUI, XamShowMessageBoxUI);
GUEST_FUNCTION_HOOK(__imp__XamShowDirtyDiscErrorUI, XamShowDirtyDiscErrorUI);
GUEST_FUNCTION_HOOK(__imp__XamEnableInactivityProcessing, XamEnableInactivityProcessing);
GUEST_FUNCTION_HOOK(__imp__XamResetInactivity, XamResetInactivity);
GUEST_FUNCTION_HOOK(__imp__XamShowMessageBoxUIEx, XamShowMessageBoxUIEx);
GUEST_FUNCTION_HOOK(__imp__XGetLanguage, XGetLanguage);
GUEST_FUNCTION_HOOK(__imp__XGetAVPack, XGetAVPack);
GUEST_FUNCTION_HOOK(__imp__XamLoaderTerminateTitle, XamLoaderTerminateTitle);
GUEST_FUNCTION_HOOK(__imp__XamGetExecutionId, XamGetExecutionId);
GUEST_FUNCTION_HOOK(__imp__XamLoaderLaunchTitle, XamLoaderLaunchTitle);
GUEST_FUNCTION_HOOK(__imp__NtOpenFile, NtOpenFile);
GUEST_FUNCTION_HOOK(__imp__RtlInitAnsiString, RtlInitAnsiString);
GUEST_FUNCTION_HOOK(__imp__NtCreateFile, NtCreateFile);
GUEST_FUNCTION_HOOK(__imp__NtClose, NtClose);
GUEST_FUNCTION_HOOK(__imp__NtSetInformationFile, NtSetInformationFile);
GUEST_FUNCTION_HOOK(__imp__FscGetCacheElementCount, FscGetCacheElementCount);
GUEST_FUNCTION_HOOK(__imp__FscSetCacheElementCount, FscSetCacheElementCount);
GUEST_FUNCTION_HOOK(__imp__XamLoaderGetLaunchDataSize, XamLoaderGetLaunchDataSize);
GUEST_FUNCTION_HOOK(__imp__XamLoaderGetLaunchData, XamLoaderGetLaunchData);
GUEST_FUNCTION_HOOK(__imp__XamLoaderSetLaunchData, XamLoaderSetLaunchData);
GUEST_FUNCTION_HOOK(__imp__NtWaitForSingleObjectEx, NtWaitForSingleObjectEx);
GUEST_FUNCTION_HOOK(__imp__NtWriteFile, NtWriteFile);
GUEST_FUNCTION_HOOK(__imp__ExGetXConfigSetting, ExGetXConfigSetting);
GUEST_FUNCTION_HOOK(__imp__NtQueryVirtualMemory, NtQueryVirtualMemory);
GUEST_FUNCTION_HOOK(__imp__MmQueryStatistics, MmQueryStatistics);
GUEST_FUNCTION_HOOK(__imp__NtCreateEvent, NtCreateEvent);
GUEST_FUNCTION_HOOK(__imp__XexCheckExecutablePrivilege, XexCheckExecutablePrivilege);
GUEST_FUNCTION_HOOK(__imp__DbgPrint, DbgPrint);
GUEST_FUNCTION_HOOK(__imp____C_specific_handler, __C_specific_handler_x);
GUEST_FUNCTION_HOOK(__imp__RtlNtStatusToDosError, RtlNtStatusToDosError);
GUEST_FUNCTION_HOOK(__imp__XexGetProcedureAddress, XexGetProcedureAddress);
GUEST_FUNCTION_HOOK(__imp__XexGetModuleSection, XexGetModuleSection);
GUEST_FUNCTION_HOOK(__imp__RtlUnicodeToMultiByteN, RtlUnicodeToMultiByteN);
GUEST_FUNCTION_HOOK(__imp__KeDelayExecutionThread, KeDelayExecutionThread);
GUEST_FUNCTION_HOOK(__imp__ExFreePool, ExFreePool);
GUEST_FUNCTION_HOOK(__imp__NtQueryInformationFile, NtQueryInformationFile);
GUEST_FUNCTION_HOOK(__imp__NtQueryVolumeInformationFile, NtQueryVolumeInformationFile);
GUEST_FUNCTION_HOOK(__imp__NtQueryDirectoryFile, NtQueryDirectoryFile);
GUEST_FUNCTION_HOOK(__imp__NtReadFileScatter, NtReadFileScatter);
GUEST_FUNCTION_HOOK(__imp__NtReadFile, NtReadFile);
GUEST_FUNCTION_HOOK(__imp__NtDuplicateObject, NtDuplicateObject);
GUEST_FUNCTION_HOOK(__imp__NtAllocateVirtualMemory, NtAllocateVirtualMemory);
GUEST_FUNCTION_HOOK(__imp__NtFreeVirtualMemory, NtFreeVirtualMemory);
GUEST_FUNCTION_HOOK(__imp__ObDereferenceObject, ObDereferenceObject);
GUEST_FUNCTION_HOOK(__imp__KeSetBasePriorityThread, KeSetBasePriorityThread);
GUEST_FUNCTION_HOOK(__imp__ObReferenceObjectByHandle, ObReferenceObjectByHandle);
GUEST_FUNCTION_HOOK(__imp__KeQueryBasePriorityThread, KeQueryBasePriorityThread);
GUEST_FUNCTION_HOOK(__imp__NtSuspendThread, NtSuspendThread);
GUEST_FUNCTION_HOOK(__imp__KeSetAffinityThread, KeSetAffinityThread);
GUEST_FUNCTION_HOOK(__imp__RtlLeaveCriticalSection, RtlLeaveCriticalSection);
#if defined(__SWITCH__)
// The caller's r13 straight from its context (see RtlEnterCriticalSectionAs); the pointer as the hook would pass it.
PPC_FUNC(__imp__RtlEnterCriticalSection)
{
    RtlEnterCriticalSectionAs(ctx.r3.u32 != 0 ? reinterpret_cast<XRTL_CRITICAL_SECTION*>(base + ctx.r3.u32) : nullptr, ctx.r13.u32);
}
#else
GUEST_FUNCTION_HOOK(__imp__RtlEnterCriticalSection, RtlEnterCriticalSection);
#endif
GUEST_FUNCTION_HOOK(__imp__RtlImageXexHeaderField, RtlImageXexHeaderField);
GUEST_FUNCTION_HOOK(__imp__HalReturnToFirmware, HalReturnToFirmware);
GUEST_FUNCTION_HOOK(__imp__RtlFillMemoryUlong, RtlFillMemoryUlong);
GUEST_FUNCTION_HOOK(__imp__KeBugCheckEx, KeBugCheckEx);
GUEST_FUNCTION_HOOK(__imp__KeGetCurrentProcessType, KeGetCurrentProcessType);
GUEST_FUNCTION_HOOK(__imp__RtlCompareMemoryUlong, RtlCompareMemoryUlong);
GUEST_FUNCTION_HOOK(__imp__RtlInitializeCriticalSection, RtlInitializeCriticalSection);
GUEST_FUNCTION_HOOK(__imp__RtlRaiseException, RtlRaiseException_x);
GUEST_FUNCTION_HOOK(__imp__KfReleaseSpinLock, KfReleaseSpinLock);
#if defined(__SWITCH__)
PPC_FUNC(__imp__KfAcquireSpinLock)
{
    AcquireGuestSpinLock(ctx.r3.u32 != 0 ? reinterpret_cast<uint32_t*>(base + ctx.r3.u32) : nullptr, ctx.r13.u32);
}
#else
GUEST_FUNCTION_HOOK(__imp__KfAcquireSpinLock, KfAcquireSpinLock);
#endif
GUEST_FUNCTION_HOOK(__imp__KeQueryPerformanceFrequency, KeQueryPerformanceFrequency);
GUEST_FUNCTION_HOOK(__imp__MmFreePhysicalMemory, MmFreePhysicalMemory);
GUEST_FUNCTION_HOOK(__imp__VdPersistDisplay, VdPersistDisplay);
GUEST_FUNCTION_HOOK(__imp__VdSwap, VdSwap);
GUEST_FUNCTION_HOOK(__imp__VdGetSystemCommandBuffer, VdGetSystemCommandBuffer);
GUEST_FUNCTION_HOOK(__imp__KeReleaseSpinLockFromRaisedIrql, KeReleaseSpinLockFromRaisedIrql);
#if defined(__SWITCH__)
PPC_FUNC(__imp__KeAcquireSpinLockAtRaisedIrql)
{
    AcquireGuestSpinLock(ctx.r3.u32 != 0 ? reinterpret_cast<uint32_t*>(base + ctx.r3.u32) : nullptr, ctx.r13.u32);
}
#else
GUEST_FUNCTION_HOOK(__imp__KeAcquireSpinLockAtRaisedIrql, KeAcquireSpinLockAtRaisedIrql);
#endif
GUEST_FUNCTION_HOOK(__imp__KiApcNormalRoutineNop, KiApcNormalRoutineNop);
GUEST_FUNCTION_HOOK(__imp__VdEnableRingBufferRPtrWriteBack, VdEnableRingBufferRPtrWriteBack);
GUEST_FUNCTION_HOOK(__imp__VdInitializeRingBuffer, VdInitializeRingBuffer);
GUEST_FUNCTION_HOOK(__imp__MmGetPhysicalAddress, MmGetPhysicalAddress);
GUEST_FUNCTION_HOOK(__imp__VdSetSystemCommandBufferGpuIdentifierAddress, VdSetSystemCommandBufferGpuIdentifierAddress);
GUEST_FUNCTION_HOOK(__imp__ExRegisterTitleTerminateNotification, ExRegisterTitleTerminateNotification);
GUEST_FUNCTION_HOOK(__imp__VdShutdownEngines, VdShutdownEngines);
GUEST_FUNCTION_HOOK(__imp__VdQueryVideoMode, VdQueryVideoMode);
GUEST_FUNCTION_HOOK(__imp__VdGetCurrentDisplayInformation, VdGetCurrentDisplayInformation);
GUEST_FUNCTION_HOOK(__imp__VdSetDisplayMode, VdSetDisplayMode);
GUEST_FUNCTION_HOOK(__imp__VdSetGraphicsInterruptCallback, VdSetGraphicsInterruptCallback);
GUEST_FUNCTION_HOOK(__imp__VdInitializeEngines, VdInitializeEngines);
GUEST_FUNCTION_HOOK(__imp__VdIsHSIOTrainingSucceeded, VdIsHSIOTrainingSucceeded);
GUEST_FUNCTION_HOOK(__imp__VdGetCurrentDisplayGamma, VdGetCurrentDisplayGamma);
GUEST_FUNCTION_HOOK(__imp__VdQueryVideoFlags, VdQueryVideoFlags);
GUEST_FUNCTION_HOOK(__imp__VdCallGraphicsNotificationRoutines, VdCallGraphicsNotificationRoutines);
GUEST_FUNCTION_HOOK(__imp__VdInitializeScalerCommandBuffer, VdInitializeScalerCommandBuffer);
GUEST_FUNCTION_HOOK(__imp__KeLeaveCriticalRegion, KeLeaveCriticalRegion);
GUEST_FUNCTION_HOOK(__imp__VdRetrainEDRAM, VdRetrainEDRAM);
GUEST_FUNCTION_HOOK(__imp__VdRetrainEDRAMWorker, VdRetrainEDRAMWorker);
GUEST_FUNCTION_HOOK(__imp__KeEnterCriticalRegion, KeEnterCriticalRegion);
GUEST_FUNCTION_HOOK(__imp__MmAllocatePhysicalMemoryEx, MmAllocatePhysicalMemoryEx);
GUEST_FUNCTION_HOOK(__imp__ObDeleteSymbolicLink, ObDeleteSymbolicLink);
GUEST_FUNCTION_HOOK(__imp__ObCreateSymbolicLink, ObCreateSymbolicLink);
GUEST_FUNCTION_HOOK(__imp__MmQueryAddressProtect, MmQueryAddressProtect);
GUEST_FUNCTION_HOOK(__imp__VdEnableDisableClockGating, VdEnableDisableClockGating);
GUEST_FUNCTION_HOOK(__imp__KeBugCheck, KeBugCheck);
GUEST_FUNCTION_HOOK(__imp__KeLockL2, KeLockL2);
GUEST_FUNCTION_HOOK(__imp__KeUnlockL2, KeUnlockL2);
GUEST_FUNCTION_HOOK(__imp__KeSetEvent, KeSetEvent);
GUEST_FUNCTION_HOOK(__imp__KeResetEvent, KeResetEvent);
GUEST_FUNCTION_HOOK(__imp__KeWaitForSingleObject, KeWaitForSingleObject);
#if defined(__SWITCH__)
// SwitchFastGuestTls (see KeTlsGetValueRef): the same vector element, found without a thread-local access.
PPC_FUNC(__imp__KeTlsGetValue)
{
    const uint32_t index = ctx.r3.u32;
    if (g_fastGuestTls)
    {
        const std::vector<uint32_t>* values = FastGuestTlsValues();
        if (values != nullptr && index < values->size())
        {
            ctx.r3.u64 = (*values)[index];
            return;
        }
    }

    ctx.r3.u64 = KeTlsGetValue(index);
}

PPC_FUNC(__imp__KeTlsSetValue)
{
    const uint32_t index = ctx.r3.u32;
    const uint32_t value = ctx.r4.u32;
    if (g_fastGuestTls)
    {
        std::vector<uint32_t>* values = FastGuestTlsValues();
        if (values != nullptr && index < values->size())
        {
            (*values)[index] = value;
            ctx.r3.u64 = TRUE;
            return;
        }
    }

    ctx.r3.u64 = KeTlsSetValue(index, value);
}
#else
GUEST_FUNCTION_HOOK(__imp__KeTlsGetValue, KeTlsGetValue);
GUEST_FUNCTION_HOOK(__imp__KeTlsSetValue, KeTlsSetValue);
#endif
GUEST_FUNCTION_HOOK(__imp__KeTlsAlloc, KeTlsAlloc);
GUEST_FUNCTION_HOOK(__imp__KeTlsFree, KeTlsFree);
GUEST_FUNCTION_HOOK(__imp__XMsgInProcessCall, XMsgInProcessCall);
GUEST_FUNCTION_HOOK(__imp__XamUserReadProfileSettings, XamUserReadProfileSettings);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAStartup, NetDll_WSAStartup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACleanup, NetDll_WSACleanup);
GUEST_FUNCTION_HOOK(__imp__NetDll_socket, NetDll_socket);
GUEST_FUNCTION_HOOK(__imp__NetDll_closesocket, NetDll_closesocket);
GUEST_FUNCTION_HOOK(__imp__NetDll_setsockopt, NetDll_setsockopt);
GUEST_FUNCTION_HOOK(__imp__NetDll_bind, NetDll_bind);
GUEST_FUNCTION_HOOK(__imp__NetDll_connect, NetDll_connect);
GUEST_FUNCTION_HOOK(__imp__NetDll_listen, NetDll_listen);
GUEST_FUNCTION_HOOK(__imp__NetDll_accept, NetDll_accept);
GUEST_FUNCTION_HOOK(__imp__NetDll_select, NetDll_select);
GUEST_FUNCTION_HOOK(__imp__NetDll_recv, NetDll_recv);
GUEST_FUNCTION_HOOK(__imp__NetDll_send, NetDll_send);
GUEST_FUNCTION_HOOK(__imp__NetDll_inet_addr, NetDll_inet_addr);
GUEST_FUNCTION_HOOK(__imp__NetDll___WSAFDIsSet, NetDll___WSAFDIsSet);
GUEST_FUNCTION_HOOK(__imp__XMsgStartIORequestEx, XMsgStartIORequestEx);
GUEST_FUNCTION_HOOK(__imp__XamInputGetCapabilities, XamInputGetCapabilities);
GUEST_FUNCTION_HOOK(__imp__XamInputGetState, XamInputGetState);
GUEST_FUNCTION_HOOK(__imp__XamInputSetState, XamInputSetState);
GUEST_FUNCTION_HOOK(__imp__XexGetModuleHandle, XexGetModuleHandle);
#if defined(__SWITCH__)
PPC_FUNC(__imp__RtlTryEnterCriticalSection)
{
    ctx.r3.u64 = RtlTryEnterCriticalSectionAs(ctx.r3.u32 != 0 ? reinterpret_cast<XRTL_CRITICAL_SECTION*>(base + ctx.r3.u32) : nullptr,
        ctx.r13.u32) ? 1 : 0;
}
#else
GUEST_FUNCTION_HOOK(__imp__RtlTryEnterCriticalSection, RtlTryEnterCriticalSection);
#endif
GUEST_FUNCTION_HOOK(__imp__RtlInitializeCriticalSectionAndSpinCount, RtlInitializeCriticalSectionAndSpinCount);
GUEST_FUNCTION_HOOK(__imp__XeCryptBnQwBeSigVerify, XeCryptBnQwBeSigVerify);
GUEST_FUNCTION_HOOK(__imp__XeKeysGetKey, XeKeysGetKey);
GUEST_FUNCTION_HOOK(__imp__XeCryptRotSumSha, XeCryptRotSumSha);
GUEST_FUNCTION_HOOK(__imp__XeCryptSha, XeCryptSha);
GUEST_FUNCTION_HOOK(__imp__KeEnableFpuExceptions, KeEnableFpuExceptions);
GUEST_FUNCTION_HOOK(__imp__RtlUnwind, RtlUnwind_x);
GUEST_FUNCTION_HOOK(__imp__RtlCaptureContext, RtlCaptureContext_x);
GUEST_FUNCTION_HOOK(__imp__NtQueryFullAttributesFile, NtQueryFullAttributesFile);
GUEST_FUNCTION_HOOK(__imp__RtlMultiByteToUnicodeN, RtlMultiByteToUnicodeN);
GUEST_FUNCTION_HOOK(__imp__DbgBreakPoint, DbgBreakPoint);
GUEST_FUNCTION_HOOK(__imp__MmQueryAllocationSize, MmQueryAllocationSize);
GUEST_FUNCTION_HOOK(__imp__NtClearEvent, NtClearEvent);
GUEST_FUNCTION_HOOK(__imp__NtResumeThread, NtResumeThread);
GUEST_FUNCTION_HOOK(__imp__NtSetEvent, NtSetEvent);
GUEST_FUNCTION_HOOK(__imp__NtCreateSemaphore, NtCreateSemaphore);
GUEST_FUNCTION_HOOK(__imp__NtReleaseSemaphore, NtReleaseSemaphore);
GUEST_FUNCTION_HOOK(__imp__NtWaitForMultipleObjectsEx, NtWaitForMultipleObjectsEx);
GUEST_FUNCTION_HOOK(__imp__RtlCompareStringN, RtlCompareStringN);
GUEST_FUNCTION_HOOK(__imp__StfsControlDevice, StfsControlDevice);
GUEST_FUNCTION_HOOK(__imp__StfsCreateDevice, StfsCreateDevice);
GUEST_FUNCTION_HOOK(__imp__NtFlushBuffersFile, NtFlushBuffersFile);
GUEST_FUNCTION_HOOK(__imp__KeQuerySystemTime, KeQuerySystemTime);
GUEST_FUNCTION_HOOK(__imp__RtlTimeToTimeFields, RtlTimeToTimeFields);
GUEST_FUNCTION_HOOK(__imp__RtlFreeAnsiString, RtlFreeAnsiString);
GUEST_FUNCTION_HOOK(__imp__RtlUnicodeStringToAnsiString, RtlUnicodeStringToAnsiString);
GUEST_FUNCTION_HOOK(__imp__RtlInitUnicodeString, RtlInitUnicodeString);
GUEST_FUNCTION_HOOK(__imp__ExTerminateThread, ExTerminateThread);
GUEST_FUNCTION_HOOK(__imp__ExCreateThread, ExCreateThread);
GUEST_FUNCTION_HOOK(__imp__IoInvalidDeviceRequest, IoInvalidDeviceRequest);
GUEST_FUNCTION_HOOK(__imp__ObReferenceObject, ObReferenceObject);
GUEST_FUNCTION_HOOK(__imp__IoCreateDevice, IoCreateDevice);
GUEST_FUNCTION_HOOK(__imp__IoDeleteDevice, IoDeleteDevice);
GUEST_FUNCTION_HOOK(__imp__ExAllocatePoolTypeWithTag, ExAllocatePoolTypeWithTag);
GUEST_FUNCTION_HOOK(__imp__RtlTimeFieldsToTime, RtlTimeFieldsToTime);
GUEST_FUNCTION_HOOK(__imp__IoCompleteRequest, IoCompleteRequest);
GUEST_FUNCTION_HOOK(__imp__RtlUpcaseUnicodeChar, RtlUpcaseUnicodeChar);
GUEST_FUNCTION_HOOK(__imp__ObIsTitleObject, ObIsTitleObject);
GUEST_FUNCTION_HOOK(__imp__IoCheckShareAccess, IoCheckShareAccess);
GUEST_FUNCTION_HOOK(__imp__IoSetShareAccess, IoSetShareAccess);
GUEST_FUNCTION_HOOK(__imp__IoRemoveShareAccess, IoRemoveShareAccess);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetStartup, NetDll_XNetStartup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetTitleXnAddr, NetDll_XNetGetTitleXnAddr);
GUEST_FUNCTION_HOOK(__imp__KeWaitForMultipleObjects, KeWaitForMultipleObjects);
GUEST_FUNCTION_HOOK(__imp__KeRaiseIrqlToDpcLevel, KeRaiseIrqlToDpcLevel);
GUEST_FUNCTION_HOOK(__imp__KfLowerIrql, KfLowerIrql);
GUEST_FUNCTION_HOOK(__imp__KeReleaseSemaphore, KeReleaseSemaphore);
GUEST_FUNCTION_HOOK(__imp__XAudioGetVoiceCategoryVolume, XAudioGetVoiceCategoryVolume);
GUEST_FUNCTION_HOOK(__imp__XAudioGetVoiceCategoryVolumeChangeMask, XAudioGetVoiceCategoryVolumeChangeMask);
GUEST_FUNCTION_HOOK(__imp__KeResumeThread, KeResumeThread);
GUEST_FUNCTION_HOOK(__imp__KeInitializeSemaphore, KeInitializeSemaphore);
GUEST_FUNCTION_HOOK(__imp__XMAReleaseContext, XMAReleaseContext);
GUEST_FUNCTION_HOOK(__imp__XMACreateContext, XMACreateContext);
GUEST_FUNCTION_HOOK(__imp__XAudioRegisterRenderDriverClient, XAudioRegisterRenderDriverClient);
GUEST_FUNCTION_HOOK(__imp__XAudioUnregisterRenderDriverClient, XAudioUnregisterRenderDriverClient);
GUEST_FUNCTION_HOOK(__imp__XAudioSubmitRenderDriverFrame, XAudioSubmitRenderDriverFrame);
