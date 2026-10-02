#pragma once

#if defined(__SWITCH__)

// [Switch] Guest functions replaced by native code (os/switch/perf/native_*.cpp). Each flag is one [Switch] key of
// user/switch/config_native.inl, read once by SwitchPerfInitNative (native_init.cpp) right after the config loads
// and before any guest code runs; the hooks read only these globals. Off, the recompiled code runs.
//
// Every hook leaves guest memory above the stack pointer, r1 and the return value exactly as the recompiled code
// does. It does not repeat the recompiled code's stores below the stack pointer (its own frame and register save
// slots, dead once it returns) nor its values in volatile registers other than r3, which callers never read after
// the call (the ABI; the same assumption as the memcpy/memset hooks in misc_impl.cpp). Anything a hook does not
// reproduce with certainty runs the recompiled code instead.
extern bool g_switchNativeRtti;                 // SwitchNativeRtti: type_info::operator==, __RTtypeid
extern bool g_switchNativeDynamicCast;          // SwitchNativeDynamicCast: __RTDynamicCast (single inheritance)
extern bool g_switchVerifyNativeDynamicCast;    // SwitchVerifyNativeDynamicCast: also run the game's, compare
extern bool g_switchNativeShaderConstants;      // SwitchNativeShaderConstants: D3D float constant setters
extern bool g_switchNativeCrtStrings;           // SwitchNativeCrtStrings: _stricmp, strncpy, strchr, memcpy variant
extern bool g_switchNativeInflate;              // SwitchNativeInflate: the archive loader's one-shot zlib inflate
extern bool g_switchVerifyNativeInflate;        // SwitchVerifyNativeInflate: also run the game's inflate, compare
extern bool g_switchModifierCache;              // SwitchModifierCache: CSD aspect-ratio modifier lookups cached
extern bool g_switchNativeVectorMath;           // SwitchNativeVectorMath: matrix product, box projection
extern bool g_switchVerifyNativeVectorMath;     // SwitchVerifyNativeVectorMath: also run the game's, compare

#endif
