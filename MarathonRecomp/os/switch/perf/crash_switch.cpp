#if defined(__SWITCH__)

#include <os/switch_crash.h>
#include <os/switch_cpu_profiler.h>
#include <os/switch_stall_watch.h>
#include <kernel/memory.h>

#include <switch.h>
#include <plume_render_interface.h>

#if __has_include(<switch_build_id.h>)
#include <switch_build_id.h>
#endif
#ifndef MARATHON_RECOMP_SWITCH_BUILD_ID
#define MARATHON_RECOMP_SWITCH_BUILD_ID ""
#endif

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// See os/switch_crash.h. Ported from UnleashedRecomp-NX (os/switch/crash_switch.cpp), whose CPU report came from the
// battd_nx port's nx_crash_handler.c. Nothing here takes a lock or allocates once a report has started: the thread that
// crashed may hold the heap's, stdio's or the profiler's. The report is formatted into a static buffer (integer formats
// only: newlib's vsnprintf allocates for floating point alone) and written with the file system service directly
// (fsdev's FsFileSystem, not a FILE).
//
// Marathon difference: this module never defines __libnx_exception_handler or __nx_exception_stack. Marathon's handler
// (os/switch/exception_switch.cpp) must stay the only one, since it emulates faulting guest-window accesses and the game
// depends on that; the toolchain links with --allow-multiple-definition, so a second definition would not even fail
// the link. Its fatal path calls WriteCpuExceptionReport, which returns, and then breaks itself (FatalFaultBreak).
//
// That handler runs on libnx's one exception stack and dump, shared by every thread, and libnx lets the next fault in
// as soon as its entry has returned from the kernel exception. Emulated guest-window faults are routine, so while a
// CPU report waits on the SD card another thread's handler reuses that stack (and rewrites the dump). The report
// therefore keeps nothing there: its entry (assembly, at the end) claims the one report, then writes it on a stack
// of its own from a copy of the dump.

// The one report per process (CPU or GPU): both write g_report. Claimed by BeginReport and by the CPU report's entry,
// which takes it with ldaxrb/stlxrb before it may use the report's stack.
extern "C"
{
    __attribute__((visibility("hidden"), used, externally_visible)) std::atomic<bool> SwitchCrashReporting{ false };
}
static_assert(sizeof(std::atomic<bool>) == 1 && std::atomic<bool>::is_always_lock_free);

namespace
{
    constexpr const char* CRASH_DIRECTORY = "/switch/MarathonRecomp";      // On sdmc.
    constexpr const char* CRASH_PATH = "/switch/MarathonRecomp/crash.log"; // On sdmc.
    constexpr size_t STACK_DUMP_BYTES = 0x200;
    constexpr uint32_t BACKTRACE_DEPTH = 24;
    constexpr uint32_t STACK_SCAN_WORDS = 2048; // 16 KB of stack searched for return addresses.
    constexpr uint32_t STACK_SCAN_MAX = 32;
    constexpr uint64_t SECOND_REPORT_WAIT_NS = 3'000'000'000ull;
    static_assert(SECOND_REPORT_WAIT_NS == 0xb2d05e00, "the CPU report's entry loads it with movz/movk");

    bool g_alsoStderr = false;

    // The guest window, read once at Init (g_memory reserves it during static initialisation).
    uint64_t g_guestBase = 0;

    char g_report[24 * 1024];
    size_t g_reportLength = 0;

    // The CPU report's copy of the faulting thread's dump.
    ThreadExceptionDump g_cpuDump;

    // The driver's latest messages (errors), kept for a GPU report.
    constexpr uint32_t DRIVER_MESSAGES = 8;
    constexpr size_t DRIVER_MESSAGE_BYTES = 480;
    char g_driverMessages[DRIVER_MESSAGES][DRIVER_MESSAGE_BYTES];
    std::atomic<uint32_t> g_driverMessageCount{ 0 };

    // True when this thread may write the report; false after waiting for another thread's report. One report per
    // process: a second thread that crashes meanwhile waits for the first to break, for a few seconds at most; if the
    // first never gets there, the second one breaks instead, so the process never hangs.
    bool BeginReport()
    {
        if (!SwitchCrashReporting.exchange(true))
            return true;

        for (uint64_t waited = 0; waited < SECOND_REPORT_WAIT_NS; waited += 100'000'000ull)
            svcSleepThread(100'000'000ull);

        return false;
    }

    __attribute__((format(printf, 1, 2)))
    void Append(const char* format, ...)
    {
        if (g_reportLength >= sizeof(g_report) - 1)
            return;

        va_list args;
        va_start(args, format);
        const int written = vsnprintf(g_report + g_reportLength, sizeof(g_report) - g_reportLength, format, args);
        va_end(args);

        if (written > 0)
            g_reportLength = std::min(sizeof(g_report) - 1, g_reportLength + size_t(written));
    }

