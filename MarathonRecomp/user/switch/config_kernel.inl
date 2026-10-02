// [Switch] keys of the kernel area. Included by user/config_def.h inside its __SWITCH__ block.
// CONFIG_DEFINE_HIDDEN("Switch", type, Name, default, requiresRestart);
// Read once by SwitchPerfInitKernel (os/switch/perf/kernel_switch.cpp); hot paths read plain globals.

// KeTlsGetValue/KeTlsSetValue find the calling thread's values without a thread-local access (a call with
// -mtp=soft). The values stay where they are; only the way to their address changes.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFastGuestTls, true, true);
// The size of an open read-only file from its descriptor (one file-system IPC) instead of from its path, and one
// stat instead of two for GetFileAttributes. The same answers.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFewerFileQueries, true, true);
// Guest-kernel changes below: off until a Marathon session with them on keeps its audio through a stage's first
// in-engine event and a pre-rendered movie (the test Unleashed gated its guest-kernel changes on).
// Critical section waiters sleep on the owner word until the final leave wakes them (1 ms safety timeout), and spin
// lock waiters sleep 10 µs, instead of both retrying with yield() (which never lets a lower-priority owner run on
// the waiter's core).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSleepingGuestLocks, true, true);
// With SwitchSleepingGuestLocks: the final leave of a critical section skips its wake-up system call when nobody
// waits (waiters count themselves in LockCount, which the guest never reads). No effect without it (off by
// default), so the default build has no guest-kernel change; true so that the sleeping critical sections always
// come with it. false: every final leave makes the system call (A/B only, slower than the yield loops).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFastCriticalSections, true, true);
// With SwitchFastCriticalSections: the final leave drops the full fence between clearing the owner and reading the
// waiter count, which its sequentially consistent store and load already order (UnleashedRecomp-NX round 12).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLeanCriticalSectionLeave, true, true);
// With SwitchSleepingGuestLocks: a critical section another thread holds is watched for ~2 us (reads only) before the
// kernel wait, as SwitchGuestSpinBeforeSleep does for spin locks (UnleashedRecomp-NX round 14).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCriticalSectionSpin, true, true);
// Only an event set or semaphore release that a KeWaitForMultipleObjects call waits on wakes those calls (the audio
// pump's); every other one skips the dispatcher's mutex and wake-up (UnleashedRecomp-NX round 14).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTargetedDispatcherWakeups, true, true);
// A wait without timeout on an event that is not set watches it for ~2 us (reads only) before sleeping: the game thread
// waits for its job workers' events every frame.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchEventSpin, true, true);
// The game's engine threads started at sub_825866A8 (its job workers and the other per-frame threads the game thread
// waits for at the end of every frame) prefer cores 1 and 2, in turn, instead of core 0, where the game thread runs
// (still allowed on every core). perf6 A/B; on by default since perf7: libnx gives every thread core 0 as its
// preferred core, so a worker woken while the game thread runs waited for another core to take it over.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchJobWorkersOffMainCore, true, true);
// perf7: the game's sound thread (started at sub_8255B848: the CRI mixer, ~20 % of a core) prefers core 2, with the
// host audio threads (SwitchAudioThreadCores), instead of the game thread's core 0. Still allowed on every core.
// Off since perf8: the perf7 log had 23 audio gaps (perf6, with the thread on core 0: none). Guest threads all run at
// the time-sliced priority 0x3B, so on core 2 the mixer shared its time with whichever engine thread landed there (one
// of them at 65 % of a core in the heavy stretches); on core 0 the game thread waits for the mixer's lock anyway.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSoundThreadOffMainCore, false, true);
// Guest spin locks watch the lock word for ~2 µs (reads only) before their yield or sleep. Only pays when the
// owner runs on another core.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchGuestSpinBeforeSleep, true, true);
// Guest events and semaphores (and the KeWaitForMultipleObjects dispatcher) skip their wake-up system calls when
// nobody waits.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFastEvents, true, true);
// A semaphore release of N units wakes N waiters instead of all of them.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSemaphoreWakeCount, true, true);
// A/B: game threads start on the core their SetThreadIdealProcessor calls name (Xbox 360 hardware thread n ->
// Switch core n / 2), still free to move to the others. Wants the recompiled code's sync/lwsync as real fences.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchThreadIdealCores, false, true);
