#pragma once

struct ShaderCacheEntry
{
    const uint64_t hash;
    const uint32_t dxilOffset;
    const uint32_t dxilSize;
    const uint32_t spirvOffset;
    const uint32_t spirvSize;
    const uint32_t airOffset;
    const uint32_t airSize;
    const uint32_t specConstantsMask;
#if defined(__SWITCH__)
    // [Switch] Written only by XenosRecomp with XenosRecomp-switch-perf.patch, which the Switch build applies; the
    // PC builds run the unpatched translator, whose rows end at specConstantsMask and the filename. __SWITCH__ is
    // defined for every file of the Switch build (the devkitA64 toolchain), so both libraries see one layout.

    // Float4 constant registers the shader can read, counted from c0 (the whole block, 224 pixel or
    // 256 vertex registers, when it indexes an array by a0 or aL). The renderer may upload only these.
    const uint32_t float4ConstantRegisters;
    // Texture2DArray slots read by the shadow gathers (bit s = slot s, as in g_GatherableSlots;
    // SPEC_CONSTANT_SHADOW_GATHER and _KNOWN). Sonic '06: 0x800 (g_smpCSM) or 0.
    const uint32_t gatherSlots;
    // SHADER_FLAG_* (tools/XenosRecomp/XenosRecomp/shader_common.h).
    const uint32_t flags;
    // The texture slots the shader can fetch from (bit s = slot s): its declared samplers (perf8).
    const uint32_t textureSlotsRead;
#endif
    char filename[256];
    struct GuestShader* guestShader;
};

extern ShaderCacheEntry g_shaderCacheEntries[];
extern const size_t g_shaderCacheEntryCount;

extern const uint8_t g_compressedDxilCache[];
extern const size_t g_dxilCacheCompressedSize;
extern const size_t g_dxilCacheDecompressedSize;

extern const uint8_t g_compressedAirCache[];
extern const size_t g_airCacheCompressedSize;
extern const size_t g_airCacheDecompressedSize;

extern const uint8_t g_compressedSpirvCache[];
extern const size_t g_spirvCacheCompressedSize;
extern const size_t g_spirvCacheDecompressedSize;
