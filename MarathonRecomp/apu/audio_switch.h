#pragma once

#if defined(__SWITCH__)

#include <atomic>
#include <cstdint>

// [Switch] Audio, XMA and input switches. SwitchPerfInitAudio() (os/switch/perf/audio_switch.cpp) reads the
// [Switch] keys into these once, right after Config::Load() and before the audio system starts; the audio
// pump, the XMA decoders and the HID driver read only these.
extern bool g_switchAudioOut;           // SwitchAudioOut: game audio through audout, fed by the pump thread.
extern bool g_switchAudioThreadCores;   // SwitchAudioThreadCores: pump and XMA decoders pinned to one core.
extern int32_t g_switchAudioThreadCore; // SwitchAudioThreadCore: the core they move to (2).
extern bool g_switchXmaEventWait;       // SwitchXmaEventWait: XMA decoders sleep until they can make progress.
extern bool g_switchVibrationDedupe;    // SwitchVibrationDedupe: unchanged vibration values are not re-sent.

// Times audout ran out of blocks to play since start (a short gap each). For reports; relaxed reads.
extern std::atomic<uint32_t> g_switchAudioUnderruns;

// Pins the calling thread (audio pump or XMA decoder) to SwitchAudioThreadCore when SwitchAudioThreadCores is
// on. Without it they keep libnx's whole process core mask and may overlap one another on different cores; with
// it every audio thread shares that single core, so they no longer run at the same time as each other (Horizon
// does not time-slice priority 0x2B).
void SwitchAudioSetCurrentThreadCore();

#endif
