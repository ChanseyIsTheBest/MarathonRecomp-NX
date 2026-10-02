#if defined(__spirv__) && defined(GAMMA_CORRECTION_PUSH_CONSTANTS)

// [Switch] SwitchGammaPushConstants (gamma_correction_push_ps.hlsl): the constants come in the 24-byte push
// constant range itself, which the driver serves from its root constant bank, instead of four loads through the
// shared constants pointer. Same values in the same layout; the arithmetic below is unchanged.
struct GammaPushConstants
{
    float Gamma;
    uint TextureDescriptorIndex;
    int2 ViewportOffset;
    int2 ViewportSize;
};

[[vk::push_constant]] ConstantBuffer<GammaPushConstants> g_GammaPushConstants;

Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);

#define g_Gamma g_GammaPushConstants.Gamma
#define g_TextureDescriptorIndex g_GammaPushConstants.TextureDescriptorIndex

#define g_ViewportOffset g_GammaPushConstants.ViewportOffset
#define g_ViewportSize g_GammaPushConstants.ViewportSize

#else

#include "../../../../tools/XenosRecomp/XenosRecomp/shader_common.h"

#ifdef __spirv__

#define g_Gamma vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 0)
#define g_TextureDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 4)

#define g_ViewportOffset vk::RawBufferLoad<int2>(g_PushConstants.SharedConstants + 8)
#define g_ViewportSize vk::RawBufferLoad<int2>(g_PushConstants.SharedConstants + 16)

#else

cbuffer SharedConstants : register(b2, space4)
{
    float g_Gamma;
    uint g_TextureDescriptorIndex;
    int2 g_ViewportOffset;
    int2 g_ViewportSize;
};

#endif

#endif

float4 shaderMain(in float4 position : SV_Position) : SV_Target
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[g_TextureDescriptorIndex];

    int2 movedPosition = int2(position.xy) - g_ViewportOffset;
    bool boxed = any(movedPosition < 0) || any(movedPosition >= g_ViewportSize);
    if (boxed) movedPosition = 0;

    float4 color = boxed ? 0.0 : texture.Load(int3(movedPosition, 0));
    color.rgb = pow(color.rgb, g_Gamma);
    return color;
}
