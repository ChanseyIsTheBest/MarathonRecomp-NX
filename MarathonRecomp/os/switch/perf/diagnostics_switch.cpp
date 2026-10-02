#if defined(__SWITCH__)

#include <os/logger.h>
#include <os/switch_cpu_profiler.h>
#include <os/switch_crash.h>
#include <os/switch_overlay.h>
#include <os/switch_perf.h>
#include <os/switch_perf_init.h>
#include <os/switch_stall_watch.h>
#include <user/config.h>

// [Switch] Diagnostics area (docs/SWITCH-PERFORMANCE.md): logs, crash reports, CPU profiler, stall watchdog, overlay
// FPS and the handheld GPU profile. Runs first of the areas' init hooks (os/switch/perf/switch_perf_init.cpp), on the
// main thread, right after Config::Load(), so that whatever the others print has somewhere to go. None of it changes
// what the game computes or draws: it writes files, reads other threads' registers, or sets clocks.

void SwitchPerfInitDiagnostics()
{
    const bool log = Config::SwitchLog;
    const bool cpuProfiler = Config::SwitchCpuProfiler;
    const double stallWatchSeconds = Config::SwitchStallWatchSeconds;

    // MarathonRecomp.log: the lines kept since boot are written to it, or dropped with every later one (from here on
    // the LOG* macros do not even format their lines).
    os::logger::SetFileEnabled(log);

    // stderr.log carries the renderer's and the driver's messages and the profilers' reports: the profilers write
    // nowhere else, so they turn it on by themselves, here, before the other areas print their init lines and before
    // their threads start. Horizon has no console behind stderr: without this it goes nowhere. Its first lines name
    // the build and list every [Switch] key with its value.
    // The renderer's reports (resolve statistics, GPU pass/draw profilers, frame log) also write only to stderr.log;
    // the renderer's init runs last, so the file is opened here for them too. The overlay's ([overlay]) and the
    // handheld GPU profile's ([apm]) status lines go there only when it is open anyway (1.0.3: features that stay on
    // in the release build, which writes no log).
    const bool stderrOnly = cpuProfiler || stallWatchSeconds > 0.0 || Config::SwitchResolveStats ||
        Config::SwitchGpuPassProfiler || Config::SwitchGpuDrawProfiler || Config::SwitchFrameLog;
    if (log || stderrOnly)
        os::logger::EnableStderrLog();

    // crash.log on a fatal CPU exception or a lost GPU, whatever SwitchLog says.
    os::switch_crash::Init(log);

    // The main thread becomes the game's main thread (main.cpp renames it once the guest code starts).
    os::switch_cpu_profiler::RegisterCurrentThread("main");
    if (cpuProfiler)
        os::switch_cpu_profiler::SetSlowFrameThreshold(uint32_t(std::max<int32_t>(0, Config::SwitchSlowFrameProfileMs)));
    os::switch_cpu_profiler::Start(cpuProfiler);
    os::switch_stall_watch::Start(stallWatchSeconds);

    if (Config::SwitchHandheldGpuBoost)
        os::switch_perf::StartHandheldGpuBoost();

    if (Config::SwitchOverlayFps)
        os::switch_overlay::Start();
}

#endif
