#pragma once

#include "hook_event.h"

#if defined(__SWITCH__)
#include <atomic>
#endif

class LoadingPatches
{
public:
    static inline std::vector<IHookEvent*> Events{};

#if defined(__SWITCH__)
    // [Switch] Counts the HUDLoading::Update calls of loading screens that have not finished (loading_patches.cpp).
    // The renderer saves its pipeline cache once it stops changing (gpu/video.cpp, UpdatePipelineCacheSaving).
    static inline std::atomic<uint32_t> s_activeUpdates{ 0 };
#endif
};
