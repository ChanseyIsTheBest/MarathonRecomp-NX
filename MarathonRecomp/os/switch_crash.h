#pragma once

#if defined(__SWITCH__)

#include <cstdint>

// [Switch] Crash reports. A fatal CPU exception or a lost GPU (VK_ERROR_DEVICE_LOST from a submission, fence
// wait, query read, present or acquire) appends a report to sdmc:/switch/MarathonRecomp/crash.log, whatever
// [Switch] SwitchLog says, before the game breaks into Atmosphère's own crash report.
//
// Marathon's exception handler (os/switch/exception_switch.cpp) stays the libnx user exception handler: it
// emulates faulting guest-window accesses first (PC's committed-zero semantics) and only a fault it cannot
// emulate is fatal. Its fatal path calls WriteCpuExceptionReport before FatalFaultBreak.
//
// A CPU report has the faulting pc, lr, far and esr, the registers, the frame-pointer chain, the return
// addresses found on the stack and a dump of it; offsets into the executable are "+0x..." on "[crash]"
// lines, which tools/switch-cpu-profile.py names. A GPU report has the driver's own messages from just
// before, among them the reason the channel was lost (a release Mesa hands them only to a
// VK_EXT_debug_utils messenger, which plume makes on Switch).
namespace os::switch_crash
{
    // Installs plume's device-lost and driver message callbacks. `alsoStderr`: driver errors and the
    // reports go to stderr.log as well (SwitchLog).
    void Init(bool alsoStderr);

    // Called by the exception handler's fatal path with its ThreadExceptionDump*. Must not take locks
    // or allocate (the faulting thread may hold them). Writes the report on a stack of its own from a
    // copy of the dump, since every thread's handler shares libnx's exception stack and dump; a second
    // fatal fault meanwhile only waits a few seconds and returns. Defined in assembly (crash_switch.cpp).
    // Weak: a build without the module skips it.
    void WriteCpuExceptionReport(const void* threadExceptionDump) __attribute__((weak));
}

#endif
