#if defined(__SWITCH__)

#include <switch.h>

#include <apu/audio_switch.h>
#include <os/switch_perf_init.h>
#include <user/config.h>

#include <cstdio>

// Initialised to the keys' defaults (config_audio.inl); SwitchPerfInitAudio sets them from the config.
bool g_switchAudioOut = true;
bool g_switchAudioThreadCores = true;
int32_t g_switchAudioThreadCore = 2;
bool g_switchXmaEventWait = true;
bool g_switchVibrationDedupe = true;

std::atomic<uint32_t> g_switchAudioUnderruns{ 0 };

void SwitchPerfInitAudio()
{
    g_switchAudioOut = Config::SwitchAudioOut;
    g_switchAudioThreadCores = Config::SwitchAudioThreadCores;
    g_switchAudioThreadCore = Config::SwitchAudioThreadCore;
    g_switchXmaEventWait = Config::SwitchXmaEventWait;
    g_switchVibrationDedupe = Config::SwitchVibrationDedupe;

    // No fence check: the pump runs the game's mixer (guest code), but the recompiled code lowers every sync,
    // lwsync and eieio to a real fence on ARM (ppc_context.h), and the pump and the decoders, like every pthread
    // here, already run on any of the process's cores without this key (libnx's pthread_create gives each one the
    // whole process core mask). The key only narrows the audio threads to one core.
    if (g_switchAudioThreadCores)
    {
        // The cores the process may use (0x7 for an application: cores 0-2).
        uint64_t processCores = 0;
        if (R_FAILED(svcGetInfo(&processCores, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
            processCores = 0;

        const int32_t core = g_switchAudioThreadCore;
        if (core < 0 || core > 3 || (processCores & (uint64_t(1) << core)) == 0)
        {
            fprintf(stderr, "[Switch] SwitchAudioThreadCores: core %d is not one of the process's cores (mask 0x%X); "
                "the audio threads keep every core of the process.\n", int(core), unsigned(processCores));
            g_switchAudioThreadCores = false;
        }
    }

    fprintf(stderr, "[Switch] audio: SwitchAudioOut=%d SwitchAudioThreadCores=%d (core %d) SwitchXmaEventWait=%d "
        "SwitchVibrationDedupe=%d\n", int(g_switchAudioOut), int(g_switchAudioThreadCores), int(g_switchAudioThreadCore),
        int(g_switchXmaEventWait), int(g_switchVibrationDedupe));
}

void SwitchAudioSetCurrentThreadCore()
{
    if (!g_switchAudioThreadCores)
        return;

    // Preferred core and the only allowed one. Without the key the pump and the XMA decoders keep libnx's whole
    // process mask (they start on core 0 and move when another core idles or on a yield), so a decoder pass and a
    // mixer call can overlap; the release/acquire ring offsets and the stream mutexes make that safe. Here they
    // all share this one core, off the game's main core 0: at priority 0x2B Horizon does not time-slice them, so
    // they no longer overlap one another.
    const int32_t core = g_switchAudioThreadCore;
    const Result rc = svcSetThreadCoreMask(threadGetCurHandle(), core, 1u << core);
    if (R_FAILED(rc))
        fprintf(stderr, "[Switch] SwitchAudioThreadCores: svcSetThreadCoreMask(%d) failed: 0x%X\n", int(core), unsigned(rc));
}

#endif