    uint64_t ModuleBase()
    {
        return os::switch_cpu_profiler::ModuleBase();
    }

    bool InModule(uint64_t address)
    {
        const uint64_t base = ModuleBase();
        return base != 0 && address >= base && address - base < os::switch_cpu_profiler::ModuleSize();
    }

    // "+0x..." for an address inside the executable (what tools/switch-cpu-profile.py names), the address otherwise.
    void AppendAddress(uint64_t address)
    {
        if (InModule(address))
            Append(" +0x%07llx", (unsigned long long)(address - ModuleBase()));
        else
            Append(" 0x%llx", (unsigned long long)address);
    }

    bool Readable(uint64_t address, size_t size)
    {
        if (address < 0x1000 || address + size < address)
            return false;

        uint64_t at = address;
        const uint64_t end = address + size;
        while (at < end)
        {
            MemoryInfo info{};
            u32 pageInfo = 0;
            if (R_FAILED(svcQueryMemory(&info, &pageInfo, at)) || info.type == MemType_Unmapped || (info.perm & Perm_R) == 0)
                return false;

            const uint64_t blockEnd = info.addr + info.size;
            if (blockEnd <= at)
                return false;
            at = blockEnd;
        }

        return true;
    }

    void AppendHeader(const char* what)
    {
        const uint64_t uptimeMs = armTicksToNs(armGetSystemTick()) / 1'000'000ull;
        u64 now = 0;
        timeGetCurrentTime(TimeType_UserSystemClock, &now);

        char name[48] = "unregistered";
        os::switch_cpu_profiler::TryGetCurrentThreadName(name, sizeof(name));
        u64 threadId = 0;
        svcGetThreadId(&threadId, CUR_THREAD_HANDLE);

        Append("\n[crash] ===== %s =====\n", what);
        Append("[crash] build %s; posix time %llu; %llu.%03llu s after boot of the process clock; %llu frames presented; "
            "thread %s (id 0x%llx); module base 0x%llx\n",
            MARATHON_RECOMP_SWITCH_BUILD_ID[0] != '\0' ? MARATHON_RECOMP_SWITCH_BUILD_ID : "unnamed", (unsigned long long)now,
            (unsigned long long)(uptimeMs / 1000), (unsigned long long)(uptimeMs % 1000),
            (unsigned long long)os::switch_stall_watch::FrameCount(), name, (unsigned long long)threadId,
            (unsigned long long)ModuleBase());
    }

