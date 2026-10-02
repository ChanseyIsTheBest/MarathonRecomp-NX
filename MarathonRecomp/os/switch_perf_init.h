#pragma once

#if defined(__SWITCH__)

// [Switch] Called once from main() right after Config::Load(), before the guest kernel, audio or renderer
// start. Each area reads its [Switch] keys into plain globals here (hot paths must not look the config up).
// The per-area functions are weak: an area that has nothing to set up simply does not define its own.
void SwitchPerfOnConfigLoaded();

void SwitchPerfInitCodegen() __attribute__((weak));
void SwitchPerfInitKernel() __attribute__((weak));
void SwitchPerfInitNative() __attribute__((weak));
void SwitchPerfInitAudio() __attribute__((weak));
void SwitchPerfInitDiagnostics() __attribute__((weak));
void SwitchPerfInitRenderer() __attribute__((weak));

#endif
