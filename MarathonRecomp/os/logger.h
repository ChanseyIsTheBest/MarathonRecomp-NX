#pragma once

#include <source_location>

#if defined(__SWITCH__)
#include <atomic>
#include <cstdint>

// [Switch] With SwitchLog off, Log() drops every line, so the lines are not built at all: the check comes before the
// arguments are evaluated and before fmt::format runs (no argument of a LOG* call has side effects). Until the
// configuration is read the lines are kept, so they are formatted then. A conditional expression, not a statement,
// so every existing use (after an if, in an expression) stays valid.
#define LOG_IMPL(type, func, str)       (os::logger::IsEnabled() ? os::logger::Log(str, os::logger::ELogType::type, func) : void())
#define LOGF_IMPL(type, func, str, ...) (os::logger::IsEnabled() ? os::logger::Log(fmt::format(str, __VA_ARGS__), os::logger::ELogType::type, func) : void())
#else
#define LOG_IMPL(type, func, str)       os::logger::Log(str, os::logger::ELogType::type, func)
#define LOGF_IMPL(type, func, str, ...) os::logger::Log(fmt::format(str, __VA_ARGS__), os::logger::ELogType::type, func)
#endif

// Function-specific logging.

#define LOG(str)               LOG_IMPL(None, __func__, str)
#define LOG_WARNING(str)       LOG_IMPL(Warning, __func__, str)
#define LOG_ERROR(str)         LOG_IMPL(Error, __func__, str)

#if _DEBUG
#define LOG_UTILITY(str)       LOG_IMPL(Utility, __func__, str)
#else
#define LOG_UTILITY(str)       LOG_IMPL(Utility, __func__, str)
#endif

#define LOGF(str, ...)         LOGF_IMPL(None, __func__, str, __VA_ARGS__)
#define LOGF_WARNING(str, ...) LOGF_IMPL(Warning, __func__, str, __VA_ARGS__)
#define LOGF_ERROR(str, ...)   LOGF_IMPL(Error, __func__, str, __VA_ARGS__)

#if _DEBUG
#define LOGF_UTILITY(str, ...) LOGF_IMPL(Utility, __func__, str, __VA_ARGS__)
#else
#define LOGF_UTILITY(str, ...) LOGF_IMPL(Utility, __func__, str, __VA_ARGS__)
#endif

// Non-function-specific logging.

#define LOGN(str)               LOG_IMPL(None, "*", str)
#define LOGN_WARNING(str)       LOG_IMPL(Warning, "*", str)
#define LOGN_ERROR(str)         LOG_IMPL(Error, "*", str)

#if _DEBUG
#define LOGN_UTILITY(str)       LOG_IMPL(Utility, "*", str)
#else
#define LOGN_UTILITY(str)       LOG_IMPL(Utility, "*", str)
#endif

#define LOGFN(str, ...)         LOGF_IMPL(None, "*", str, __VA_ARGS__)
#define LOGFN_WARNING(str, ...) LOGF_IMPL(Warning, "*", str, __VA_ARGS__)
#define LOGFN_ERROR(str, ...)   LOGF_IMPL(Error, "*", str, __VA_ARGS__)

#if _DEBUG
#define LOGFN_UTILITY(str, ...) LOGF_IMPL(Utility, "*", str, __VA_ARGS__)
#else
#define LOGFN_UTILITY(str, ...) LOGF_IMPL(Utility, "*", str, __VA_ARGS__)
#endif

namespace os::logger
{
    enum class ELogType
    {
        None,
        Utility,
        Warning,
        Error
    };

    void Init();
    void Log(const std::string_view str, ELogType type = ELogType::None, const char* func = nullptr);

#if defined(__SWITCH__)
    // [Switch] SwitchLog: whether MarathonRecomp.log is written. Until this is called (after Config::Load) the lines
    // are kept in memory; then they are written to the file, or dropped with every later one.
    void SetFileEnabled(bool enabled);

    // stderr to sdmc:/switch/MarathonRecomp/stderr.log (line-buffered), with the build as its first line. Done once;
    // later calls only return true. Call it right after the configuration is read, before the threads that write to
    // stderr start: SwitchLog and the diagnostics' own switches turn it on in SwitchPerfInitDiagnostics, and any
    // other area whose switch writes to stderr (a profiler) may call it from its own init. False if the file could
    // not be opened.
    bool EnableStderrLog();
    bool IsStderrLogEnabled();

    // 0 = not decided yet (lines kept), 1 = written to the file, 2 = dropped. Read without a lock by IsEnabled().
    extern std::atomic<uint8_t> g_switchLogState;

    // False once SwitchLog turned out to be off: a line logged now would be dropped.
    inline bool IsEnabled()
    {
        return g_switchLogState.load(std::memory_order_relaxed) != 2;
    }
#endif
}