    // Appends the report to crash.log (and to stderr.log with SwitchLog when `stderrToo`).
    void WriteReport(bool stderrToo)
    {
        if (FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc"))
        {
            fsFsCreateDirectory(fs, "/switch");
            fsFsCreateDirectory(fs, CRASH_DIRECTORY);
            fsFsCreateFile(fs, CRASH_PATH, 0, 0); // Fails when it exists, which is fine.

            FsFile file;
            if (R_SUCCEEDED(fsFsOpenFile(fs, CRASH_PATH, FsOpenMode_Write | FsOpenMode_Append, &file)))
            {
                s64 size = 0;
                fsFileGetSize(&file, &size);
                fsFileWrite(&file, size, g_report, g_reportLength, FsWriteOption_Flush);
                fsFileClose(&file);
            }
        }

        if (stderrToo && g_alsoStderr)
        {
            fwrite(g_report, 1, g_reportLength, stderr);
            fflush(stderr);
        }
    }

    [[noreturn]] void Break()
    {
        svcBreak(BreakReason_Panic, 0, 0);
        for (;;)
            svcSleepThread(1'000'000'000ull);
    }

    const char* ExceptionName(u32 desc)
    {
        switch (desc)
        {
        case ThreadExceptionDesc_InstructionAbort: return "instruction abort";
        case ThreadExceptionDesc_MisalignedPC: return "misaligned pc";
        case ThreadExceptionDesc_MisalignedSP: return "misaligned sp";
        case ThreadExceptionDesc_SError: return "SError";
        case ThreadExceptionDesc_BadSVC: return "bad svc";
        case ThreadExceptionDesc_Trap: return "trap (undefined instruction, illegal state)";
        case ThreadExceptionDesc_Other: return "data abort or other (see esr)";
        default: return "unknown";
        }
    }

    void OnDriverMessage(const char* message)
    {
        const uint32_t index = g_driverMessageCount.fetch_add(1, std::memory_order_relaxed) % DRIVER_MESSAGES;
        snprintf(g_driverMessages[index], DRIVER_MESSAGE_BYTES, "%s", message);

        if (g_alsoStderr)
            fprintf(stderr, "[vulkan] %s\n", message);
    }

    // Replaces the freeze that followed: a lost channel never finishes the frame Present waits for, and Vulkan has no
    // way back from VK_ERROR_DEVICE_LOST.
    void OnDeviceLost(const char* call)
    {
        if (!BeginReport())
            Break();

        AppendHeader("GPU LOST");
        Append("[crash] %s returned VK_ERROR_DEVICE_LOST: the GPU channel is gone and nothing more can be drawn\n", call);

        const uint32_t count = g_driverMessageCount.load(std::memory_order_relaxed);
        const uint32_t first = count > DRIVER_MESSAGES ? count - DRIVER_MESSAGES : 0;
        if (count == 0)
            Append("[crash] the driver reported no error message before it\n");
        for (uint32_t i = first; i < count; i++)
            Append("[crash] driver: %s\n", g_driverMessages[i % DRIVER_MESSAGES]);

        Append("[crash] ============== END ==============\n");
        WriteReport(true);
        Break();
    }
}

// The CPU report itself. Only the entry below calls it: once per process, after it has claimed the report, and on the
// report's own stack. Called by name from assembly, hence its linkage and attributes (LTO must keep the name).
extern "C" __attribute__((used, externally_visible, noinline)) void SwitchCrashWriteCpuReport(const void* threadExceptionDump)
{
    // Work from a copy: the next guest-window fault of any thread rewrites libnx's dump while this report is written.
    std::memcpy(&g_cpuDump, threadExceptionDump, sizeof(g_cpuDump));
    const ThreadExceptionDump* context = &g_cpuDump;

    // The fault first: if anything below fails, this line is still worth reading.
    AppendHeader("CPU EXCEPTION");
    Append("[crash] %s (0x%x); pc", ExceptionName(context->error_desc), context->error_desc);
    AppendAddress(context->pc.x);
    Append("; lr");
    AppendAddress(context->lr.x);
    Append("; far 0x%llx; esr 0x%08x; pstate 0x%08x\n", (unsigned long long)context->far.x, context->esr, context->pstate);

    // What Marathon's handler looked at before it gave up: the faulting instruction (the handler emulates the loads and
    // stores it can decode) and, for a fault in the guest window, the guest address.
    const uint64_t pc = context->pc.x;
    if ((pc & 3) == 0 && Readable(pc, 4))
        Append("[crash] instruction 0x%08x", *reinterpret_cast<const uint32_t*>(pc));
    else
        Append("[crash] instruction unreadable");
    const uint64_t faultAddress = context->far.x;
    if (g_guestBase != 0 && faultAddress >= g_guestBase && faultAddress - g_guestBase < PPC_MEMORY_SIZE)
        Append("; far is guest address 0x%08llx (guest window at 0x%llx)\n", (unsigned long long)(faultAddress - g_guestBase),
            (unsigned long long)g_guestBase);
    else
        Append("; far is outside the guest window (at 0x%llx)\n", (unsigned long long)g_guestBase);

    for (uint32_t i = 0; i < 28; i += 4)
    {
        Append("[crash] x%-2u %016llx  x%-2u %016llx  x%-2u %016llx  x%-2u %016llx\n",
            i, (unsigned long long)context->cpu_gprs[i].x, i + 1, (unsigned long long)context->cpu_gprs[i + 1].x,
            i + 2, (unsigned long long)context->cpu_gprs[i + 2].x, i + 3, (unsigned long long)context->cpu_gprs[i + 3].x);
    }
    Append("[crash] x28 %016llx  fp  %016llx  sp  %016llx\n", (unsigned long long)context->cpu_gprs[28].x,
        (unsigned long long)context->fp.x, (unsigned long long)context->sp.x);

    // Frame-pointer chain: [fp] is the caller's fp, [fp + 8] the return address.
    Append("[crash] frames");
    uint64_t fp = context->fp.x;
    for (uint32_t depth = 0; depth < BACKTRACE_DEPTH && fp != 0 && (fp & 7) == 0 && Readable(fp, 16); depth++)
    {
        const uint64_t next = reinterpret_cast<const uint64_t*>(fp)[0];
        const uint64_t ret = reinterpret_cast<const uint64_t*>(fp)[1];
        if (ret == 0)
            break;
        AppendAddress(ret);
        if (next <= fp)
            break;
        fp = next;
    }
    Append("\n");

    // Return addresses on the stack (the frame-pointer chain misses leaf and frame-less code, and the recompiled
    // functions keep no frame pointer at all).
    const uint64_t sp = context->sp.x & ~uint64_t(7);
    Append("[crash] stack");
    uint32_t found = 0;
    for (uint32_t i = 0; i < STACK_SCAN_WORDS && found < STACK_SCAN_MAX; i++)
    {
        const uint64_t at = sp + uint64_t(i) * 8;
        if ((at & 0xFFF) == 0 || i == 0)
        {
            if (!Readable(at, 8))
                break;
        }
        const uint64_t word = *reinterpret_cast<const uint64_t*>(at);
        if (InModule(word) && (word & 3) == 0)
        {
            AppendAddress(word);
            found++;
        }
    }
    Append("\n");

    if (Readable(sp, STACK_DUMP_BYTES))
    {
        for (size_t offset = 0; offset < STACK_DUMP_BYTES; offset += 0x20)
        {
            const uint64_t* q = reinterpret_cast<const uint64_t*>(sp + offset);
            Append("[crash]   sp+%03zx: %016llx %016llx %016llx %016llx\n", offset, (unsigned long long)q[0],
                (unsigned long long)q[1], (unsigned long long)q[2], (unsigned long long)q[3]);
        }
    }
    else
    {
        Append("[crash]   stack unreadable\n");
    }

    Append("[crash] ============== END ==============\n");

    // Not stderr: the thread that faulted may hold its lock. The caller breaks next (FatalFaultBreak).
    WriteReport(false);
}

// os::switch_crash::WriteCpuExceptionReport(const void* threadExceptionDump), x0 = the dump. Nothing it needs after the
// report stays on the shared exception stack, where another thread's handler may overwrite it meanwhile:
//  - The thread that claims the report (SwitchCrashReporting, never released) moves to the report's own stack, saves
//    its return address there, keeps the exception stack's sp in x19 (saved there too) and returns with them. The
//    handler keeps the values it breaks with in callee-saved registers, which the report preserves on that stack.
//  - A thread that finds the report taken (a second fatal fault, or a GPU report) sleeps in the svc itself, registers
//    only, for SECOND_REPORT_WAIT_NS so the first can finish, and returns: the handler then breaks with its own values.
__asm__(
    ".pushsection .bss.SwitchCrashStack, \"aw\", %nobits\n"
    ".balign 16\n"
    "SwitchCrashStack:\n"
    "    .space 0x8000\n"
    "SwitchCrashStackTop:\n"
    ".popsection\n"
    ".pushsection .text._ZN2os12switch_crash23WriteCpuExceptionReportEPKv, \"ax\", %progbits\n"
    ".weak _ZN2os12switch_crash23WriteCpuExceptionReportEPKv\n"
    ".type _ZN2os12switch_crash23WriteCpuExceptionReportEPKv, %function\n"
    ".p2align 2\n"
    "_ZN2os12switch_crash23WriteCpuExceptionReportEPKv:\n"
    "    cbz    x0, 3f\n"                        // No dump, no report.
    "    adrp   x9, SwitchCrashReporting\n"
    "    add    x9, x9, :lo12:SwitchCrashReporting\n"
    "    mov    w11, #1\n"
    "1:  ldaxrb w10, [x9]\n"                    // SwitchCrashReporting.exchange(true)
    "    cbnz   w10, 2f\n"
    "    stlxrb w12, w11, [x9]\n"
    "    cbnz   w12, 1b\n"
    "    mov    x10, sp\n"
    "    adrp   x9, SwitchCrashStackTop\n"
    "    add    x9, x9, :lo12:SwitchCrashStackTop\n"
    "    mov    sp, x9\n"
    "    stp    x19, x30, [sp, #-16]!\n"
    "    mov    x19, x10\n"                      // The exception stack's sp.
    "    bl     SwitchCrashWriteCpuReport\n"
    "    mov    x10, x19\n"
    "    ldp    x19, x30, [sp], #16\n"
    "    mov    sp, x10\n"
    "    ret\n"
    "2:  clrex\n"
    "    movz   x0, #0x5e00\n"                   // SECOND_REPORT_WAIT_NS
    "    movk   x0, #0xb2d0, lsl #16\n"
    "    svc    #0xb\n"                         // svcSleepThread
    "3:  ret\n"
    ".size _ZN2os12switch_crash23WriteCpuExceptionReportEPKv, .-_ZN2os12switch_crash23WriteCpuExceptionReportEPKv\n"
    ".popsection\n"
);

void os::switch_crash::Init(bool alsoStderr)
{
    g_alsoStderr = alsoStderr;
    g_guestBase = reinterpret_cast<uint64_t>(g_memory.base);
    plume::SetSwitchDriverMessageCallback(OnDriverMessage);
    plume::SetSwitchDeviceLostCallback(OnDeviceLost);
}

#endif
