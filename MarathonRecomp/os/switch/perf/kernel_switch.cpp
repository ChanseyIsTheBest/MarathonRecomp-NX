// [Switch] Kernel area: the [Switch] keys of user/switch/config_kernel.inl, read once into the plain globals the
// guest kernel (kernel/imports.cpp), the guest threads (cpu/guest_thread.cpp) and the file I/O
// (kernel/io/file_system.cpp) test on their hot paths.
#if defined(__SWITCH__)

#include <os/switch_cpu_profiler.h>
#include <os/switch_perf_init.h>
#include <user/config.h>

// kernel/imports.cpp
extern bool g_fastGuestTls;
extern bool g_sleepingGuestLocks;
extern bool g_fastCriticalSections;
extern bool g_leanCriticalSectionLeave;
extern bool g_guestSpinBeforeSleep;
extern bool g_fastEvents;
extern bool g_semaphoreWakeCount;
extern bool g_targetedDispatcherWakeups;
extern bool g_criticalSectionSpin;
extern bool g_eventSpin;
bool SwitchInitFastGuestTls();

// cpu/guest_thread.cpp
extern bool g_threadIdealCores;
extern bool g_jobWorkersOffMainCore;
extern bool g_soundThreadOffMainCore;

// kernel/io/file_system.cpp
extern bool g_fewerFileQueries;

// Called from main() (SwitchPerfOnConfigLoaded) after Config::Load, before the guest kernel, the audio or any guest
// thread starts: nothing reads these globals concurrently with these stores.
void SwitchPerfInitKernel()
{
    const bool fastGuestTlsReady = Config::SwitchFastGuestTls && SwitchInitFastGuestTls();

    g_fastGuestTls = fastGuestTlsReady;
    g_fewerFileQueries = Config::SwitchFewerFileQueries;
    g_sleepingGuestLocks = Config::SwitchSleepingGuestLocks;
    g_fastCriticalSections = Config::SwitchFastCriticalSections;
    g_leanCriticalSectionLeave = Config::SwitchLeanCriticalSectionLeave;
    g_guestSpinBeforeSleep = Config::SwitchGuestSpinBeforeSleep;
    g_fastEvents = Config::SwitchFastEvents;
    g_semaphoreWakeCount = Config::SwitchSemaphoreWakeCount;
    g_targetedDispatcherWakeups = Config::SwitchTargetedDispatcherWakeups;
    g_criticalSectionSpin = Config::SwitchCriticalSectionSpin;
    g_eventSpin = Config::SwitchEventSpin;
    g_jobWorkersOffMainCore = Config::SwitchJobWorkersOffMainCore;
    g_soundThreadOffMainCore = Config::SwitchSoundThreadOffMainCore;
    g_threadIdealCores = Config::SwitchThreadIdealCores;

    const char* threadIdealCoresNote = "";
#if !defined(PPC_SYNC)
    // Placing the game's threads on different cores on purpose needs the guest's sync/lwsync as real fences
    // (PPC_SYNC/PPC_LWSYNC in the generated ppc_context.h); this build's recompiled code emits them as nothing.
    if (g_threadIdealCores)
    {
        g_threadIdealCores = false;
        threadIdealCoresNote = " (ignored: the guest code of this build has no memory fences)";
    }
#endif

    std::string line = fmt::format("[kernel] SwitchFastGuestTls={}{} SwitchFewerFileQueries={} "
        "SwitchSleepingGuestLocks={} SwitchFastCriticalSections={}{} SwitchLeanCriticalSectionLeave={} SwitchGuestSpinBeforeSleep={} "
        "SwitchFastEvents={} "
        "SwitchSemaphoreWakeCount={} SwitchTargetedDispatcherWakeups={} SwitchCriticalSectionSpin={} SwitchEventSpin={} SwitchJobWorkersOffMainCore={} SwitchSoundThreadOffMainCore={} SwitchThreadIdealCores={}{}\n",
        int(g_fastGuestTls), Config::SwitchFastGuestTls && !fastGuestTlsReady ? " (thread pointer check failed)" : "",
        int(g_fewerFileQueries), int(g_sleepingGuestLocks), int(g_fastCriticalSections),
        g_fastCriticalSections && !g_sleepingGuestLocks ? " (inactive without SwitchSleepingGuestLocks)" : "",
        int(g_leanCriticalSectionLeave),
        int(g_guestSpinBeforeSleep), int(g_fastEvents), int(g_semaphoreWakeCount), int(g_targetedDispatcherWakeups),
        int(g_criticalSectionSpin), int(g_eventSpin), int(g_jobWorkersOffMainCore), int(g_soundThreadOffMainCore), int(g_threadIdealCores),
        threadIdealCoresNote);

    os::switch_cpu_profiler::WriteLog(line);
}

#endif
