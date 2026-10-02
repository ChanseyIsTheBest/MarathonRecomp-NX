#pragma once

#if defined(__SWITCH__)

#include <cstddef>
#include <cstdint>

// [Switch] Static analyses of the SPIR-V the renderer hands to the driver (gpu/video.cpp, renderer stage 3). Each
// answers conservatively: a module it cannot read, or an instruction it does not know, counts as "the shader may do
// that", which only turns the optimisation that asked off.
namespace switch_spirv
{
    // True when the module reads its shader constants through the dynamic uniform buffers of descriptor set uboSet
    // (it has a DescriptorSet uboSet decoration), or reads none through pointers (no PhysicalStorageBufferAddresses
    // capability). Only meaningful for modules whose every constant read is either behind
    // SPEC_CONSTANT_CONSTANTS_UBO (the translated shaders, shader_common.h) or has no pointer path at all.
    bool ReadsConstantsThroughUbo(const void* data, size_t size, uint32_t uboSet);

    // Pixel shaders: true when the module cannot write depth, stencil or the sample mask, write memory other than its
    // own outputs and variables (no image write, no atomic, no store through a buffer or pointer) and has at most
    // allowedKills kill instructions (OpKill, OpTerminateInvocation, OpDemoteToHelperInvocation). Without colour
    // targets, and in a pipeline where those kills are dead, such a pixel shader has no effect at all.
    bool RemovableInDepthOnlyPass(const void* data, size_t size, uint32_t allowedKills);

    // Bit N set for each stage input at location N (< 32) the module may read. ~0u when unsure.
    uint32_t InputLocationsRead(const void* data, size_t size);
}

#endif
