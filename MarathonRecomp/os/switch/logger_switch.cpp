#include <string_view>

#include <os/logger.h>

// Logging is disabled on Switch release builds: no SD-card log file, no stderr
// redirect, no crash-report file. Every logging call compiles to a no-op.

void os::logger::Init()
{
}

void os::logger::Log(const std::string_view, ELogType, const char*)
{
}
