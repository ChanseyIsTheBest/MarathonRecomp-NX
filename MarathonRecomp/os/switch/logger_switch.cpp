#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <os/logger.h>
#include <user/config.h>

#if __has_include(<switch_build_id.h>)
#include <switch_build_id.h>
#endif
#ifndef MARATHON_RECOMP_SWITCH_BUILD_ID
#define MARATHON_RECOMP_SWITCH_BUILD_ID ""
#endif
#ifndef MARATHON_RECOMP_SWITCH_BUILD_OPTIONS
#define MARATHON_RECOMP_SWITCH_BUILD_OPTIONS ""
#endif

extern const char* g_versionString; // version.cpp

// [Switch] SwitchLog. The game's log goes to MarathonRecomp.log and stderr to stderr.log, both only when asked for:
// otherwise neither file is opened, the first lines (written before the configuration is read) are dropped, and every
// later LOG* call returns before it formats anything (os/logger.h).

std::atomic<uint8_t> os::logger::g_switchLogState{ 0 };

namespace
{
    constexpr const char* kLogDirectory = "sdmc:/switch/MarathonRecomp";
    constexpr const char* kLogPath = "sdmc:/switch/MarathonRecomp/MarathonRecomp.log";
    constexpr const char* kStderrPath = "sdmc:/switch/MarathonRecomp/stderr.log";
    constexpr uint32_t kEarlyFlushLines = 96;
    constexpr uint32_t kFlushInterval = 64;

    constexpr uint8_t STATE_UNDECIDED = 0;
    constexpr uint8_t STATE_FILE = 1;
    constexpr uint8_t STATE_DROPPED = 2;

    std::mutex g_logMutex;
    FILE* g_logFile = nullptr;
    uint32_t g_logLineCount = 0;

    // Before SetFileEnabled (Config::Load has not run yet): the lines, up to this many bytes.
    constexpr size_t kEarlyBytes = 16 * 1024;
    std::string g_earlyLines;

    std::mutex g_stderrMutex;
    bool g_stderrRedirected = false;

    std::string FormatLogLine(std::string_view str, os::logger::ELogType type, const char* func)
    {
        const char* prefix = "";

        switch (type)
        {
        case os::logger::ELogType::Utility:
            prefix = "utility";
            break;
        case os::logger::ELogType::Warning:
            prefix = "warning";
            break;
        case os::logger::ELogType::Error:
            prefix = "error";
            break;
        default:
            break;
        }

        if (func != nullptr && prefix[0] != '\0')
            return fmt::format("[{}] [{}] {}", func, prefix, str);
        if (func != nullptr)
            return fmt::format("[{}] {}", func, str);
        if (prefix[0] != '\0')
            return fmt::format("[{}] {}", prefix, str);

        return std::string(str);
    }
}

void os::logger::Init()
{
    std::lock_guard lock(g_logMutex);

    if (g_logFile != nullptr)
    {
        fclose(g_logFile);
        g_logFile = nullptr;
    }

    g_logLineCount = 0;

    std::error_code ec;
    std::filesystem::create_directories(kLogDirectory, ec);
}

void os::logger::SetFileEnabled(bool enabled)
{
    std::lock_guard lock(g_logMutex);

    if (enabled && g_logFile == nullptr)
    {
        g_logFile = fopen(kLogPath, "w");
        if (g_logFile != nullptr)
        {
            setvbuf(g_logFile, nullptr, _IOFBF, 64 * 1024);
            fputs(g_earlyLines.c_str(), g_logFile);
            fflush(g_logFile);
        }
    }

    g_switchLogState.store(g_logFile != nullptr ? STATE_FILE : STATE_DROPPED, std::memory_order_relaxed);

    g_earlyLines.clear();
    g_earlyLines.shrink_to_fit();
}

void os::logger::Log(const std::string_view str, ELogType type, const char* func)
{
    // Fast path for direct callers (the LOG* macros check this themselves): SwitchLog is off.
    if (g_switchLogState.load(std::memory_order_relaxed) == STATE_DROPPED)
        return;

    const auto line = FormatLogLine(str, type, func);
    std::lock_guard lock(g_logMutex);

    // Decided under the lock: SetFileEnabled may have run since the check above.
    const uint8_t state = g_switchLogState.load(std::memory_order_relaxed);
    if (state == STATE_UNDECIDED)
    {
        if (g_earlyLines.size() + line.size() + 1 <= kEarlyBytes)
        {
            g_earlyLines += line;
            g_earlyLines += '\n';
        }
        return;
    }

    if (state == STATE_FILE && g_logFile != nullptr)
    {
        fputs(line.c_str(), g_logFile);
        fputc('\n', g_logFile);

        ++g_logLineCount;

        if (type == ELogType::Error ||
            type == ELogType::Warning ||
            g_logLineCount <= kEarlyFlushLines ||
            (g_logLineCount % kFlushInterval) == 0)
        {
            fflush(g_logFile);
        }
    }
}

bool os::logger::EnableStderrLog()
{
    std::lock_guard lock(g_stderrMutex);
    if (g_stderrRedirected)
        return true;

    // Line-buffered: the messages are rare, and a crash loses nothing. Reports go through
    // os::switch_cpu_profiler::WriteLog, in one write each. The directory exists: Init() creates it.
    if (freopen(kStderrPath, "w", stderr) == nullptr)
        return false;

    setvbuf(stderr, nullptr, _IOLBF, 1024);
    g_stderrRedirected = true;

    // First line: which build wrote this log (tools/build-switch.sh sets the ID at configure time).
    fprintf(stderr, "Build: %s, Marathon Recompiled %s, options: %s\n",
        MARATHON_RECOMP_SWITCH_BUILD_ID[0] != '\0' ? MARATHON_RECOMP_SWITCH_BUILD_ID : "unnamed", g_versionString,
        MARATHON_RECOMP_SWITCH_BUILD_OPTIONS[0] != '\0' ? MARATHON_RECOMP_SWITCH_BUILD_OPTIONS : "none");

    // Second line: every [Switch] key with its value (the configuration is loaded by now), so that a log says which
    // switches its test set ran with.
    std::string line = "[Switch] config:";
    for (const IConfigDef* def : g_configDefinitions)
    {
        if (def->GetSection() != "Switch")
            continue;

        line += ' ';
        line += def->GetName();
        line += '=';
        line += def->ToString(false);
    }
    line += '\n';
    fputs(line.c_str(), stderr);
    return true;
}

bool os::logger::IsStderrLogEnabled()
{
    std::lock_guard lock(g_stderrMutex);
    return g_stderrRedirected;
}
