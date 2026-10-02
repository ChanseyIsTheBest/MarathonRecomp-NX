#if defined(__SWITCH__)

#include <os/switch/perf/native_hooks.h>
#include <os/switch_perf_init.h>
#include <user/config.h>

// Initialised to the keys' defaults (config_native.inl); SwitchPerfInitNative sets them from the config before any
// guest code runs, and nothing changes them afterwards.
bool g_switchNativeRtti = true;
bool g_switchNativeDynamicCast = true;
bool g_switchVerifyNativeDynamicCast = false;
bool g_switchNativeShaderConstants = true;
bool g_switchNativeCrtStrings = true;
bool g_switchNativeInflate = true;
bool g_switchVerifyNativeInflate = false;
bool g_switchModifierCache = true;
bool g_switchNativeVectorMath = true;
bool g_switchVerifyNativeVectorMath = false;

void SwitchPerfInitNative()
{
    g_switchNativeRtti = Config::SwitchNativeRtti;
    g_switchNativeDynamicCast = Config::SwitchNativeDynamicCast;
    g_switchVerifyNativeDynamicCast = Config::SwitchVerifyNativeDynamicCast;
    g_switchNativeShaderConstants = Config::SwitchNativeShaderConstants;
    g_switchNativeCrtStrings = Config::SwitchNativeCrtStrings;
    g_switchNativeInflate = Config::SwitchNativeInflate;
    g_switchVerifyNativeInflate = Config::SwitchVerifyNativeInflate;
    g_switchModifierCache = Config::SwitchModifierCache;
    g_switchNativeVectorMath = Config::SwitchNativeVectorMath;
    g_switchVerifyNativeVectorMath = Config::SwitchVerifyNativeVectorMath;
}

#endif
