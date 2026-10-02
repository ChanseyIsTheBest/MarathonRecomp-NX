// [Switch] Codegen area: the [Switch] keys of user/switch/config_codegen.inl, read once into the plain globals the
// recompiled code (the generated ppc_context.h, patches/XenonRecomp-switch-perf.patch) tests.
#if defined(__SWITCH__)

#include <os/switch_cpu_profiler.h>
#include <os/switch_perf_init.h>
#include <user/config.h>

// Declared by ppc_context.h: stwcx./stdcx. as relaxed compare-and-swaps (PPCStoreConditional32/64).
bool g_ppcRelaxedAtomics = false;

// Called from main() (SwitchPerfOnConfigLoaded) after Config::Load, before the guest kernel, the audio or any guest
// thread starts: nothing reads the global concurrently with this store.
void SwitchPerfInitCodegen()
{
    g_ppcRelaxedAtomics = Config::SwitchRelaxedAtomics;

    std::string line = fmt::format("[codegen] SwitchRelaxedAtomics={} guest fences={} fused multiply-add={}\n",
        int(g_ppcRelaxedAtomics),
#if defined(PPC_SYNC)
        "sync/lwsync/eieio as dmb",
#else
        "none (ppc/ generated before the fences)",
#endif
#if defined(PPC_EXPLICIT_FMA)
        "explicit (guest fused ops only)"
#else
        "compiler contraction"
#endif
    );
    os::switch_cpu_profiler::WriteLog(line);
}

#endif
