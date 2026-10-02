// [Switch] keys of the native area. Included by user/config_def.h inside its __SWITCH__ block.
// CONFIG_DEFINE_HIDDEN("Switch", type, Name, default, requiresRestart);
// Read once at startup (os/switch/perf/native_init.cpp); each replaces guest code with native code that leaves the
// game's memory and results exactly as they were (os/switch/perf/native_hooks.h).

// type_info comparisons (1,124 call sites) and typeid run as native code; the throwing cases run the game's code.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeRtti, true, true);
// dynamic_cast (__RTDynamicCast, 1,157 call sites) of single-inheritance classes runs as native code, with the
// results for the class descriptors in the executable remembered; multiple/virtual inheritance and throwing casts
// run the game's code.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeDynamicCast, true, true);
// Every native dynamic_cast also runs the game's code and is compared with it ("[rtti]" lines in stderr.log).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyNativeDynamicCast, false, true);
// The D3D device's vertex/pixel shader float constant setters run as native code (a copy and a flag update).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeShaderConstants, true, true);
// The game's _stricmp, strncpy, strchr and its word/byte memcpy run as native code (overlapping copies run the game's).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeCrtStrings, true, true);
// The archive loader's zlib decompression runs as native zlib; a stream it does not decode completely runs the
// game's decoder.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeInflate, true, true);
// Every native decompression is also done by the game's decoder and compared with it ("[inflate]" lines).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyNativeInflate, false, true);
// The UI's aspect-ratio modifier lookups (once per scene, cast node and cast drawn) are cached.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchModifierCache, true, true);
// The 4x4 matrix product (sub_82168C48, sub_82272648) and the box-corner projection (sub_825A1490) with their vector
// registers as locals: the same operations in the same order (os/switch/perf/native_vector_math.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeVectorMath, true, true);
// Verification: also run the recompiled ones on every call and compare memory and registers ("[native vector]
// MISMATCH" lines in stderr.log; slower).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyNativeVectorMath, false, true);
