#if defined(__SWITCH__)

#include <os/switch_perf_init.h>

void SwitchPerfOnConfigLoaded()
{
    // Diagnostics first, so that the logs the other areas write have somewhere to go.
    if (SwitchPerfInitDiagnostics)
        SwitchPerfInitDiagnostics();
    if (SwitchPerfInitCodegen)
        SwitchPerfInitCodegen();
    if (SwitchPerfInitKernel)
        SwitchPerfInitKernel();
    if (SwitchPerfInitNative)
        SwitchPerfInitNative();
    if (SwitchPerfInitAudio)
        SwitchPerfInitAudio();
    if (SwitchPerfInitRenderer)
        SwitchPerfInitRenderer();
}

#endif
