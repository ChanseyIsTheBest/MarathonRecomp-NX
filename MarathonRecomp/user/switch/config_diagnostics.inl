// [Switch] keys of the diagnostics area. Included by user/config_def.h inside its __SWITCH__ block.
// CONFIG_DEFINE_HIDDEN("Switch", type, Name, default, requiresRestart);
// Read once by SwitchPerfInitDiagnostics (os/switch/perf/diagnostics_switch.cpp); none of them changes what the game
// does or draws. crash.log is written on a crash whatever these say.

// Diagnostics: write stderr.log (the renderer's and the driver's messages, the profilers' reports) and MarathonRecomp.log
// (the game's log). Off in release builds: neither file is opened and the log lines are not even formatted.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLog, false, true);
// Measurement: where the registered threads spend CPU time, sampled every 2 ms and written to stderr.log every 30 s as
// code addresses (tools/switch-cpu-profile.py names them). Turns stderr.log on by itself.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCpuProfiler, false, true);
// With SwitchCpuProfiler: frames whose game-thread work took at least this many milliseconds get their own report, with
// every sample of the game thread in them (running or waiting) and its callers (0 = off).
CONFIG_DEFINE_HIDDEN("Switch", int32_t, SwitchSlowFrameProfileMs, 0, true);
// Diagnostics: after this many seconds without a presented frame, write what every thread is doing to stderr.log
// ("[stall]" lines; tools/switch-cpu-profile.py names them), and sum up frames over 100 ms ("[hitch]"). 0 turns the
// watchdog off (release builds); 1 for tests. Turns stderr.log on by itself.
CONFIG_DEFINE_HIDDEN("Switch", float, SwitchStallWatchSeconds, 0.0f, true);
// Publishes FPS and render resolution for Status Monitor / SaltyNX overlays. Only does anything when SaltyNX is
// installed (see the notes in os/switch/perf/overlay_switch.cpp). Its status lines go to stderr.log only when that is
// open for another reason.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchOverlayFps, true, true);
// Requests the stock 460.8 MHz handheld GPU profile (memory stays at 1331.2 MHz) whenever the console is in handheld
// mode (perf8: also after undocking while playing). It trades battery life and heat for GPU headroom. Its "[apm]"
// lines go to stderr.log only when that is open for another reason.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchHandheldGpuBoost, true, true);
