// [Switch] keys of the codegen area. Included by user/config_def.h inside its __SWITCH__ block.
// CONFIG_DEFINE_HIDDEN("Switch", type, Name, default, requiresRestart);
// Read once by SwitchPerfInitCodegen (os/switch/perf/codegen_switch.cpp); the recompiled code reads a plain global.

// The game's stwcx./stdcx. (guest atomics) as relaxed compare-and-swaps, as on PowerPC, instead of each carrying a
// full barrier. The guest's own sync/lwsync/eieio stay fences, and each compare-and-swap stays a compiler barrier.
// Off: it changes the memory ordering the game's threads see (back to PowerPC's), and Marathon has only 54 of
// them; turn it on after an on-console session (audio through a stage's first event) shows no difference.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchRelaxedAtomics, true, true);
