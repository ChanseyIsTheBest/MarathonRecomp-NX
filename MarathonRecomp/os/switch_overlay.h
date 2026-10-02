#pragma once

#include <cstdint>

#if defined(__SWITCH__)

// [Switch] SwitchOverlayFps. Publishes the game's FPS and render resolution where Status Monitor Overlay (and its
// forks, such as Horizon OC Monitor) read them: an NX-FPS block in SaltyNX's shared memory. NX-FPS itself cannot
// provide it for this port, because it hooks nvnQueuePresentTexture / eglSwapBuffers / vkQueuePresentKHR in the SDK,
// and this NRO links its own Vulkan driver. Does nothing without SaltyNX. Port of UnleashedRecomp-NX's module (from
// nfsmw-nx's sdk/src/ui/switch_saltynx.cpp); see os/switch/perf/overlay_switch.cpp.
namespace os::switch_overlay
{
    // Starts the once-per-second publisher thread (connects to SaltyNX, keeps the block alive), only when SaltyNX is
    // installed. It makes no IPC before the game has presented its first frames.
    void Start();

    // Call on every present, from the presenting thread, with the resolution the game renders at. One relaxed load
    // when the overlay was not started.
    void OnPresent(uint32_t width, uint32_t height);
}

#endif
