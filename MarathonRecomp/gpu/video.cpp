#include "video.h"

#include "imgui/imgui_common.h"
#include "imgui/imgui_snapshot.h"
#include "imgui/imgui_font_builder.h"

#include <app.h>
#include <bc_diff.h>
#include <cpu/guest_thread.h>
#include <cstdint>
#include <cstdio>
#include <decompressor.h>
#include <kernel/function.h>
#include <kernel/heap.h>
#include <hid/hid.h>
#include <kernel/memory.h>
#include <kernel/xdbf.h>
#include <plume_render_interface.h>
#include <res/bc_diff/button_bc_diff.bin.h>
#include <res/font/im_font_atlas.dds.h>
#include <shader/shader_cache.h>
#include <Marathon.h>
#include <ui/achievement_menu.h>
#include <ui/achievement_overlay.h>
#include <ui/button_window.h>
#include <ui/fader.h>
#include <ui/imgui_utils.h>
#include <ui/installer_wizard.h>
#include <ui/message_window.h>
#include <ui/options_menu.h>
#include <ui/game_window.h>
#include <ui/black_bar.h>
#include <patches/aspect_ratio_patches.h>
#include <user/config.h>
#include <user/paths.h>
#include <sdl_listener.h>
#include <xxHashMap.h>
#include <os/process.h>

#if defined(__SWITCH__)
#include <apu/audio_switch.h>
#include <os/switch_cpu_profiler.h>
#include <os/switch_overlay.h>
#include <os/switch_stall_watch.h>
#include <patches/loading_patches.h>
#include <pthread.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#endif

#if defined(ASYNC_PSO_DEBUG) || defined(PSO_CACHING)
#include <magic_enum/magic_enum.hpp>
#endif

#define MARATHON_RECOMP
#include "../../tools/XenosRecomp/XenosRecomp/shader_common.h"

#if defined(__SWITCH__)
#include <os/switch/perf/spirv_analysis_switch.h>
#endif

#ifdef MARATHON_RECOMP_D3D12
#include "shader/hlsl/blend_color_alpha_ps.hlsl.dxil.h"
#include "shader/hlsl/conditional_survey_ps.hlsl.dxil.h"
#include "shader/hlsl/copy_vs.hlsl.dxil.h"
#include "shader/hlsl/copy_color_ps.hlsl.dxil.h"
#include "shader/hlsl/copy_depth_ps.hlsl.dxil.h"
#include "shader/hlsl/csd_filter_ps.hlsl.dxil.h"
#include "shader/hlsl/csd_no_tex_vs.hlsl.dxil.h"
#include "shader/hlsl/csd_vs.hlsl.dxil.h"
#include "shader/hlsl/enhanced_burnout_blur_vs.hlsl.dxil.h"
#include "shader/hlsl/enhanced_burnout_blur_ps.hlsl.dxil.h"
#include "shader/hlsl/gamma_correction_ps.hlsl.dxil.h"
#include "shader/hlsl/gaussian_blur_3x3.hlsl.dxil.h"
#include "shader/hlsl/gaussian_blur_5x5.hlsl.dxil.h"
#include "shader/hlsl/gaussian_blur_7x7.hlsl.dxil.h"
#include "shader/hlsl/gaussian_blur_9x9.hlsl.dxil.h"
#include "shader/hlsl/imgui_ps.hlsl.dxil.h"
#include "shader/hlsl/imgui_vs.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_color_2x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_color_4x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_color_8x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_depth_2x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_depth_4x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_depth_8x.hlsl.dxil.h"
#endif

#ifdef MARATHON_RECOMP_METAL
#include "shader/msl/blend_color_alpha_ps.metal.metallib.h"
#include "shader/msl/conditional_survey_ps.metal.metallib.h"
#include "shader/msl/copy_vs.metal.metallib.h"
#include "shader/msl/copy_color_ps.metal.metallib.h"
#include "shader/msl/copy_depth_ps.metal.metallib.h"
#include "shader/msl/csd_filter_ps.metal.metallib.h"
#include "shader/msl/csd_no_tex_vs.metal.metallib.h"
#include "shader/msl/csd_vs.metal.metallib.h"
#include "shader/msl/enhanced_burnout_blur_vs.metal.metallib.h"
#include "shader/msl/enhanced_burnout_blur_ps.metal.metallib.h"
#include "shader/msl/gamma_correction_ps.metal.metallib.h"
#include "shader/msl/gaussian_blur_3x3.metal.metallib.h"
#include "shader/msl/gaussian_blur_5x5.metal.metallib.h"
#include "shader/msl/gaussian_blur_7x7.metal.metallib.h"
#include "shader/msl/gaussian_blur_9x9.metal.metallib.h"
#include "shader/msl/imgui_ps.metal.metallib.h"
#include "shader/msl/imgui_vs.metal.metallib.h"
#include "shader/msl/resolve_msaa_color_2x.metal.metallib.h"
#include "shader/msl/resolve_msaa_color_4x.metal.metallib.h"
#include "shader/msl/resolve_msaa_color_8x.metal.metallib.h"
#include "shader/msl/resolve_msaa_depth_2x.metal.metallib.h"
#include "shader/msl/resolve_msaa_depth_4x.metal.metallib.h"
#include "shader/msl/resolve_msaa_depth_8x.metal.metallib.h"
#endif

#include "shader/hlsl/blend_color_alpha_ps.hlsl.spirv.h"
#include "shader/hlsl/conditional_survey_ps.hlsl.spirv.h"
#include "shader/hlsl/copy_vs.hlsl.spirv.h"
#include "shader/hlsl/copy_color_ps.hlsl.spirv.h"
#include "shader/hlsl/copy_depth_ps.hlsl.spirv.h"
#include "shader/hlsl/csd_filter_ps.hlsl.spirv.h"
#include "shader/hlsl/csd_no_tex_vs.hlsl.spirv.h"
#include "shader/hlsl/csd_vs.hlsl.spirv.h"
#include "shader/hlsl/enhanced_burnout_blur_vs.hlsl.spirv.h"
#include "shader/hlsl/enhanced_burnout_blur_ps.hlsl.spirv.h"
#include "shader/hlsl/gamma_correction_ps.hlsl.spirv.h"
#include "shader/hlsl/gaussian_blur_3x3.hlsl.spirv.h"
#include "shader/hlsl/gaussian_blur_5x5.hlsl.spirv.h"
#include "shader/hlsl/gaussian_blur_7x7.hlsl.spirv.h"
#include "shader/hlsl/gaussian_blur_9x9.hlsl.spirv.h"
#include "shader/hlsl/imgui_ps.hlsl.spirv.h"
#include "shader/hlsl/imgui_vs.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_color_2x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_color_4x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_color_8x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_depth_2x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_depth_4x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_depth_8x.hlsl.spirv.h"

#if defined(__SWITCH__)
// [Switch] Variants of app shaders that only tools/generate-switch-app-shaders.sh builds (SPIR-V). Without
// them (a build that compiles the app shaders itself), the options using them stay off.
#if __has_include("shader/hlsl/conditional_survey_store_ps.hlsl.spirv.h")
#include "shader/hlsl/conditional_survey_store_ps.hlsl.spirv.h"
#define MARATHON_RECOMP_SWITCH_SURVEY_STORE_SHADER
#endif
#if __has_include("shader/hlsl/gamma_correction_push_ps.hlsl.spirv.h")
#include "shader/hlsl/gamma_correction_push_ps.hlsl.spirv.h"
#define MARATHON_RECOMP_SWITCH_GAMMA_PUSH_SHADER
#endif
#if __has_include("shader/hlsl/conditional_survey_slots_ps.hlsl.spirv.h")
#include "shader/hlsl/conditional_survey_slots_ps.hlsl.spirv.h"
#define MARATHON_RECOMP_SWITCH_SURVEY_SLOTS_SHADER
#endif
#endif

#ifdef _WIN32
extern "C"
{
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace plume
{
#ifdef MARATHON_RECOMP_D3D12
    extern std::unique_ptr<RenderInterface> CreateD3D12Interface();
#endif
#ifdef MARATHON_RECOMP_METAL
extern std::unique_ptr<RenderInterface> CreateMetalInterface();
#endif
#ifdef PLUME_SDL_VULKAN_ENABLED
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow sdlWindow);
#else
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif

    static std::unique_ptr<RenderInterface> CreateVulkanInterfaceWrapper() {
#if defined(__SWITCH__)
        const auto userPath = GetUserPath();
        SwitchFrameGenerationConfig frameGeneration;
        frameGeneration.enabled = Config::FrameGeneration;
        frameGeneration.shaderPath = (userPath / "lsfg" / "Lossless.dll").string();
        frameGeneration.pipelineCachePath = (userPath / "cache" / "lsfg-vk-pipeline-cache.bin").string();
        frameGeneration.flowScale = Config::FrameGenerationFlowScale;
        frameGeneration.performanceMode = Config::FrameGenerationPerformanceMode;
        ConfigureSwitchFrameGeneration(frameGeneration);
#endif
#ifdef PLUME_SDL_VULKAN_ENABLED
        return CreateVulkanInterface(GameWindow::s_renderWindow);
#else
        return CreateVulkanInterface();
#endif
    }
}

using namespace plume;

#pragma pack(push, 1)
struct PipelineState
{
    GuestShader* vertexShader = nullptr;
    GuestShader* pixelShader = nullptr;
    GuestVertexDeclaration* vertexDeclaration = nullptr;
    bool zEnable = true;
    bool zWriteEnable = true;
    bool stencilEnable = false;
    bool stencilTwoSided = false;
    RenderBlend srcBlend = RenderBlend::ONE;
    RenderBlend destBlend = RenderBlend::ZERO;
    RenderCullMode cullMode = RenderCullMode::NONE;
    RenderFrontFace frontFace = RenderFrontFace::CLOCKWISE;
    RenderComparisonFunction zFunc = RenderComparisonFunction::LESS;
    RenderComparisonFunction stencilFunc = RenderComparisonFunction::ALWAYS;
    RenderStencilOp stencilFail = RenderStencilOp::KEEP;
    RenderStencilOp stencilZFail = RenderStencilOp::KEEP;
    RenderStencilOp stencilPass = RenderStencilOp::KEEP;
    RenderComparisonFunction stencilFuncCCW = RenderComparisonFunction::ALWAYS;
    RenderStencilOp stencilFailCCW = RenderStencilOp::KEEP;
    RenderStencilOp stencilZFailCCW = RenderStencilOp::KEEP;
    RenderStencilOp stencilPassCCW = RenderStencilOp::KEEP;
    uint32_t stencilMask = 0xFFFFFFFF;
    uint32_t stencilWriteMask = 0xFFFFFFFF;
    uint32_t stencilRef = 0;
    bool alphaBlendEnable = false;
    RenderBlendOperation blendOp = RenderBlendOperation::ADD;
    float slopeScaledDepthBias = 0.0f;
    int32_t depthBias = 0;
    RenderBlend srcBlendAlpha = RenderBlend::ONE;
    RenderBlend destBlendAlpha = RenderBlend::ZERO;
    RenderBlendOperation blendOpAlpha = RenderBlendOperation::ADD;
    uint32_t colorWriteEnable = uint32_t(RenderColorWriteEnable::ALL);
    RenderPrimitiveTopology primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
    uint8_t vertexStrides[16]{};
    RenderFormat renderTargetFormat{};
    RenderFormat depthStencilFormat{};
    RenderSampleCounts sampleCount = RenderSampleCount::COUNT_1;
    bool enableAlphaToCoverage = false;
    bool enableConditionalSurvey = false;
    uint32_t specConstants = 0;
#if defined(__SWITCH__)
    // [Switch] SwitchCoverageHandOver: the draw also marks the pixels it writes in an 8-bit stencil buffer of its own
    // (CreateGraphicsPipeline). Only ever set in a sanitized copy (TryCoverageHandOver); the render thread's state keeps 0.
    uint8_t coverageStencil = 0;
    // [Switch] SwitchStableFramebuffers: KEPT_COLOR_ATTACHMENT / KEPT_DEPTH_ATTACHMENT, the bound framebuffer's
    // attachments this draw neither writes nor tests are kept in its pipeline (SanitizePipelineState).
    uint8_t keptAttachments = 0;
    // [Switch] SwitchClipDistanceSpecialization: g_sharedConstants.clipPlaneEnabled, set together with it (only with the
    // option), so that the clip plane state selects the pipeline. SanitizePipelineState turns it into
    // SPEC_CONSTANT_NO_CLIP_DISTANCE and clears it.
    uint8_t clipPlaneEnabled = 0;
#endif
};
#pragma pack(pop)

#if defined(__SWITCH__)
static constexpr uint8_t KEPT_COLOR_ATTACHMENT = 0x1;
static constexpr uint8_t KEPT_DEPTH_ATTACHMENT = 0x2;
#endif

#if defined(__SWITCH__)
// [Switch] Specialization bits ORed into every pipeline after the shaders' masks (SanitizePipelineState), decided once
// in CreateHostDevice: SPEC_CONSTANT_CONSTANTS_UBO and whatever later bits shader_common.h lists as "renderer, after
// masking". They are in no shader's mask, and every pipeline is created through SanitizePipelineState, so the
// pipelines' hashes agree.
static uint32_t g_postMaskSpecConstants = 0;
// SwitchTrimConstantUploads: the bytes of each constant block uploaded last in this command list (ConstantBytesToUpload).
static uint32_t g_vertexConstantBytesUploaded = 0;
static uint32_t g_pixelConstantBytesUploaded = 0;
#endif

#if defined(__SWITCH__) && defined(SPEC_CONSTANT_UNUSED_COLOR_SHIFT) && defined(SPEC_CONSTANT_UNUSED_OUTPUT_COMPONENT_COUNT) && \
    defined(SHADER_FLAG_PIXEL_KILL_CONDITIONAL)
// [Switch] What the pipelines use of XenosRecomp-switch-perf.patch (shader_common.h): the output trimming constants
// and the kill flags. Without them (an unpatched translator) SwitchTrimPixelOutputs, SwitchTrimVertexOutputs and
// SwitchDepthOnlyWithoutPixelShader do nothing.
#define MARATHON_RECOMP_SWITCH_SHADER_SPECIALIZATION
#endif

#if defined(__SWITCH__) && defined(SPEC_CONSTANT_CONSTANTS_UBO)
// [Switch] SwitchConstantsUBO: the translated shaders read their constants from dynamic uniform buffers (descriptor
// set 5; set 4 is g_ConditionalSurveyBuffer) instead of through the 64-bit pointers of the push constants. NVK serves
// dynamic uniform buffers from hardware constant banks, also on Maxwell; the pointer path compiles to global memory
// loads. Both read the same bytes of the same upload allocation (shader_common.h: with -fvk-use-dx-layout, word B of
// a block is v[B / 16][(B % 16) / 4]), and SPEC_CONSTANT_CONSTANTS_UBO, ORed into every pipeline after the shaders'
// masks, compiles the other path out. Set 5 is in the pipeline layout whatever the option says: the translated
// shaders declare it either way.
#define MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
static constexpr uint32_t CONSTANTS_UBO_SET_INDEX = 5;
static bool g_constantsUbo = false; // Decided once in CreateHostDevice, before the pipeline layout.
static RenderDescriptorSetBuilder g_constantsUboSetBuilder;
// Bound to set 5 at the start of every command list, until a draw binds the blocks it uploaded (never read: every
// read of set 5 is behind SPEC_CONSTANT_CONSTANTS_UBO, and with it every draw binds its own blocks first).
static std::unique_ptr<RenderBuffer> g_constantsUboNullBuffer;
static std::unique_ptr<RenderDescriptorSet> g_constantsUboNullSet;
#endif

#if defined(__SWITCH__) && defined(SHARED_CONSTANTS_SIZE) && defined(SHARED_CONSTANTS_TEXTURE_2D_ARRAY_SIZES_OFFSET) && \
    defined(SHARED_CONSTANTS_GATHERABLE_SLOTS_OFFSET) && defined(SHARED_CONSTANTS_TEXTURE_2D_SIZES_OFFSET) && \
    defined(SPEC_CONSTANT_TEXTURE_SIZE) && defined(SPEC_CONSTANT_SHADOW_GATHER) && defined(SPEC_CONSTANT_SHADOW_GATHER_KNOWN)
// [Switch] The shared constant tables of XenosRecomp-switch-perf.patch (shader_common.h): per texture slot, the size of
// the view behind its 2D-array and 2D descriptors (SwitchTextureSizeConstants) and whether its 2D-array view and sampler
// are gatherable (SwitchShadowGather). Only read by shaders with SPEC_CONSTANT_TEXTURE_SIZE(_VERIFY) or
// SPEC_CONSTANT_SHADOW_GATHER(_VERIFY); kept up to date only while one of those is on (g_textureSlotTables).
#define MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
#endif

#if defined(__SWITCH__)
// [Switch] Renderer stage 5, decided once in CreateHostDevice (Vulkan only, which the Switch always is):
//  g_textureSlotTables: the texture size and gatherable slot tables are written (any size or gather option).
//  g_shadowGatherSpecialization: SwitchShadowGatherSpecialization (SPEC_CONSTANT_SHADOW_GATHER_KNOWN variants).
//  g_alphaTestEarlyOutBit, g_alphaTestSinkBit: SPEC_CONSTANT_ALPHA_TEST_EARLY_OUT / _SINK, ORed in before masking.
//  g_skipTransparentPixels: SwitchSkipTransparentPixels (BlendSkipBits).
//  g_vertexSwapSpecialization: specialization constant 4 from the vertex declaration (CreateGraphicsPipeline).
//  g_clipDistanceSpecialization: SPEC_CONSTANT_NO_CLIP_DISTANCE for pipelines drawn with the clip plane off.
//  g_relativeFromMemory: SPEC_CONSTANT_RELATIVE_FROM_MEMORY for vertex shaders with a0-indexed constant arrays.
//  g_surveySlots: SwitchSurveySlots (ProcSetConditionalSurvey).
static bool g_textureSlotTables = false;
static bool g_shadowGatherSpecialization = false;
// The slots any translated pixel shader gathers (the OR of every cache entry's gatherSlots; Sonic '06: slot 11, g_smpCSM):
// the only bits of g_GatherableSlots the choice of a SPEC_CONSTANT_SHADOW_GATHER_KNOWN variant reads.
static uint32_t g_gatherSlotsRead = 0;
static uint32_t g_alphaTestEarlyOutBit = 0;
static uint32_t g_alphaTestSinkBit = 0;
static bool g_skipTransparentPixels = false;
static bool g_vertexSwapSpecialization = false;
static bool g_clipDistanceSpecialization = false;
static bool g_relativeFromMemory = false;
static bool g_surveySlots = false;
#endif

struct UploadAllocation
{
    const RenderBuffer* buffer;
    uint64_t offset;
    uint8_t* memory;
    uint64_t deviceAddress;
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    // The set 5 of the upload buffer the allocation is in (SwitchConstantsUBO), else nullptr.
    RenderDescriptorSet* constantsSet = nullptr;
#endif
};

struct SharedConstants
{
    uint32_t texture2DIndices[16]{};
    uint32_t texture2DArrayIndices[16]{};
    uint32_t textureCubeIndices[16]{};
    uint32_t samplerIndices[16]{};
    uint32_t booleans{};
    uint32_t swappedTexcoords{};
    uint32_t swappedNormals{};
    uint32_t swappedBinormals{};
    uint32_t swappedTangents{};
    uint32_t swappedBlendWeights{};
    float halfPixelOffsetX{};
    float halfPixelOffsetY{};
    float clipPlane[4]{};
    bool clipPlaneEnabled{};
    float alphaThreshold{};
    uint32_t conditionalSurveyIndex{};
    uint32_t conditionalRenderingIndex{};
#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
    // [Switch] The tables past the original 320 bytes (shader_common.h, SHARED_CONSTANTS_*_OFFSET), written by
    // UpdateTextureSlotTables wherever a slot's descriptors or sampler change. The shaders read them only with
    // SPEC_CONSTANT_TEXTURE_SIZE(_VERIFY) or SPEC_CONSTANT_SHADOW_GATHER(_VERIFY) (g_textureSlotTables).
    // Width, height and layer count of the view behind texture2DArrayIndices[s], as GetDimensions(0, ...) returns them.
    float texture2DArraySizes[16][4]{};
    // Bit s: that view and samplerIndices[s]'s sampler make a gather return exactly the texels of point fetches.
    uint32_t gatherableSlots{};
    uint32_t tablePadding[3]{};
    // Width and height of mip 0 of the view behind texture2DIndices[s].
    float texture2DSizes[16][2]{};
#endif
};

// The bytes of the original block: every shader reads them; the tables after them only with the options above.
static constexpr uint32_t SHARED_CONSTANTS_BASE_SIZE = 320;

#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
// The uniform buffer view of the block (UrConstantsShared, shader_common.h) reads these byte offsets, as the pointer
// path does. Binding 2 covers sizeof(SharedConstants), the SHARED_CONSTANTS_SIZE bytes the shaders that read the tables
// declare (the others declare the first 320).
static_assert(offsetof(SharedConstants, booleans) == 256 && offsetof(SharedConstants, swappedBlendWeights) == 276);
static_assert(offsetof(SharedConstants, halfPixelOffsetX) == 280 && offsetof(SharedConstants, clipPlane) == 288);
static_assert(offsetof(SharedConstants, clipPlaneEnabled) == 304 && offsetof(SharedConstants, alphaThreshold) == 308);
static_assert(offsetof(SharedConstants, conditionalSurveyIndex) == 312 && offsetof(SharedConstants, conditionalRenderingIndex) == 316);
static_assert(sizeof(SharedConstants) % 16 == 0 && sizeof(SharedConstants) <= SHARED_CONSTANTS_SIZE);
#endif

#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
static_assert(sizeof(SharedConstants) == SHARED_CONSTANTS_SIZE, "shader_common.h declares the shared block as SHARED_CONSTANTS_SIZE bytes.");
static_assert(offsetof(SharedConstants, texture2DArraySizes) == SHARED_CONSTANTS_TEXTURE_2D_ARRAY_SIZES_OFFSET);
static_assert(offsetof(SharedConstants, gatherableSlots) == SHARED_CONSTANTS_GATHERABLE_SLOTS_OFFSET);
static_assert(offsetof(SharedConstants, texture2DSizes) == SHARED_CONSTANTS_TEXTURE_2D_SIZES_OFFSET);
#else
static_assert(sizeof(SharedConstants) == SHARED_CONSTANTS_BASE_SIZE);
#endif

// Depth bias values here are only used when the render device has 
// dynamic depth bias capability enabled. Otherwise, they get unused
// and the values get assigned in the pipeline state instead.

static GuestSurface* g_renderTarget;
static GuestSurface* g_depthStencil;
static RenderFramebuffer* g_framebuffer;
static RenderViewport g_viewport(0.0f, 0.0f, 1280.0f, 720.0f);
static PipelineState g_pipelineState;
static int32_t g_depthBias;
static float g_slopeScaledDepthBias;
static uint32_t g_vertexShaderConstants[0x400];
static uint32_t g_pixelShaderConstants[0x380];
static SharedConstants g_sharedConstants;
static GuestTexture* g_textures[16];
static RenderSamplerDesc g_samplerDescs[16];
static bool g_scissorTestEnable = false;
static RenderRect g_scissorRect;
static RenderVertexBufferView g_vertexBufferViews[16];
static RenderInputSlot g_inputSlots[16];
static RenderIndexBufferView g_indexBufferView({}, 0, RenderFormat::R16_UINT);

struct DirtyStates
{
    bool renderTargetAndDepthStencil;
    bool viewport;
    bool pipelineState;
    bool depthBias;
    bool sharedConstants;
    bool scissorRect;
    bool vertexShaderConstants;
    uint8_t vertexStreamFirst;
    uint8_t vertexStreamLast;
    bool indices;
    bool pixelShaderConstants;

    DirtyStates(bool value)
        : renderTargetAndDepthStencil(value)
        , viewport(value)
        , pipelineState(value)
        , depthBias(value)
        , sharedConstants(value)
        , scissorRect(value)
        , vertexShaderConstants(value)
        , vertexStreamFirst(value ? 0 : 255)
        , vertexStreamLast(value ? 15 : 0)
        , indices(value)
        , pixelShaderConstants(value)
    {
    }
};

static DirtyStates g_dirtyStates(true);

template<typename T>
static void SetDirtyValue(bool& dirtyState, T& dest, const T& src)
{
    if (dest != src)
    {
        dest = src;
        dirtyState = true;
    }
}

static constexpr size_t PROFILER_VALUE_COUNT = 256;
static size_t g_profilerValueIndex;

struct Profiler
{
    std::atomic<double> value;
    double values[PROFILER_VALUE_COUNT];
    std::chrono::steady_clock::time_point start;

    void Begin()
    {
        start = std::chrono::steady_clock::now();
    }

    void End()
    {
        value = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    void Set(double v)
    {
        value = v;
    }

    void Reset()
    {
        End();
        Begin();
    }

    double UpdateAndReturnAverage()
    {
        values[g_profilerValueIndex] = value;
        return std::accumulate(values, values + PROFILER_VALUE_COUNT, 0.0) / PROFILER_VALUE_COUNT;
    }
};

static double g_applicationValues[PROFILER_VALUE_COUNT];
static Profiler g_gpuFrameProfiler;
static Profiler g_presentProfiler;
static Profiler g_frameFenceProfiler;
static Profiler g_presentWaitProfiler;
static Profiler g_swapChainAcquireProfiler;

static bool g_profilerVisible;
static bool g_profilerWasToggled;

#if !defined(MARATHON_RECOMP_D3D12) && !defined(MARATHON_RECOMP_METAL)
static constexpr Backend g_backend = Backend::VULKAN;
#else
static Backend g_backend;
#endif

static bool g_triangleStripWorkaround = false;

static std::unique_ptr<RenderInterface> g_interface;
static std::unique_ptr<RenderDevice> g_device;

#if defined(__SWITCH__)
extern "C" void SwitchSetCurrentThreadPriority(int priority);
#endif

// createTexture can fail (the Vulkan backend returns null when vkCreateImage /
// its memory allocation fails — typically VK_ERROR_OUT_OF_DEVICE_MEMORY on
// large render targets). Callers here
// historically assumed success and crashed deep inside the driver when a
// null/half-dead texture was used. Log the failing description loudly and retry
// at progressively halved resolution; return null only if everything fails.
static std::unique_ptr<RenderTexture> CreateTextureChecked(const RenderTextureDesc& desc, const char* context, RenderTextureDesc* achievedOut = nullptr)
{
    RenderTextureDesc attempt = desc;

    auto texture = g_device->createTexture(attempt);
    if (texture == nullptr)
    {
        LOGFN_ERROR("!!! createTexture FAILED ({}): {}x{}x{} mips={} arr={} fmt={} flags=0x{:X} - retrying at reduced size",
            context, desc.width, desc.height, desc.depth, desc.mipLevels, desc.arraySize,
            int(desc.format), uint32_t(desc.flags));

        // arraySize and flags are preserved (downstream builds per-layer views
        // from the requested shape); only resolution degrades. Callers must use
        // the achieved desc for stored dimensions or framebuffers mismatch.
        while (texture == nullptr && (attempt.width > 1 || attempt.height > 1 || attempt.depth > 1))
        {
            attempt.width = std::max(1u, attempt.width / 2);
            attempt.height = std::max(1u, attempt.height / 2);
            attempt.depth = std::max(1u, attempt.depth / 2);
            attempt.mipLevels = 1;
            texture = g_device->createTexture(attempt);
        }

        if (texture != nullptr)
            LOGFN_ERROR("... createTexture recovered at {}x{}x{} arr={} ({})",
                attempt.width, attempt.height, attempt.depth, attempt.arraySize, context);
        else
            LOGFN_ERROR("!!! createTexture fallback ALSO FAILED ({})", context);
    }

    if (achievedOut != nullptr)
        *achievedOut = attempt;

    return texture;
}

static RenderDeviceCapabilities g_capabilities;

static constexpr size_t NUM_FRAMES = 2;
static constexpr size_t NUM_QUERIES = 2;

static uint32_t g_frame = 0;
static uint32_t g_nextFrame = 1;

static std::unique_ptr<RenderCommandQueue> g_queue;
static std::unique_ptr<RenderCommandList> g_commandLists[NUM_FRAMES];
static std::unique_ptr<RenderCommandFence> g_commandFences[NUM_FRAMES];
static std::unique_ptr<RenderQueryPool> g_queryPools[NUM_FRAMES];
static bool g_commandListStates[NUM_FRAMES];

static RecompMutex g_copyMutex;
static std::unique_ptr<RenderCommandQueue> g_copyQueue;
static std::unique_ptr<RenderCommandList> g_copyCommandList;
static std::unique_ptr<RenderCommandFence> g_copyCommandFence;

static RecompMutex g_discardMutex;
static std::unique_ptr<RenderCommandList> g_discardCommandList;
static std::unique_ptr<RenderCommandFence> g_discardCommandFence;

static std::unique_ptr<RenderSwapChain> g_swapChain;
static bool g_swapChainValid;

#if defined(__SWITCH__)
static constexpr RenderFormat BACKBUFFER_FORMAT = RenderFormat::R8G8B8A8_UNORM;
#else
static constexpr RenderFormat BACKBUFFER_FORMAT = RenderFormat::B8G8R8A8_UNORM;
#endif

static std::unique_ptr<RenderCommandSemaphore> g_acquireSemaphores[NUM_FRAMES];
static std::unique_ptr<RenderCommandSemaphore> g_renderSemaphores[NUM_FRAMES];
static uint32_t g_backBufferIndex;
static std::unique_ptr<GuestSurface> g_backBufferHolder;
static GuestSurface* g_backBuffer;

static std::unique_ptr<RenderTexture> g_intermediaryBackBufferTexture;
static uint32_t g_intermediaryBackBufferTextureWidth;
static uint32_t g_intermediaryBackBufferTextureHeight;
static uint32_t g_intermediaryBackBufferTextureDescriptorIndex;

static std::unique_ptr<RenderPipeline> g_gammaCorrectionPipeline;

#if defined(__SWITCH__)
// The gamma pass was built with gamma_correction_push_ps (SwitchGammaPushConstants).
static bool g_gammaPushConstants;
#endif

// copy_vs draws the port's full-screen copies (resolves, the gamma pass): vertices 0-2 are one triangle that
// covers the whole target, 3-5 a second one over half of it again. [Switch] SwitchSingleCopyTriangle draws only
// the first. The pixel shaders drawn with it (copy, resolve, gamma) compute each pixel from its own position
// (a Load at SV_Position), without blending and with depth test ALWAYS: writing a pixel twice changed nothing.
static uint32_t CopyTriangleVertexCount()
{
#if defined(__SWITCH__)
    if (g_switchRenderer.singleCopyTriangle)
        return 3;
#endif
    return 6;
}

static std::unique_ptr<RenderDescriptorSet> g_textureDescriptorSet;
static std::unique_ptr<RenderDescriptorSet> g_samplerDescriptorSet;

static constexpr uint32_t CONDITIONAL_SURVEY_MAX = 64;
static std::unique_ptr<RenderBuffer> g_conditionalSurveyBuffer;
static std::unique_ptr<RenderDescriptorSet> g_conditionalSurveyDescriptorSet;

enum
{
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D,
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D_ARRAY,
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE,
    TEXTURE_DESCRIPTOR_NULL_COUNT
};

static constexpr size_t TEXTURE_DESCRIPTOR_SIZE = 32768;

// The heap size the texture descriptor set layouts are created with.
static uint32_t g_textureDescriptorCount = TEXTURE_DESCRIPTOR_SIZE;

#if defined(__SWITCH__)
// [Switch] SwitchCompactTextureHeap: 16,384 entries, 64 KB with NVK's 4-byte descriptors, which is the most NVK
// reads from a hardware constant bank (NVK_MAX_CBUF_SIZE). With 32,768 entries every texture fetch first loads
// its descriptor from memory. While fewer than 16,384 descriptors are in use, the same indices hold the same
// descriptors either way. A texture that finds the heap full gets the null 2D descriptor (logged once), so it
// would render black: that is why it stays off until the high-water lines below show enough headroom.
static constexpr uint32_t TEXTURE_DESCRIPTOR_COMPACT_SIZE = 16384;

// Set once the null descriptors exist; later writes to their indices (a texture that found the heap full)
// are dropped instead of replacing them.
static bool g_nullTextureDescriptorsWritten;
#endif

struct TextureDescriptorAllocator
{
    RecompMutex mutex;
    uint32_t capacity = TEXTURE_DESCRIPTOR_NULL_COUNT;
    std::vector<uint32_t> freed;
    // Heap size when it is smaller than the arrays (the Switch's compact heap); no limit otherwise.
    uint32_t limit = UINT32_MAX;
    bool exhausted = false;

    uint32_t allocate()
    {
        std::lock_guard lock(mutex);

        uint32_t value;
        if (!freed.empty())
        {
            value = freed.back();
            freed.pop_back();
        }
        else if (capacity >= limit)
        {
            // Out of descriptors: hand out the null 2D texture, whose descriptor is never overwritten.
            if (!exhausted)
            {
                fprintf(stderr, "Texture descriptors: the heap of %u is full; further textures get the null texture.\n", limit);
                exhausted = true;
            }

            value = TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D;
        }
        else
        {
            value = capacity;
            ++capacity;

#if defined(__SWITCH__)
            // High-water mark of the heap, so its size can be checked against real play sessions.
            if ((capacity % 1024) == 0)
                fprintf(stderr, "Texture descriptors: %u in use at most so far.\n", capacity);
#endif
        }

        return value;
    }

    void free(uint32_t value)
    {
        assert(value != NULL || exhausted);
        if (value < TEXTURE_DESCRIPTOR_NULL_COUNT)
            return; // A null descriptor handed out by a full heap: not ours to recycle.

        std::lock_guard lock(mutex);
        freed.push_back(value);
    }
};

static std::unique_ptr<RenderTexture> g_blankTextures[TEXTURE_DESCRIPTOR_NULL_COUNT];
static std::unique_ptr<RenderTextureView> g_blankTextureViews[TEXTURE_DESCRIPTOR_NULL_COUNT];

static TextureDescriptorAllocator g_textureDescriptorAllocator;

// Size (mip 0) and mip count of the texture written to each texture descriptor, for the renderer changes that
// replace shader size queries with constants. Every view the renderer creates starts at mip 0, so the size is
// what GetDimensions() returns for a 2D descriptor. All descriptor writes go through SetTextureDescriptor so the
// tables cannot go stale; an entry is written before its descriptor index is handed to the render thread. The
// size recorded is the one achieved, which CreateTextureChecked may have halved. 0 mip levels: not known.
static float g_textureDescriptorSizes[TEXTURE_DESCRIPTOR_SIZE][2];
static uint8_t g_textureDescriptorMipLevels[TEXTURE_DESCRIPTOR_SIZE];
// The view's layer count (what GetDimensions() returns for a 2D-array descriptor): every view the renderer creates for
// a descriptor starts at layer 0 and has every layer of its image (plume's default arraySize), so it is the image's.
static uint16_t g_textureDescriptorLayers[TEXTURE_DESCRIPTOR_SIZE];

// A null `texture` (a released texture's descriptor) leaves the tables alone: plume's Vulkan backend then
// leaves the descriptor unchanged too.
static void SetTextureDescriptor(uint32_t descriptorIndex, const RenderTexture* texture, uint32_t width, uint32_t height,
    RenderTextureLayout layout, const RenderTextureView* textureView = nullptr, uint32_t mipLevels = 0, uint32_t layers = 1)
{
#if defined(__SWITCH__)
    if (descriptorIndex < TEXTURE_DESCRIPTOR_NULL_COUNT && g_nullTextureDescriptorsWritten)
        return;
#endif

    if (texture != nullptr && descriptorIndex < TEXTURE_DESCRIPTOR_SIZE)
    {
        g_textureDescriptorSizes[descriptorIndex][0] = float(width);
        g_textureDescriptorSizes[descriptorIndex][1] = float(height);
        g_textureDescriptorMipLevels[descriptorIndex] = uint8_t(std::min(mipLevels, 255u));
        g_textureDescriptorLayers[descriptorIndex] = uint16_t(std::min(layers, 0xFFFFu));
    }

    g_textureDescriptorSet->setTexture(descriptorIndex, texture, layout, textureView);
}

#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
// [Switch] SwitchTextureSizeConstants / SwitchShadowGather: the shared constant tables of texture slot `index`, from the
// descriptors its 2D-array and 2D indices name and from its sampler. Called with the indices already written, wherever
// they or the sampler may change: SetTextureInRenderThread, SetSurface, SetSurfaceAsTexture, SampleDepthReadOnly,
// ProcSetSamplerState and the reset of the slots at the start of a command list. Every write of the indices goes through
// one of those, and so does every rebinding of a descriptor index whose tables changed (a recycled index is a new
// texture, bound through SetTextureInRenderThread; the hand-overs and depth-read views take new indices), so the entries
// always describe the descriptors the shaders read: the size and layer count of their views, which is what GetDimensions()
// returns (every view starts at mip 0 and layer 0 and has every layer), as the same integers converted to float.
//
// Gatherable (shader_common.h, SPEC_CONSTANT_SHADOW_GATHER): a 2D-array view with power-of-two width and height and one
// mip level, sampled with point filtering both ways, without anisotropy and without depth comparison (plume's samplers
// always take normalized coordinates). Address modes and borders apply to the same texel indices either way.
static void UpdateTextureSlotTables(uint32_t index)
{
    if (!g_textureSlotTables || index >= std::size(g_sharedConstants.texture2DArraySizes))
        return;

    auto& constants = g_sharedConstants;
    bool& dirty = g_dirtyStates.sharedConstants;

    const uint32_t arrayDescriptor = constants.texture2DArrayIndices[index];
    float arrayWidth = 1.0f;
    float arrayHeight = 1.0f;
    float arrayLayers = 1.0f;
    bool gatherable = false;
    if (arrayDescriptor < TEXTURE_DESCRIPTOR_SIZE)
    {
        arrayWidth = g_textureDescriptorSizes[arrayDescriptor][0];
        arrayHeight = g_textureDescriptorSizes[arrayDescriptor][1];
        arrayLayers = float(g_textureDescriptorLayers[arrayDescriptor]);

        const uint32_t width = uint32_t(arrayWidth);
        const uint32_t height = uint32_t(arrayHeight);
        gatherable = g_textureDescriptorMipLevels[arrayDescriptor] == 1 && width != 0 && height != 0 &&
            (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
    }

    SetDirtyValue(dirty, constants.texture2DArraySizes[index][0], arrayWidth);
    SetDirtyValue(dirty, constants.texture2DArraySizes[index][1], arrayHeight);
    SetDirtyValue(dirty, constants.texture2DArraySizes[index][2], arrayLayers);

    const uint32_t descriptor = constants.texture2DIndices[index];
    SetDirtyValue(dirty, constants.texture2DSizes[index][0], descriptor < TEXTURE_DESCRIPTOR_SIZE ? g_textureDescriptorSizes[descriptor][0] : 1.0f);
    SetDirtyValue(dirty, constants.texture2DSizes[index][1], descriptor < TEXTURE_DESCRIPTOR_SIZE ? g_textureDescriptorSizes[descriptor][1] : 1.0f);

    const RenderSamplerDesc& sampler = g_samplerDescs[index];
    gatherable = gatherable && sampler.minFilter == RenderFilter::NEAREST && sampler.magFilter == RenderFilter::NEAREST &&
        !sampler.anisotropyEnabled && !sampler.comparisonEnabled;

    uint32_t slots = constants.gatherableSlots;
    if (gatherable)
        slots |= 1u << index;
    else
        slots &= ~(1u << index);

    // SwitchShadowGatherSpecialization: a pipeline chosen for the previous slots must not draw once they changed
    // (FindOrCreateGraphicsPipeline chooses again; the lookup cache keys on them). Only the slots shaders gather count.
    if (g_shadowGatherSpecialization && ((slots ^ constants.gatherableSlots) & g_gatherSlotsRead) != 0)
        g_dirtyStates.pipelineState = true;

    SetDirtyValue(dirty, constants.gatherableSlots, slots);
}
#endif

static std::unique_ptr<RenderPipelineLayout> g_pipelineLayout;
static xxHashMap<std::unique_ptr<RenderPipeline>> g_pipelines;

#ifdef ASYNC_PSO_DEBUG
static std::atomic<uint32_t> g_pipelinesCreatedInRenderThread;
static std::atomic<uint32_t> g_pipelinesCreatedAsynchronously;
static std::atomic<uint32_t> g_pipelinesDropped;
static std::atomic<uint32_t> g_pipelinesCurrentlyCompiling;
static std::string g_pipelineDebugText;
static RecompMutex g_debugMutex;
#endif

#ifdef PSO_CACHING
static xxHashMap<PipelineState> g_pipelineStatesToCache;
static RecompMutex g_pipelineCacheMutex;
#endif

static std::atomic<uint32_t> g_compilingPipelineTaskCount;
static std::atomic<uint32_t> g_pendingPipelineTaskCount;

enum class PipelineTaskType
{
    Null,
    DatabaseData,
    PrecompilePipelines,
    RecompilePipelines
};

struct PipelineTask
{
    PipelineTaskType type{};
//    boost::shared_ptr<Hedgehog::Database::CDatabaseData> databaseData;
};

static RecompMutex g_pipelineTaskMutex;
static std::vector<PipelineTask> g_pipelineTaskQueue;

//static void EnqueuePipelineTask(PipelineTaskType type, const boost::shared_ptr<Hedgehog::Database::CDatabaseData>& databaseData)
//{
//    // Precompiled pipelines deliberately do not increment
//    // this counter to overlap the compilation with intro logos.
//    if (type != PipelineTaskType::PrecompilePipelines)
//        ++g_compilingPipelineTaskCount;
//
//    {
//        std::lock_guard lock(g_pipelineTaskMutex);
//        g_pipelineTaskQueue.emplace_back(type, databaseData);
//    }
//
//    if ((++g_pendingPipelineTaskCount) == 1)
//        g_pendingPipelineTaskCount.notify_one();
//}

static const PipelineState g_pipelineStateCache[] =
{
#include "cache/pipeline_state_cache.h"
};

#include "cache/vertex_element_cache.h"

static uint8_t* const g_vertexDeclarationCache[] =
{
#include "cache/vertex_declaration_cache.h"
};

static xxHashMap<std::pair<uint32_t, std::unique_ptr<RenderSampler>>> g_samplerStates;

static RecompMutex g_vertexDeclarationMutex;
static xxHashMap<GuestVertexDeclaration*> g_vertexDeclarations;

struct UploadBuffer
{
    static constexpr size_t SIZE = 16 * 1024 * 1024;

    std::unique_ptr<RenderBuffer> buffer;
    uint8_t* memory = nullptr;
    uint64_t deviceAddress = 0;
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    std::unique_ptr<RenderDescriptorSet> constantsSet;
#endif
};

#if defined(__SWITCH__) && defined(__aarch64__)
// [Switch] The byte-swapped copy of a vertex or index buffer into its GPU memory, 64 bytes per step (the profile had
// UnlockBuffer at 5-9 % of the game thread), and of the render thread's byte-swapped uploads (UploadAllocator). UnlockBuffer's
// loop re-read the buffer's size and source pointer after every 4-byte store (the destination could have aliased them)
// and wrote the uncached GPU memory 4 bytes at a time. The same elements, the same bytes: count elements of sizeof(T)
// each, every one with its bytes reversed.
template<typename T>
static void CopyByteSwapped(T* __restrict dest, const T* __restrict src, size_t count)
{
    static_assert(sizeof(T) == 2 || sizeof(T) == 4);
    constexpr size_t PER_VECTOR = 16 / sizeof(T);
    auto reverse = [](uint8x16_t v) { return sizeof(T) == 4 ? vrev32q_u8(v) : vrev16q_u8(v); };

    size_t i = 0;
    for (; i + 4 * PER_VECTOR <= count; i += 4 * PER_VECTOR)
    {
        uint8x16x4_t v = vld1q_u8_x4(reinterpret_cast<const uint8_t*>(src + i));
        v.val[0] = reverse(v.val[0]);
        v.val[1] = reverse(v.val[1]);
        v.val[2] = reverse(v.val[2]);
        v.val[3] = reverse(v.val[3]);
        vst1q_u8_x4(reinterpret_cast<uint8_t*>(dest + i), v);
    }

    for (; i + PER_VECTOR <= count; i += PER_VECTOR)
        vst1q_u8(reinterpret_cast<uint8_t*>(dest + i), reverse(vld1q_u8(reinterpret_cast<const uint8_t*>(src + i))));

    for (; i < count; i++)
        dest[i] = ByteSwap(src[i]);
}
#endif

struct UploadAllocator
{
    std::vector<UploadBuffer> buffers;
    uint32_t index = 0;
    uint32_t offset = 0;

    // reserve: bytes that must fit in the buffer from the returned offset ([Switch] a uniform buffer binding covers
    // its whole constant block, also when fewer bytes were copied into it). 0 = size, as before.
    UploadAllocation allocate(uint32_t size, uint32_t alignment, uint32_t reserve = 0)
    {
        assert(size <= UploadBuffer::SIZE);

        offset = (offset + alignment - 1) & ~(alignment - 1);

        if (offset + std::max(size, reserve) > UploadBuffer::SIZE)
        {
            ++index;
            offset = 0;
        }

        if (buffers.size() <= index)
            buffers.resize(index + 1);

        auto& buffer = buffers[index];
        if (buffer.buffer == nullptr)
        {
            buffer.buffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(UploadBuffer::SIZE, RenderBufferFlag::CONSTANT | RenderBufferFlag::VERTEX | RenderBufferFlag::INDEX | RenderBufferFlag::DEVICE_ADDRESSABLE));
            buffer.memory = reinterpret_cast<uint8_t*>(buffer.buffer->map());
            buffer.deviceAddress = buffer.buffer->getDeviceAddress();
        }

#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
        if (g_constantsUbo && buffer.constantsSet == nullptr)
        {
            // Bindings 0/1/2 = the vertex, pixel and shared constant blocks of this buffer: the dynamic offsets
            // select the blocks, the ranges are their sizes (UrConstantsVertex/Pixel/Shared, shader_common.h).
            buffer.constantsSet = g_constantsUboSetBuilder.create(g_device.get());
            buffer.constantsSet->setBuffer(0, buffer.buffer.get(), sizeof(g_vertexShaderConstants));
            buffer.constantsSet->setBuffer(1, buffer.buffer.get(), sizeof(g_pixelShaderConstants));
            buffer.constantsSet->setBuffer(2, buffer.buffer.get(), sizeof(SharedConstants));
        }
#endif

        auto ref = buffer.buffer->at(offset);
        offset += size;

        UploadAllocation result{ ref.ref, ref.offset, buffer.memory + ref.offset, buffer.deviceAddress + ref.offset };
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
        result.constantsSet = buffer.constantsSet.get();
#endif
        return result;
    }

#if defined(__SWITCH__)
    // [Switch] SwitchTrimConstantUploads: the first copySize bytes of a constant block of blockSize bytes, with the
    // whole block reserved (a uniform buffer binding covers it; the shader reads none of the bytes past copySize).
    UploadAllocation allocateConstants(const void* memory, uint32_t copySize, uint32_t blockSize, uint32_t alignment)
    {
        auto result = allocate(copySize, alignment, blockSize);
        memcpy(result.memory, memory, copySize);
        return result;
    }

    // [Switch] Moves on to a new buffer unless `bytes` more fit in the current one, so that the allocations that
    // follow (up to `bytes` with their alignment) land in the same buffer.
    void ensureSpace(uint32_t bytes)
    {
        if (offset + bytes > UploadBuffer::SIZE)
        {
            ++index;
            offset = 0;
        }
    }
#endif

    template<bool TByteSwap, typename T>
    UploadAllocation allocate(const T* memory, uint32_t size, uint32_t alignment)
    {
        auto result = allocate(size, alignment);

        if constexpr (TByteSwap)
        {
            auto destination = reinterpret_cast<T*>(result.memory);

#if defined(__SWITCH__) && defined(__aarch64__)
            // [Switch] The same elements as the loop below (one per started sizeof(T) bytes), 64 bytes per step.
            if constexpr (sizeof(T) == 2 || sizeof(T) == 4)
            {
                CopyByteSwapped(destination, memory, (size_t(size) + sizeof(T) - 1) / sizeof(T));
                return result;
            }
#endif
            for (size_t i = 0; i < size; i += sizeof(T))
            {
                *destination = ByteSwap(*memory);
                ++destination;
                ++memory;
            }
        }
        else
        {
            memcpy(result.memory, memory, size);
        }

        return result;
    }

    void reset()
    {
        index = 0;
        offset = 0;
    }
};

static UploadAllocator g_uploadAllocators[NUM_FRAMES];

struct IntermediaryUploadAllocator
{
    static constexpr size_t SIZE = 16 * 1024 * 1024;

    std::vector<std::unique_ptr<uint8_t[]>> buffers;
    uint32_t index = 0;
    uint32_t offset = 0;

    uint8_t* allocate(uint32_t size)
    {
        assert(size <= SIZE);

        if (offset + size > SIZE)
        {
            ++index;
            offset = 0;
        }

        if (buffers.size() <= index)
            buffers.resize(index + 1);

        auto& buffer = buffers[index];
        if (buffer == nullptr)
            buffer = std::make_unique_for_overwrite<uint8_t[]>(SIZE);

        auto result = buffer.get() + offset;
        offset += ((size + 0xF) & ~0xF);

        return result;
    }

    uint8_t* allocate(const void* memory, uint32_t size)
    {
        auto result = allocate(size);
        memcpy(result, memory, size);
        return result;
    }

    void reset()
    {
        index = 0;
        offset = 0;
    }
};

#if defined(__SWITCH__)
// [Switch] SwitchPresentWithoutRecordWait: one per frame the render thread may still be recording. The D3D thread
// switches to the other at the end of each pipelined Present that does not wait for the recording (Video::Present):
// the frame that used it two Presents ago has been recorded by then (the wait at the start of that Present).
static IntermediaryUploadAllocator g_intermediaryUploadAllocators[2];
static uint32_t g_intermediaryUploadAllocatorIndex = 0;

static IntermediaryUploadAllocator& CurrentIntermediaryUploadAllocator()
{
    return g_intermediaryUploadAllocators[g_intermediaryUploadAllocatorIndex];
}
#else
static IntermediaryUploadAllocator g_intermediaryUploadAllocator;

static IntermediaryUploadAllocator& CurrentIntermediaryUploadAllocator()
{
    return g_intermediaryUploadAllocator;
}
#endif

static std::vector<GuestResource*> g_tempResources[NUM_FRAMES];
static std::vector<std::unique_ptr<RenderBuffer>> g_tempBuffers[NUM_FRAMES];

template<GuestPrimitiveType PrimitiveType>
struct PrimitiveIndexData
{
    std::vector<uint16_t> indexData;
    RenderBufferReference indexBuffer;
    uint32_t currentIndexCount = 0;

    uint32_t prepare(uint32_t guestPrimCount)
    {
        uint32_t primCount;
        uint32_t indexCountPerPrimitive;

        switch (PrimitiveType)
        {
        case D3DPT_TRIANGLEFAN:
            primCount = guestPrimCount - 2;
            indexCountPerPrimitive = 3; 
            break;
        case D3DPT_QUADLIST:
            primCount = guestPrimCount / 4;
            indexCountPerPrimitive = 6;
            break;
        default:
            assert(false && "Unknown primitive type.");
            break;
        }

        uint32_t indexCount = primCount * indexCountPerPrimitive;

        if (indexData.size() < indexCount)
        {
            const size_t oldPrimCount = indexData.size() / indexCountPerPrimitive;
            indexData.resize(indexCount);

            for (size_t i = oldPrimCount; i < primCount; i++)
            {
                switch (PrimitiveType)
                {
                case D3DPT_TRIANGLEFAN:
                {
                    indexData[i * 3 + 0] = 0;
                    indexData[i * 3 + 1] = static_cast<uint16_t>(i + 1);
                    indexData[i * 3 + 2] = static_cast<uint16_t>(i + 2);
                    break;
                }
                case D3DPT_QUADLIST:
                {
                    indexData[i * 6 + 0] = static_cast<uint16_t>(i * 4 + 0);
                    indexData[i * 6 + 1] = static_cast<uint16_t>(i * 4 + 1);
                    indexData[i * 6 + 2] = static_cast<uint16_t>(i * 4 + 2);

                    indexData[i * 6 + 3] = static_cast<uint16_t>(i * 4 + 0);
                    indexData[i * 6 + 4] = static_cast<uint16_t>(i * 4 + 2);
                    indexData[i * 6 + 5] = static_cast<uint16_t>(i * 4 + 3);
                    break;
                }
                default:
                    assert(false && "Unknown primitive type.");
                    break;
                }
            }
        }

        if (indexBuffer == NULL || currentIndexCount < indexCount)
        {
            auto allocation = g_uploadAllocators[g_frame].allocate<false>(indexData.data(), indexCount * 2, 2);
            indexBuffer = allocation.buffer->at(allocation.offset);
            currentIndexCount = indexCount;
        }

        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.buffer, indexBuffer);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.size, indexCount * 2);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.format, RenderFormat::R16_UINT);

        return indexCount;
    }

    void reset()
    {
        indexBuffer = {};
        currentIndexCount = 0;
    }
};

static PrimitiveIndexData<D3DPT_TRIANGLEFAN> g_triangleFanIndexData;
static PrimitiveIndexData<D3DPT_QUADLIST> g_quadIndexData;

#if defined(__SWITCH__)
// [Switch] SwitchResolveStats: what the render thread does with resolves, clears, barriers and draws, counted in plain
// render-thread counters whatever the key says, and written to stderr.log as per-frame averages every 300 frames when
// it is on (one write; see ReportResolveStats). Counting only: no command changes.
enum ResolveCopyTrigger : uint32_t
{
    RESOLVE_COPY_BEFORE_DRAW,   // the surface was about to be drawn into or tested, or a texture of it sampled
    RESOLVE_COPY_AT_CLEAR,      // the surface was about to be cleared
    RESOLVE_COPY_AT_PRESENT,    // end of the frame
    RESOLVE_COPY_OTHER,         // owed copies, texture updates, releases, resolves into the same texture again
    RESOLVE_COPY_TRIGGER_COUNT
};

// Why a draw into a surface with a pending copy made the copy instead of handing the image over (SwitchCoverageHandOver).
enum CoverageMiss : uint32_t
{
    COVERAGE_MISS_BLEND,        // blending reads the target, or a channel of it is not written
    COVERAGE_MISS_DEPTH,        // a depth buffer is bound (the marks need the depth attachment)
    COVERAGE_MISS_TEXTURES,     // several pending textures, or one of another kind (slice, array, volume)
    COVERAGE_MISS_IMAGE,        // the images differ (format, size, levels), or no stencil buffer
    COVERAGE_MISS_VARIANT,      // the stencil variant of the pipeline is still being compiled
    COVERAGE_MISS_SHADER,       // a conditional survey, no pixel shader, or MSAA
    COVERAGE_MISS_COUNT
};

// Why a draw was not proven to cover its whole target (SwitchExactCoverage).
enum ExactCoverageMiss : uint32_t
{
    EXACT_COVERAGE_MISS_NOT_QUAD,   // not a DrawPrimitiveUP of four vertices as a strip, fan or quad
    EXACT_COVERAGE_MISS_SHADERS,    // position not passed through, a clip plane, or the pixel shader can discard
    EXACT_COVERAGE_MISS_SHAPE,      // not a rectangle split on its diagonal, out of the depth range, or culled
    EXACT_COVERAGE_MISS_EDGES,      // more than a thin frame of the target left uncovered
    EXACT_COVERAGE_MISS_VIEWPORT,   // the viewport is scaled to the target (FlushViewport) or not yet known
    EXACT_COVERAGE_MISS_COUNT
};

// Why a surface's pending colour resolves were not kept over Present (SwitchKeepResolvesPending): the first reason
// CanKeepResolvePending found (diagnostics; the decisions are unchanged).
enum KeepRefused : uint32_t
{
    KEEP_REFUSED_BACK_BUFFER,   // the back buffer, or a surface without its own image
    KEEP_REFUSED_MSAA,
    KEEP_REFUSED_DEPTH,
    KEEP_REFUSED_RELEASED,
    KEEP_REFUSED_TEXTURE,       // a destination that cannot be handed over, a slice other than 0 or a patched texture
    KEEP_REFUSED_SWIZZLE,       // a destination read through a non-identity component mapping
    KEEP_REFUSED_FORMAT,
    KEEP_REFUSED_SIZE,
    KEEP_REFUSED_COUNT
};

// Why a depth surface was not sampled read-only (SwitchReadOnlyDepthSampling): the first reason CanSampleDepthReadOnly
// found (diagnostics).
enum ReadOnlyRefused : uint32_t
{
    READ_ONLY_REFUSED_DEPTH_WRITE,
    READ_ONLY_REFUSED_STENCIL_WRITE,
    READ_ONLY_REFUSED_MSAA,     // or the back buffer
    READ_ONLY_REFUSED_PENDING,  // the surface has a resolve pending (g_pendingResolves)
    READ_ONLY_REFUSED_TEXTURE,  // a destination that cannot be handed over, a slice, mips or a patched texture
    READ_ONLY_REFUSED_FORMAT,   // the texture's format differs (SwitchDepthTexturesD32 makes it so on purpose)
    READ_ONLY_REFUSED_SIZE,
    READ_ONLY_REFUSED_VIEW,     // no descriptor for the read view
    READ_ONLY_REFUSED_COUNT
};

struct ResolveStats
{
    uint64_t copies[RESOLVE_COPY_TRIGGER_COUNT];
    uint64_t copyPixels;
    uint64_t arrayCopies;
    uint64_t handOversAtClear;
    uint64_t coverageHandOvers;
    uint64_t exactHandOvers;
    uint64_t coverageMisses[COVERAGE_MISS_COUNT];
    uint64_t exactCoverageMisses[EXACT_COVERAGE_MISS_COUNT];
    uint64_t kept;
    uint64_t depthDropped;
    uint64_t owed;
    uint64_t noOpDraws;
    uint64_t restoresSkipped;
    uint64_t clearsDeferred;
    uint64_t clearsSkipped;
    uint64_t stencilClearsWidened;
    uint64_t barrierBatches;
    uint64_t conservativeBarrierBatches;
    uint64_t midPassBarrierBatches;
    uint64_t framebufferChanges;
    uint64_t eagerTransitions;
    uint64_t keptAttachmentDraws;
    uint64_t readOnlyDepthDraws;
    uint64_t surveyDraws;
    uint64_t draws;
    uint64_t keepRefused[KEEP_REFUSED_COUNT];
    uint64_t readOnlyRefused[READ_ONLY_REFUSED_COUNT];
    // SwitchCascadeAdoption (perf7): resolves of the shadow surface held in the cascade image instead of copied, the
    // images handed to the array, the copies those needed (array slices not drawn this frame, or held layers copied when
    // a swap did not pay), and the times the surface needed its own image back.
    uint64_t cascadeHeld;
    uint64_t cascadeAdoptions;
    uint64_t cascadeFillIns;
    uint64_t cascadeHeldCopies;
    uint64_t cascadeMaterialized;
    uint64_t cascadeContinued;     // perf8: a cascade continued from a resolved layer into the next one (one layer copy)
    uint64_t cascadeFillsSkipped;  // perf8: layers already equal to the map's slice, not filled in again
};

static ResolveStats g_resolveStats;
static uint32_t g_resolveStatsFrames = 0;
static uint32_t g_resolveCopyTrigger = RESOLVE_COPY_BEFORE_DRAW;

// [Switch] SwitchResolveHandOver: views, descriptors and framebuffers that commands already recorded in the frame may
// still use after their texture or surface got another image. Released when the frame slot comes around again (its
// fence has been waited for by then), in DestructTempResources.
static std::vector<std::unique_ptr<RenderTextureView>> g_handOverViews[NUM_FRAMES];
static std::vector<uint32_t> g_handOverDescriptors[NUM_FRAMES];
static std::vector<std::unique_ptr<RenderFramebuffer>> g_handOverFramebuffers[NUM_FRAMES];
// SwitchCascadeAdoption: the cascade image of a forgotten pair, released with the views above.
static std::vector<std::unique_ptr<RenderTexture>> g_retiredCascadeImages[NUM_FRAMES];

// Every guest render target and depth surface alive (not the back buffer). Their framebuffer caches are keyed by colour
// image, and images move between textures and surfaces with hand-overs: a destroyed image is dropped from every cache,
// so that a new image at the same address cannot find a framebuffer of the old one.
static ankerl::unordered_dense::set<GuestSurface*> g_liveSurfaces;

static void ForgetFramebuffersOf(const RenderTexture* image)
{
    for (GuestSurface* surface : g_liveSurfaces)
    {
        for (auto* cache : { &surface->framebuffers, &surface->readOnlyFramebuffers })
        {
            auto findResult = cache->find(image);
            if (findResult != cache->end())
            {
                if (g_framebuffer == findResult->second.get())
                    g_framebuffer = nullptr;

                cache->erase(findResult);
            }
        }
    }
}

static void ForgetCoverageFramebuffersOf(const RenderTexture* image);
static bool UsesLongerPendingCopies();

// SwitchCascadeAdoption (defined after the hand-overs).
static bool CascadeActive(const GuestSurface* surface);
static RenderFormat CascadeDepthFormat(const GuestSurface* surface);
static void CascadeBindLayer();
static bool CascadeOnResolve(GuestSurface* surface, GuestTexture* texture, uint32_t slice);
static void CascadeAdoptIfHeld(const GuestTexture* texture);
static void CascadeTextureUpdated(const GuestTexture* texture);
static void CascadeAtPresent();
static bool StencilMayBeWritten(const PipelineState& state);
#endif

#if defined(__SWITCH__)
// ------------------------------------------------------------------ GPU profilers and the frame log
// [Switch] SwitchGpuPassProfiler: a GPU timestamp at the start of the frame, at every framebuffer change (SetFramebuffer,
// the framebuffer of a coverage hand-over, the gamma pass) and in front of each run of resolve copies with the same
// trigger, so the time between two timestamps is the GPU time of one pass (including the barrier waits in front of it).
// Passes are grouped by what they render to and by their order among passes with that target, averaged over 300 frames
// and written to stderr.log with per-frame counts of the renderer's work and of what its optimisations saved, where the
// presenting thread spends its frame and the CPU use of every registered thread. Needed to aim further GPU work: the
// whole-frame GPU time does not say which pass costs what.
//
// SwitchGpuDrawProfiler (turns the pass profiler on as well): one more timestamp after every draw, so each draw gets the
// GPU time from the previous timestamp of its pass (the pass start or the draw before it) to its own. Draws are grouped
// by pass, as in the pass table, by shader pair and by the state that changes their cost; the most expensive groups of
// the most expensive passes are printed after each pass table. The timestamps are written at the end of the pipeline
// without draining it, so draws that overlap share their time; the flush in front of every timestamp costs some GPU
// time, so the totals read higher than with the pass profiler alone. Translated shaders are named by the hash of their
// Xbox 360 code, the start of the BLAKE3 of their SPIR-V (which the driver's NVK_SHADER_STATS lines print as well) and
// their file; the port's own shaders by name or address. Timestamps go to pools of 128 so that only the unused tail of
// the last pool has to be written at the end of the frame (plume reads whole pools).
//
// Measurement only: the profilers add vkCmdWriteTimestamp (BOTTOM_OF_PIPE; plume neither ends nor begins a render pass
// for it) and, right after the command list begins, vkCmdResetQueryPool outside any render pass. The draws, barriers,
// framebuffers and shader inputs are the same; the timestamps only cost GPU time, which is why both are off by default.
// Each frame's timestamps are read once its fence has been waited for (Present or PresentOnRenderThread), at the start of
// the next command list that reuses its slot, on the render thread, which is the only thread that touches this state.
static constexpr uint32_t PASS_PROFILER_MAX_PASSES = 160;
static constexpr uint32_t PASS_PROFILER_REPORT_FRAMES = 300;
static constexpr uint32_t DRAW_PROFILER_POOL_SIZE = 128;
static constexpr uint32_t DRAW_PROFILER_MAX_POOLS = 32; // Up to 4096 draws per frame.

// NVK on Horizon reports timestampPeriod = 1 ns, which plume converts the timestamps with, but one tick of the Tegra X1's
// GPU timer is 1000 / 614.4 MHz = ~1.627 ns (measured against wall-clock time). Without the correction GPU times read
// ~60 % of the real ones. The same constant as UnleashedRecomp-NX, so the numbers of the two ports compare.
static double GpuTimestampMs(int64_t ticks)
{
    return double(std::max<int64_t>(ticks, 0)) / 1000000.0 * 1.627;
}

// DrawProfilerDraw::state: what changes a draw's cost besides its shaders.
enum : uint32_t
{
    DRAW_PROFILER_BLEND = 1u << 0,
    DRAW_PROFILER_Z_WRITE = 1u << 1,
    DRAW_PROFILER_ALPHA_TEST = 1u << 2,
    DRAW_PROFILER_ALPHA_TO_COVERAGE = 1u << 3,
    DRAW_PROFILER_REVERSE_Z = 1u << 4,
    DRAW_PROFILER_NO_PIXEL_SHADER = 1u << 5,  // no fragment stage (SwitchDepthOnlyWithoutPixelShader, or none bound)
    DRAW_PROFILER_SURVEY = 1u << 6,           // a conditional survey (the survey's pixel shader)
    DRAW_PROFILER_CONDITIONAL = 1u << 7,      // the conditional rendering prologue
    DRAW_PROFILER_CLIP_PLANE = 1u << 8,
    DRAW_PROFILER_STENCIL = 1u << 9,
    DRAW_PROFILER_COVERAGE = 1u << 10,        // the stencil variant of a coverage hand-over (SwitchCoverageHandOver)
    DRAW_PROFILER_Z_FUNC_SHIFT = 12,          // RenderComparisonFunction, UNKNOWN when depth testing is off.
};

// PassProfilerPass::kind: a render pass of the game, a run of resolve copies (PASS_PROFILER_COPIES + the
// ResolveCopyTrigger that made them due; its "draws" are the copies) or the gamma pass into the swap chain image.
enum : uint32_t
{
    PASS_PROFILER_PASS = 0,
    PASS_PROFILER_COPIES = 1,
    PASS_PROFILER_GAMMA = PASS_PROFILER_COPIES + RESOLVE_COPY_TRIGGER_COUNT,
    PASS_PROFILER_KINDS
};

struct PassProfilerPass
{
    uint32_t width, height;
    RenderFormat colorFormat, depthFormat;
    uint32_t samples;
    uint32_t draws;
    uint32_t kind;
};

struct DrawProfilerDraw
{
    uint32_t pass;
    uint32_t state;
    uint64_t vertexShader; // DrawProfilerShaderId
    uint64_t pixelShader;
    uint32_t count; // Vertices or indices.
};

// SwitchGpuSlowFrameMs: what the render thread did in one frame (the difference of PassProfilerReadCounters between the
// start and the end of its command list), for the slow-frame part of the report. Every field is a uint64_t.
struct PassProfilerCounters
{
    uint64_t resolveCopies = 0;
    uint64_t resolvePixels = 0;
    uint64_t textureUpdates = 0;
    uint64_t barrierBatches = 0;
    uint64_t midPassBarrierBatches = 0;
    uint64_t framebufferChanges = 0;
    uint64_t constantBytes = 0;
    uint64_t pipelines = 0;
    uint64_t pipelineUs = 0;

    static constexpr size_t COUNT = 9;

    void Add(const PassProfilerCounters& other)
    {
        uint64_t* values = &resolveCopies;
        const uint64_t* otherValues = &other.resolveCopies;
        for (size_t i = 0; i < COUNT; i++)
            values[i] += otherValues[i];
    }

    // The counters only grow (ResolveStats, RendererStats), so this is the work done since `start`.
    PassProfilerCounters Since(const PassProfilerCounters& start) const
    {
        PassProfilerCounters difference = *this;
        uint64_t* values = &difference.resolveCopies;
        const uint64_t* startValues = &start.resolveCopies;
        for (size_t i = 0; i < COUNT; i++)
            values[i] -= startValues[i];
        return difference;
    }
};

static_assert(sizeof(PassProfilerCounters) == PassProfilerCounters::COUNT * sizeof(uint64_t));

struct PassProfilerFrame
{
    std::unique_ptr<RenderQueryPool> queries;
    std::vector<PassProfilerPass> passes;
    std::unique_ptr<RenderQueryPool> drawQueries[DRAW_PROFILER_MAX_POOLS];
    std::vector<DrawProfilerDraw> draws;
    PassProfilerCounters start;    // The counters when the command list began (SwitchGpuSlowFrameMs).
    PassProfilerCounters counters; // ... and this frame's share of them, at its end.
    bool recorded = false;
};

struct PassProfilerTotal
{
    PassProfilerPass pass{};
    uint32_t ordinal = 0;
    double gpuMs = 0.0;
    uint64_t draws = 0;
};

struct DrawProfilerKey
{
    uint32_t width, height;
    RenderFormat colorFormat, depthFormat;
    uint32_t samples;
    uint32_t ordinal;
    uint64_t vertexShader, pixelShader;
    uint32_t state;

    bool operator==(const DrawProfilerKey&) const = default;
};

struct DrawProfilerKeyHash
{
    using is_avalanching = void;

    uint64_t operator()(const DrawProfilerKey& key) const
    {
        const uint64_t words[] =
        {
            (uint64_t(key.width) << 32) | key.height,
            (uint64_t(key.colorFormat) << 32) | uint32_t(key.depthFormat),
            (uint64_t(key.samples) << 32) | key.ordinal,
            key.vertexShader,
            key.pixelShader,
            key.state
        };
        return XXH3_64bits(words, sizeof(words));
    }
};

struct DrawProfilerTotal
{
    double gpuMs = 0.0;
    uint64_t draws = 0;
    uint64_t count = 0;
};

// Decided once in CreateHostDevice, before the first command list.
static bool g_passProfilerEnabled = false;
static bool g_drawProfilerEnabled = false;
static bool g_frameLogEnabled = false;

static PassProfilerFrame g_passProfilerFrames[NUM_FRAMES];
static std::vector<PassProfilerTotal> g_passProfilerTotals;
static ankerl::unordered_dense::map<DrawProfilerKey, DrawProfilerTotal, DrawProfilerKeyHash> g_drawProfilerTotals;
static uint32_t g_passProfilerFrameCount = 0;
static double g_passProfilerFrameMs = 0.0;

// [Switch] SwitchGpuSlowFrameMs (turns the pass profiler on as well), the GPU counterpart of the CPU profiler's slow
// frames (UnleashedRecomp-NX round 13): every frame whose GPU time (frame start to closing timestamp) reaches the
// threshold also goes into totals of its own, per pass and (with the draw profiler) per draw group, so the report can say
// which passes grow in those frames and by how much, next to the same passes over all frames. The three slowest frames of
// each report are listed whole. 0 turns it off. Render thread only, like the rest of the profiler's state.
static double g_gpuSlowFrameMs = 0.0;
static PassProfilerCounters g_gpuAllCounters;
static PassProfilerCounters g_gpuSlowCounters;
static uint64_t g_gpuAllDraws = 0;
static uint64_t g_gpuSlowDraws = 0;
static uint32_t g_gpuSlowFrames = 0;
static double g_gpuSlowFrameMsSum = 0.0;
static double g_gpuSlowFrameLongestMs = 0.0;
static uint32_t g_passProfilerFrameNumber = 0; // Frames collected since the start, for the slowest-frame lines.
static std::vector<PassProfilerTotal> g_gpuSlowPassTotals;
static ankerl::unordered_dense::map<DrawProfilerKey, DrawProfilerTotal, DrawProfilerKeyHash> g_drawProfilerSlowTotals;
static const std::chrono::steady_clock::time_point g_passProfilerStartTime = std::chrono::steady_clock::now();

struct GpuSlowFrame
{
    double ms = 0.0;
    double seconds = 0.0; // Since the start of the game, when the frame's timestamps were read.
    uint32_t frameNumber = 0;
    uint64_t draws = 0;
    PassProfilerCounters counters;
    std::string passes; // Its most expensive passes, formatted when the frame is taken.
};

static constexpr size_t GPU_SLOW_FRAMES_LISTED = 3;
static std::vector<GpuSlowFrame> g_gpuSlowestFrames; // At most GPU_SLOW_FRAMES_LISTED, slowest first.

// SwitchGpuDrawProfiler: the first four bytes of the BLAKE3 of each translated shader's SPIR-V, by shader cache entry
// (0: not linked yet). Kept here rather than in GuestShader, which lives in the guest heap. Mesa's BLAKE3
// (util/mesa-blake3.h) comes with the driver, which already links it in (a weak reference never pulls an archive member
// in by itself): without it the shaders are named by their hash alone.
extern "C" void _mesa_blake3_compute(const void* data, size_t size, unsigned char result[32]) __attribute__((weak));
static std::unique_ptr<std::atomic<uint32_t>[]> g_shaderSpirvBlake3;

// When a translated shader's SPIR-V is decoded (GetOrLinkShader); the draw profiler's names only.
static void DrawProfilerHashSpirv(const GuestShader* guestShader, const uint8_t* spirv, size_t spirvSize)
{
    if (g_shaderSpirvBlake3 == nullptr || _mesa_blake3_compute == nullptr || guestShader->shaderCacheEntry == nullptr)
        return;

    const size_t entry = size_t(guestShader->shaderCacheEntry - g_shaderCacheEntries);
    if (entry >= g_shaderCacheEntryCount)
        return;

    unsigned char blake3[32];
    _mesa_blake3_compute(spirv, spirvSize, blake3);
    g_shaderSpirvBlake3[entry].store((uint32_t(blake3[0]) << 24) | (uint32_t(blake3[1]) << 16) | (uint32_t(blake3[2]) << 8) | blake3[3],
        std::memory_order_relaxed);
}

// The renderer's work and what its other optimisations saved, counted on the render thread whatever the profiler says
// (plain increments; counts that need any work only while it is on). The pass profiler's report prints the difference
// to its previous one, per frame. ResolveStats (above) counts the resolves, clears, barriers and framebuffers.
struct RendererStats
{
    uint64_t conditionalRenderingDraws;   // draws with the conditional rendering prologue (lens flares)
    uint64_t drawsWithoutPixelStage;      // pipelines without a fragment stage (SwitchDepthOnlyWithoutPixelShader); profiler on
    uint64_t textureUpdates;              // CPU updates of textures (UnlockTextureRect)
    uint64_t pipelineBinds;               // pipeline lookups for the game's draws
    uint64_t pipelineLookupHits;          // ... answered by SwitchPipelineLookupCache
    uint64_t pipelinesCreated;            // compiled on the render thread (each one stalls it)
    uint64_t pipelineCreationUs;          // ... and how long that took; profiler on
    uint64_t pipelineVariantsRequested;   // handed to the compiler threads (shadow gather and coverage stencil variants)
    uint64_t pipelineVariantsAdded;       // ... and arrived from them
    uint64_t samplerStates;               // sampler states processed
    uint64_t samplerCacheHits;            // ... answered by SwitchSamplerCache
    uint64_t constantSets;                // runs of shader constants received
    uint64_t constantSetsUnchanged;       // ... that changed no value, so no upload
    uint64_t vertexConstantBytes;         // constant bytes uploaded
    uint64_t pixelConstantBytes;
    uint64_t sharedConstantBytes;
    uint64_t constantBytesTrimmed;        // SwitchTrimConstantUploads: bytes of the uploaded blocks not copied
    uint64_t pixelConstantsSkipped;       // SwitchSkipUnusedPixelConstants: draws that did not upload dirty pixel constants
    uint64_t rootAddressPushes;           // constant pointers pushed (with SwitchConstantsUBO only for shaders that read them)
    uint64_t surveyBufferResets;          // zeroing copies of the survey buffer (barriers and a transfer; SwitchSurveySlots)
};

static RendererStats g_rendererStats;

// SwitchGpuSlowFrameMs: the counters a frame's share is taken of (PassProfilerBeginFrame and EndFrame, render thread).
static PassProfilerCounters PassProfilerReadCounters()
{
    PassProfilerCounters counters;
    for (uint64_t count : g_resolveStats.copies)
        counters.resolveCopies += count;
    counters.resolvePixels = g_resolveStats.copyPixels;
    counters.textureUpdates = g_rendererStats.textureUpdates;
    counters.barrierBatches = g_resolveStats.barrierBatches;
    counters.midPassBarrierBatches = g_resolveStats.midPassBarrierBatches;
    counters.framebufferChanges = g_resolveStats.framebufferChanges;
    counters.constantBytes = g_rendererStats.vertexConstantBytes + g_rendererStats.pixelConstantBytes +
        g_rendererStats.sharedConstantBytes;
    counters.pipelines = g_rendererStats.pipelinesCreated;
    counters.pipelineUs = g_rendererStats.pipelineCreationUs;
    return counters;
}

// Counted by the game's D3D thread, while the pass profiler is on: it is their only writer, so a load and a store
// instead of an atomic read-modify-write. They only grow; the report prints the difference to its previous one.
static std::atomic<uint64_t> g_profilerStatesSkipped{ 0 };        // SwitchSkipRedundantRenderStates / SamplerStates
static std::atomic<uint64_t> g_profilerConstantBytesCopied{ 0 };  // SwitchSparseConstantCopies: constants copied
static std::atomic<uint64_t> g_profilerConstantBytesSpanned{ 0 }; // ... and the span of the changed registers
static std::atomic<uint64_t> g_profilerBatchesHandedOver{ 0 };    // SwitchBatchRenderCommands: batches handed over

static void AddGameThreadCount(std::atomic<uint64_t>& counter, uint64_t value)
{
    counter.store(counter.load(std::memory_order_relaxed) + value, std::memory_order_relaxed);
}

// Render commands the render thread has taken off its queue (only it writes this; batches count as their commands), for
// the stall watchdog's dumps (ReportRendererState) and the pass profiler's report.
static std::atomic<uint64_t> g_renderCommandsTaken{ 0 };

static void AddRenderCommandsTaken(uint64_t count)
{
    g_renderCommandsTaken.store(g_renderCommandsTaken.load(std::memory_order_relaxed) + count, std::memory_order_relaxed);
}

// SwitchGpuPassProfiler: where the presenting thread spends a frame (Video::Present): its own work between two
// Presents, then in Present ImGui's frame, the wait for the render thread to record the frame, the present, the wait for
// the GPU to finish the frame slot it reuses, getting the next swap chain image and the frame limiter. At the 60 fps cap
// the frame rate hides CPU savings; the working time shows them. With SwitchPresentOnRenderThread the render thread adds
// its present, GPU wait and acquire (PresentOnRenderThread). It only reads the clock.
enum FrameTime : uint32_t
{
    FRAME_TIME_WORK,
    FRAME_TIME_IMGUI,
    FRAME_TIME_RENDER_THREAD,
    FRAME_TIME_PRESENT,
    FRAME_TIME_GPU,
    FRAME_TIME_ACQUIRE,
    FRAME_TIME_LIMITER,
    FRAME_TIME_COUNT
};

struct FrameTimeTotals
{
    double times[FRAME_TIME_COUNT]{};
    double longestWork = 0.0;
    uint32_t frames = 0;
    uint32_t pipelinedFrames = 0;
};

static std::mutex g_frameTimesMutex;
static FrameTimeTotals g_frameTimeTotals; // Under g_frameTimesMutex; the report takes and resets them.
static std::atomic<int64_t> g_frameTimeLastPresentEnd{ 0 }; // steady_clock ticks; 0 before the first Present.

// The laps of one Present (or of one PresentOnRenderThread): each Lap adds the time since the previous one.
struct FrameTimeLaps
{
    bool enabled;
    std::chrono::steady_clock::time_point start{};
    std::chrono::steady_clock::time_point lap{};
    double times[FRAME_TIME_COUNT]{};

    explicit FrameTimeLaps(bool enabled)
        : enabled(enabled)
    {
        if (enabled)
        {
            start = std::chrono::steady_clock::now();
            lap = start;
        }
    }

    void Lap(FrameTime time)
    {
        if (!enabled)
            return;

        const auto now = std::chrono::steady_clock::now();
        times[time] += std::chrono::duration<double, std::milli>(now - lap).count();
        lap = now;
    }

    // The end of a Present: its work time is the time since the previous Present ended.
    void FinishPresent(bool pipelined)
    {
        if (!enabled)
            return;

        const int64_t previousEnd = g_frameTimeLastPresentEnd.exchange(lap.time_since_epoch().count(), std::memory_order_relaxed);
        if (previousEnd == 0)
            return;

        times[FRAME_TIME_WORK] = std::chrono::duration<double, std::milli>(start.time_since_epoch() -
            std::chrono::steady_clock::duration(previousEnd)).count();

        std::lock_guard lock(g_frameTimesMutex);
        auto& totals = g_frameTimeTotals;
        for (uint32_t i = 0; i < FRAME_TIME_COUNT; i++)
            totals.times[i] += times[i];

        totals.longestWork = std::max(totals.longestWork, times[FRAME_TIME_WORK]);
        totals.frames++;
        if (pipelined)
            totals.pipelinedFrames++;
    }

    // The end of PresentOnRenderThread: its times belong to the frame its Present counted.
    void FinishRenderThreadPresent()
    {
        if (!enabled)
            return;

        std::lock_guard lock(g_frameTimesMutex);
        for (uint32_t i = 0; i < FRAME_TIME_COUNT; i++)
            g_frameTimeTotals.times[i] += times[i];
    }
};

static void AppendFormat(std::string& out, const char* format, ...) __attribute__((format(printf, 2, 3)));

static void AppendFormat(std::string& out, const char* format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    if (length > 0)
        out.append(text, std::min<size_t>(size_t(length), sizeof(text) - 1));
}

// The reports (pass and draw profilers, the frame log, SwitchResolveStats) go to stderr.log on the SD card. The render
// thread only formats them; this thread writes each one with a single write (os::switch_cpu_profiler::WriteLog: stderr
// is line-buffered, so an fwrite would be one SD card write per line, which stalled Unleashed's render thread for up to
// half a second per report). A pthread (runtime-created std::threads misbehave on this toolchain, apu/xma_decoder.h),
// created at the first report, never joined nor detached (pthread_detach fails on Horizon), at priority 0x3B.
struct ReportWriter
{
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> reports;
    pthread_t thread{};
    bool started = false;
    bool failed = false;
};

static ReportWriter g_reportWriter;

static void* ReportWriterThread(void*)
{
    SwitchSetCurrentThreadPriority(0x3B);

    // SwitchHostThreadCores: with the pipeline compiler threads, away from the game's main thread and the render thread.
    if (g_switchRenderer.hostThreadCores)
        SwitchRendererSetThreadCore(2);

    os::switch_cpu_profiler::RegisterCurrentThread("report writer");

    auto& writer = g_reportWriter;
    while (true)
    {
        std::vector<std::string> reports;
        {
            std::unique_lock lock(writer.mutex);
            writer.condition.wait(lock, [&writer] { return !writer.reports.empty(); });
            reports.swap(writer.reports);
        }

        for (const auto& report : reports)
            os::switch_cpu_profiler::WriteLog(report);
    }

    return nullptr;
}

static void WriteReportAsync(std::string report)
{
    auto& writer = g_reportWriter;
    std::unique_lock lock(writer.mutex);

    if (!writer.started && !writer.failed)
    {
        pthread_attr_t attributes;
        pthread_attr_init(&attributes);
        pthread_attr_setstacksize(&attributes, 0x10000);
        const int result = pthread_create(&writer.thread, &attributes, ReportWriterThread, nullptr);
        pthread_attr_destroy(&attributes);

        writer.started = (result == 0);
        writer.failed = (result != 0);
        if (writer.failed)
            fprintf(stderr, "Switch renderer: the report writer thread could not be created (error %d); reports are written directly.\n", result);
    }

    if (!writer.started)
    {
        lock.unlock();
        os::switch_cpu_profiler::WriteLog(report);
        return;
    }

    writer.reports.push_back(std::move(report));
    lock.unlock();
    writer.condition.notify_one();
}

static void PassProfilerOpen(const PassProfilerPass& pass)
{
    auto& frame = g_passProfilerFrames[g_frame];
    if (!g_passProfilerEnabled || frame.passes.size() >= PASS_PROFILER_MAX_PASSES)
        return;

    // Timestamp k opens pass k (and closes pass k - 1).
    g_commandLists[g_frame]->writeTimestamp(frame.queries.get(), uint32_t(frame.passes.size()));
    frame.passes.push_back(pass);
}

// Right after the command list began (BeginCommandList): pass 0 is what comes before the first framebuffer.
static void PassProfilerBeginFrame()
{
    auto& frame = g_passProfilerFrames[g_frame];
    frame.passes.clear();
    frame.draws.clear();
    frame.recorded = false;

    if (!g_passProfilerEnabled)
        return;

    // Queries may only be reset outside render passes, so every pool is reset here.
    g_commandLists[g_frame]->resetQueryPool(frame.queries.get(), 0, PASS_PROFILER_MAX_PASSES + 1);
    frame.start = PassProfilerReadCounters();
    if (g_drawProfilerEnabled)
    {
        for (auto& pool : frame.drawQueries)
            g_commandLists[g_frame]->resetQueryPool(pool.get(), 0, DRAW_PROFILER_POOL_SIZE);
    }

    PassProfilerOpen(PassProfilerPass{});
}

// Before every framebuffer the game's passes bind.
static void PassProfilerFramebuffer(const GuestSurface* renderTarget, const GuestSurface* depthStencil)
{
    if (!g_passProfilerEnabled)
        return;

    PassProfilerPass pass{};
    const GuestSurface* size = renderTarget != nullptr ? renderTarget : depthStencil;
    if (size != nullptr)
    {
        pass.width = size->width;
        pass.height = size->height;
        pass.samples = uint32_t(size->sampleCount);
    }

    pass.colorFormat = renderTarget != nullptr ? renderTarget->format : RenderFormat::UNKNOWN;
    pass.depthFormat = depthStencil != nullptr ? depthStencil->format : RenderFormat::UNKNOWN;
    PassProfilerOpen(pass);
}

static void FrameLogCopy(const GuestSurface* surface, const GuestTexture* texture, uint32_t trigger);

// Before every resolve copy (ExecutePendingCopy), with g_resolveCopyTrigger set by its caller.
static void PassProfilerCopy(const GuestSurface* surface, const GuestTexture* texture)
{
    FrameLogCopy(surface, texture, g_resolveCopyTrigger);

    auto& frame = g_passProfilerFrames[g_frame];
    if (!g_passProfilerEnabled)
        return;

    const uint32_t kind = PASS_PROFILER_COPIES + std::min<uint32_t>(g_resolveCopyTrigger, RESOLVE_COPY_TRIGGER_COUNT - 1);
    if (frame.passes.empty() || frame.passes.back().kind != kind)
    {
        // Depth surfaces are D32_FLOAT_S8_UINT here.
        const bool depth = RenderFormatIsDepth(surface->format);
        PassProfilerPass pass{};
        pass.width = surface->width;
        pass.height = surface->height;
        pass.samples = uint32_t(surface->sampleCount);
        pass.colorFormat = depth ? RenderFormat::UNKNOWN : surface->format;
        pass.depthFormat = depth ? surface->format : RenderFormat::UNKNOWN;
        pass.kind = kind;
        PassProfilerOpen(pass);
    }

    if (!frame.passes.empty() && frame.passes.back().kind == kind)
        frame.passes.back().draws++;
}

// Before the gamma pass's framebuffer (the swap chain image).
static void PassProfilerGamma(uint32_t width, uint32_t height)
{
    if (!g_passProfilerEnabled)
        return;

    PassProfilerPass pass{};
    pass.width = width;
    pass.height = height;
    pass.samples = 1;
    pass.colorFormat = BACKBUFFER_FORMAT;
    pass.depthFormat = RenderFormat::UNKNOWN;
    pass.draws = 1;
    pass.kind = PASS_PROFILER_GAMMA;
    PassProfilerOpen(pass);
}

// Before every draw of the game's passes (after its FlushRenderStateForRenderThread).
static void PassProfilerCountDraw()
{
    auto& frame = g_passProfilerFrames[g_frame];
    if (g_passProfilerEnabled && !frame.passes.empty())
        frame.passes.back().draws++;
}

// Before the frame's closing timestamp (ProcExecuteCommandList).
static void PassProfilerEndFrame()
{
    auto& frame = g_passProfilerFrames[g_frame];
    if (!g_passProfilerEnabled || frame.passes.empty())
        return;

    // Closing timestamp of the last pass. The unused slots get one too: plume reads the whole pool, and an unwritten
    // query would make the read fail (VK_NOT_READY). They are written back to back after the frame's work, so they cost
    // next to nothing.
    for (uint32_t i = uint32_t(frame.passes.size()); i <= PASS_PROFILER_MAX_PASSES; i++)
        g_commandLists[g_frame]->writeTimestamp(frame.queries.get(), i);

    // The same for the tail of the last draw pool in use; later pools are not read.
    if (g_drawProfilerEnabled && !frame.draws.empty())
    {
        const uint32_t used = uint32_t(frame.draws.size());
        auto* pool = frame.drawQueries[(used - 1) / DRAW_PROFILER_POOL_SIZE].get();
        for (uint32_t i = used % DRAW_PROFILER_POOL_SIZE; i != 0 && i < DRAW_PROFILER_POOL_SIZE; i++)
            g_commandLists[g_frame]->writeTimestamp(pool, i);
    }

    frame.counters = PassProfilerReadCounters().Since(frame.start);
    frame.recorded = true;
}

static const char* PassProfilerFormatName(RenderFormat format)
{
    switch (format)
    {
    case RenderFormat::UNKNOWN: return "-";
    case RenderFormat::R16G16B16A16_FLOAT: return "RGBA16F";
    case RenderFormat::R8G8B8A8_UNORM: return "RGBA8";
    case RenderFormat::B8G8R8A8_UNORM: return "BGRA8";
    case RenderFormat::R16G16_FLOAT: return "RG16F";
    case RenderFormat::R32_FLOAT: return "R32F";
    case RenderFormat::R8_UNORM: return "R8";
    case RenderFormat::D32_FLOAT: return "D32F";
    case RenderFormat::D32_FLOAT_S8_UINT: return "D32FS8";
    default: return "fmt";
    }
}

static const char* ResolveCopyTriggerName(uint32_t trigger)
{
    static constexpr const char* TRIGGERS[] = { "before-draw", "at-clear", "at-present", "other" };
    static_assert(std::size(TRIGGERS) == RESOLVE_COPY_TRIGGER_COUNT);
    return trigger < std::size(TRIGGERS) ? TRIGGERS[trigger] : "?";
}

// [Switch] SwitchFrameLog: the render thread's work of one frame, once a minute (at most five times), as "[frame]" lines
// in stderr.log: each framebuffer bound, clear (and what SwitchSkipOverwrittenClears made of it), resolve, resolve copy,
// hand-over, barrier batch, survey and conditional rendering change and draw (shaders, state, bound textures; skipped
// draws too), in order. To find what a frame does that it does not need to. Surfaces and textures are named by their
// address. Text only: the commands are the same. That one frame a minute costs the render thread some formatting.
struct FrameLog
{
    bool active = false;
    uint32_t logged = 0;
    uint32_t draws = 0;
    std::chrono::steady_clock::time_point next{};
    std::string text;
};

static FrameLog g_frameLog; // Render thread only.

static uint32_t FrameLogId(const void* object)
{
    return uint32_t(reinterpret_cast<uintptr_t>(object) & 0xFFFFFF);
}

// At the start of every command list (ProcBeginCommandList).
static void FrameLogBegin()
{
    if (!g_frameLogEnabled || g_frameLog.logged >= 5)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (g_frameLog.next.time_since_epoch().count() == 0)
        g_frameLog.next = now + std::chrono::seconds(60);

    if (now < g_frameLog.next)
        return;

    g_frameLog.next = now + std::chrono::seconds(60);
    g_frameLog.active = true;
    g_frameLog.draws = 0;
    g_frameLog.text.clear();
    g_frameLog.text.reserve(1 << 20);
    AppendFormat(g_frameLog.text, "[frame] log %u: the render thread's commands of one frame (P framebuffer, C clear, R resolve, "
        "X resolve copy, H hand-over, B barriers, S survey, Q conditional rendering, D draw)\n", g_frameLog.logged + 1);
}

// Before the frame's closing timestamp (ProcExecuteCommandList).
static void FrameLogEnd()
{
    if (!g_frameLog.active)
        return;

    g_frameLog.active = false;
    g_frameLog.logged++;
    AppendFormat(g_frameLog.text, "[frame] end of log %u: %u draws\n", g_frameLog.logged, g_frameLog.draws);
    WriteReportAsync(std::move(g_frameLog.text));
    g_frameLog.text = std::string();
}

static void FrameLogSurface(std::string& out, const char* label, const GuestBaseTexture* surface)
{
    if (surface == nullptr)
        AppendFormat(out, " %s -", label);
    else
        AppendFormat(out, " %s %06X %ux%u %s", label, FrameLogId(surface), surface->width, surface->height, PassProfilerFormatName(surface->format));
}

// `note`: how the framebuffer differs from the targets' plain one, or nullptr.
static void FrameLogPass(const GuestSurface* renderTarget, const GuestSurface* depthStencil, const char* note)
{
    if (!g_frameLog.active)
        return;

    g_frameLog.text += "[frame] P";
    FrameLogSurface(g_frameLog.text, "rt", renderTarget);
    FrameLogSurface(g_frameLog.text, "ds", depthStencil);
    if (note != nullptr)
        AppendFormat(g_frameLog.text, " (%s)", note);
    g_frameLog.text += '\n';
}

// A clear as the game sent it (ProcClear).
static void FrameLogClear(uint32_t flags, const float* color, float z, uint32_t stencil)
{
    if (!g_frameLog.active)
        return;

    AppendFormat(g_frameLog.text, "[frame] C flags %X colour %g %g %g %g z %g stencil %u", flags, color[0], color[1], color[2], color[3], z, stencil);
    FrameLogSurface(g_frameLog.text, "rt", g_renderTarget);
    FrameLogSurface(g_frameLog.text, "ds", g_depthStencil);
    g_frameLog.text += '\n';
}

// What happened to a clear later (SwitchSkipOverwrittenClears).
static void FrameLogClearOutcome(const char* outcome, const GuestSurface* surface)
{
    if (!g_frameLog.active)
        return;

    AppendFormat(g_frameLog.text, "[frame] C %s", outcome);
    FrameLogSurface(g_frameLog.text, "rt", surface);
    g_frameLog.text += '\n';
}

static void FrameLogResolve(const GuestSurface* surface, const GuestTexture* texture)
{
    if (!g_frameLog.active)
        return;

    g_frameLog.text += "[frame] R";
    FrameLogSurface(g_frameLog.text, "from", surface);
    FrameLogSurface(g_frameLog.text, "to", texture);
    g_frameLog.text += '\n';
}

static void FrameLogCopy(const GuestSurface* surface, const GuestTexture* texture, uint32_t trigger)
{
    if (!g_frameLog.active)
        return;

    AppendFormat(g_frameLog.text, "[frame] X %s", ResolveCopyTriggerName(trigger));
    FrameLogSurface(g_frameLog.text, "from", surface);
    FrameLogSurface(g_frameLog.text, "to", texture);
    g_frameLog.text += '\n';
}

// SwitchResolveHandOver / SwitchCoverageHandOver: the texture took the surface's image instead of a copy.
static void FrameLogHandOver(const GuestSurface* surface, const GuestTexture* texture, uint32_t trigger)
{
    if (!g_frameLog.active)
        return;

    AppendFormat(g_frameLog.text, "[frame] H %s", ResolveCopyTriggerName(trigger));
    FrameLogSurface(g_frameLog.text, "from", surface);
    FrameLogSurface(g_frameLog.text, "to", texture);
    g_frameLog.text += '\n';
}

static void FrameLogBarriers(size_t count)
{
    if (g_frameLog.active)
        AppendFormat(g_frameLog.text, "[frame] B %zu\n", count);
}

// 'S' (conditional survey) or 'Q' (conditional rendering), begun with `index` or ended.
static void FrameLogConditional(char kind, bool enabled, uint32_t index)
{
    if (!g_frameLog.active)
        return;

    if (enabled)
        AppendFormat(g_frameLog.text, "[frame] %c begin %u\n", kind, index);
    else
        AppendFormat(g_frameLog.text, "[frame] %c end\n", kind);
}
#endif

#if defined(__SWITCH__) && defined(SPEC_CONSTANT_SURVEY_SLOTS) && defined(MARATHON_RECOMP_SWITCH_SURVEY_SLOTS_SHADER)
// [Switch] SwitchSurveySlots. The port zeroed a survey index's counter in g_conditionalSurveyBuffer before each survey of
// it (a buffer created for 4 bytes, a barrier, a copy-engine transfer and a second barrier, the barriers ending the render
// pass), the survey pixel shader marked it, and the conditional rendering prologue of later draws discarded where it was
// still 0. The counter is only ever compared with 0, so instead each survey gets a slot of its own and a generation, a
// number that slot never held before: its pixel shader stores the generation (conditional_survey_slots_ps), and the
// prologue discards where the slot does not hold it (SPEC_CONSTANT_SURVEY_SLOTS, in every pipeline). "Not the
// generation" means no sample of that survey has written, which is what "0" meant; the survey and the prologue read and
// write the same way as before, unsynchronized (SurveyBarrierScope still covers the barriers after a survey).
//
// The shared constant words the port wrote follow the port: word 312 (the index the prologue and the survey shader read)
// gets the slot of the latest survey of the index the port put there (the survey's at Begin, the one of the index End
// sends, 0, at End), word 316 that survey's generation (the port wrote the conditional rendering index there, which no
// shader reads). An index never surveyed reads slot 0, which is never handed out and stays 0, with the generation
// 0xFFFFFFFF it never holds: it discards, as the port's counter discarded while it held the 0 a new buffer starts with.
// Slots are reused only once nothing can read them: no longer an index's latest, and every command list that could have
// referenced them finished (retired with the frame slot whose fence DestructTempResources follows). A generation is only
// ever larger than every value its slot held since it was last zeroed, so it is never there before the survey writes it.
#define MARATHON_RECOMP_SWITCH_SURVEY_SLOTS
static constexpr uint32_t SURVEY_SLOT_COUNT = 4096;
static constexpr uint32_t SURVEY_NEVER_SURVEYED = 0xFFFFFFFF;

struct SurveySlotState
{
    uint32_t lastSlot[CONDITIONAL_SURVEY_MAX]{};       // 0: never surveyed.
    uint32_t lastGeneration[CONDITIONAL_SURVEY_MAX]{};
    uint32_t generations[SURVEY_SLOT_COUNT]{};          // The last generation each slot was handed out with.
    std::deque<uint32_t> freeSlots;                     // Oldest first.
    std::vector<uint32_t> retiredSlots[NUM_FRAMES];     // No longer an index's latest, retired while recording that frame.
    bool bufferCleared = false;
};

static SurveySlotState g_surveySlotState;

// The frame slot's fence has passed (DestructTempResources): nothing reads its retired slots any more.
static void FreeRetiredSurveySlots()
{
    auto& state = g_surveySlotState;
    auto& retired = state.retiredSlots[g_frame];
    state.freeSlots.insert(state.freeSlots.end(), retired.begin(), retired.end());
    retired.clear();
}
#endif

static void DestructTempResources()
{
#if defined(MARATHON_RECOMP_SWITCH_SURVEY_SLOTS)
    if (g_surveySlots)
        FreeRetiredSurveySlots();
#endif

#if defined(__SWITCH__)
    for (uint32_t descriptorIndex : g_handOverDescriptors[g_frame])
        g_textureDescriptorAllocator.free(descriptorIndex);

    g_handOverDescriptors[g_frame].clear();
    g_handOverViews[g_frame].clear();
    g_handOverFramebuffers[g_frame].clear();
    g_retiredCascadeImages[g_frame].clear();
#endif

    for (auto resource : g_tempResources[g_frame])
    {
        switch (resource->type)
        {
        case ResourceType::Texture:
        case ResourceType::VolumeTexture:
        case ResourceType::ArrayTexture:
        {
            const auto texture = reinterpret_cast<GuestTexture*>(resource);

            if (texture->mappedMemory != nullptr) {
                g_userHeap.Free(texture->mappedMemory);
            }

            SetTextureDescriptor(texture->descriptorIndex, nullptr, 0, 0, {});
            g_textureDescriptorAllocator.free(texture->descriptorIndex);

            if (texture->patchedTexture != nullptr)
            {
                SetTextureDescriptor(texture->patchedTexture->descriptorIndex, nullptr, 0, 0, {});
                g_textureDescriptorAllocator.free(texture->patchedTexture->descriptorIndex);
            }

#if defined(__SWITCH__)
            if (texture->hadSurfaceImage)
            {
                ForgetFramebuffersOf(texture->texture);
                ForgetCoverageFramebuffersOf(texture->texture);
            }
#endif

            texture->~GuestTexture();
            break;
        }

        case ResourceType::VertexBuffer:
        case ResourceType::IndexBuffer:
        {
            const auto buffer = reinterpret_cast<GuestBuffer*>(resource);


            if (buffer->mappedMemory != nullptr)
                g_userHeap.Free(buffer->mappedMemory);

            buffer->~GuestBuffer();
            break;
        }

        case ResourceType::RenderTarget:
        case ResourceType::DepthStencil:
        {
            const auto surface = reinterpret_cast<GuestSurface*>(resource);

            if (surface->descriptorIndex != NULL)
            {
                SetTextureDescriptor(surface->descriptorIndex, nullptr, 0, 0, {});
                g_textureDescriptorAllocator.free(surface->descriptorIndex);
            }

#if defined(__SWITCH__)
            for (auto& depthReadView : surface->depthReadViews)
                g_textureDescriptorAllocator.free(depthReadView.descriptorIndex);

            // Depth copies the port kept pending past the Present after the surface's release (ForgetPendingResolves):
            // their textures, which the port left pointing at the destroyed surface, forget it. With the options that
            // keep copies pending, every released texture has already left its surface's list.
            if (UsesLongerPendingCopies())
            {
                for (const auto [pendingTexture, slice] : surface->destinationTextures)
                {
                    pendingTexture->sourceSurface = nullptr;
                    pendingTexture->copyOwed = false;
                    pendingTexture->pendingCarried = false;
                }

                surface->destinationTextures.clear();
            }

            g_liveSurfaces.erase(surface);
            ForgetFramebuffersOf(surface->texture);
            ForgetCoverageFramebuffersOf(surface->texture);
#endif

            surface->~GuestSurface();
            break;
        }

        case ResourceType::VertexDeclaration:
            reinterpret_cast<GuestVertexDeclaration*>(resource)->~GuestVertexDeclaration();
            break;

        case ResourceType::VertexShader:
        case ResourceType::PixelShader:
        {
            reinterpret_cast<GuestShader*>(resource)->~GuestShader();
            break;
        }
        }

        g_userHeap.Free(resource);
    }

    g_tempResources[g_frame].clear();
    g_tempBuffers[g_frame].clear();
}

static std::thread::id g_presentThreadId = std::this_thread::get_id();
static std::atomic<bool> g_readyForCommands;

// PPC_FUNC_IMPL(__imp__sub_824ECA00);
// PPC_FUNC(sub_824ECA00)
// {
//     g_readyForCommands.wait(false);
//     g_presentThreadId = std::this_thread::get_id();
//     __imp__sub_824ECA00(ctx, base);
// }

#if defined(__SWITCH__)
// [Switch] The D3D thread is the thread that owns the guest device: D3DDevice_AcquireThreadOwnership (sub_8253EB38)
// and ReleaseThreadOwnership (sub_8253EB78), see their hooks. The main thread renders and presents; while the game
// loads, it hands the device over to the loading thread, which draws and presents the loading screen, and takes it
// back afterwards. Only the D3D thread batches its render commands and filters its states; other threads send
// theirs directly to the queue, as before.
//
// The test compares the thread's TLS region (TPIDRRO_EL0: one register read, and every live thread has its own)
// with the owner's, instead of the thread id (pthread_self through newlib) or a thread-local variable (a call with
// -mtp=soft), on every render command. Static initialisation runs on the main thread, the first owner (the
// installer and the game's frames until its first hand-over). 0 while no thread owns the device.
static uintptr_t CurrentThreadTlsRegion()
{
    uintptr_t region;
    __asm__ ("mrs %x[data], tpidrro_el0" : [data] "=r" (region));
    return region;
}

static std::atomic<uintptr_t> g_presentThreadTlsRegion{ CurrentThreadTlsRegion() };

static bool IsPresentThread()
{
    return CurrentThreadTlsRegion() == g_presentThreadTlsRegion.load(std::memory_order_acquire);
}

static void FlushDeferredRenderCommands();

// StateFilter: changed by any other thread that sends a state, and by every change of D3D thread.
static std::atomic<uint32_t> g_stateFilterEpoch{ 0 };

// [Switch] SwitchPresentOnRenderThread. Present waits only until the render thread has recorded the frame; the render
// thread then submits it, presents, waits for the GPU to finish the frame slot it reuses and acquires the next image
// by itself (PresentOnRenderThread), while the D3D thread goes on with its next frame. Only for the game's own
// presents (the guest's Present, g_gamePresenting): the installer's frames stay as they were.
static std::atomic<bool> g_gamePresenting{ false };
static std::atomic<bool> g_recordedCommandList{ false };
// Frames whose present the render thread has still to finish. The D3D thread waits for them before it writes the
// memory of a buffer or texture again (see WaitForPresentTail), and every Present waits for them first.
static std::atomic<uint32_t> g_presentTailsSent{ 0 };
static std::atomic<uint32_t> g_presentTailsDone{ 0 };
#endif

static ankerl::unordered_dense::map<RenderTexture*, RenderTextureLayout> g_barrierMap;

static void AddBarrier(GuestBaseTexture* texture, RenderTextureLayout layout)
{
    if (texture != nullptr && texture->layout != layout)
    {
        g_barrierMap[texture->texture] = layout;
        texture->layout = layout;
    }
}

static std::vector<RenderTextureBarrier> g_barriers;

#if defined(__SWITCH__)
// [Switch] SwitchPreciseBarriers (plume::SetVulkanPreciseBarriers, switched on in SwitchPerfInitRenderer): the access
// masks of a barrier are derived from the layouts it moves between (the writes possible in the old one, every access
// possible in the new one) instead of "all memory" on both sides. On NVK that drops the shader cache flush and the
// cache invalidations each batch did for nothing; the stage masks, and with them the waits for idle, stay. Every data
// flow of the port has a barrier of its own with the right layouts, but one: the conditional survey's writes into
// g_conditionalSurveyBuffer, read by the conditional rendering prologue of later draws with nothing in between but the
// barriers other images happen to need. From a survey draw to the end of the next frame, every barrier the render
// thread records keeps the conservative masks (as all of them had before), so whatever made the counters visible to
// those reads still does, at the same place. (On Maxwell the flow needs no cache maintenance at all: NAK compiles the
// counter's loads, from a writable storage buffer, to L2-only LDG.CG, and its stores and atomics go to L2.)
static uint32_t g_renderFrameCounter = 0; // Command lists begun (BeginCommandList).
static uint32_t g_conservativeBarriersUntilFrame = 0;

// SwitchPreciseSurveyBarriers: a survey draw was recorded since the survey buffer's last barrier. The next batch of
// FlushBarriers carries a barrier of g_conditionalSurveyBuffer itself (conservative masks, the graphics stages it was
// written at as its source: see SurveyBufferBarrier), which makes the counter writes visible to everything recorded after
// it, the conditional rendering prologue's reads included; the batches after it keep the precise masks. Until then every
// barrier keeps the conservative masks, as all of them did before. Reads recorded before that batch (in the survey's own
// render pass) have no barrier before them either way. Render thread only.
static bool g_surveyWritesPending = false;

struct SurveyBarrierScope
{
    bool conservative;

    SurveyBarrierScope()
        : conservative(g_switchRenderer.preciseBarriers && (g_switchRenderer.preciseSurveyBarriers ? g_surveyWritesPending :
            g_renderFrameCounter <= g_conservativeBarriersUntilFrame))
    {
        if (conservative)
        {
            SetVulkanPreciseBarriers(false);
            g_resolveStats.conservativeBarrierBatches++;
        }
    }

    ~SurveyBarrierScope()
    {
        // Other threads' copy command lists may have recorded barriers meanwhile: conservative ones, which are
        // always right.
        if (conservative)
            SetVulkanPreciseBarriers(true);
    }
};

// SwitchEagerSampleTransitions: the colour surface the last draw rendered to.
static GuestSurface* g_lastColorSurface = nullptr;

// The viewport and scissor FlushViewport last gave the command list (valid until something else sets them, which also
// marks them dirty), for the coverage proofs (GetFullScreenCoverage).
static RenderViewport g_appliedViewport;
static RenderRect g_appliedScissor;
static bool g_appliedViewportValid = false;
static bool g_appliedScissorValid = false;
#endif

#if defined(__SWITCH__)
// SwitchPreciseSurveyBarriers: the survey buffer's barrier in a batch. plume takes a barrier's source stages from the
// stages of the buffer's previous barrier; the first time (none yet) a barrier of the buffer alone sets them to the
// graphics stages (where the survey's pixel shaders write it), so that every later one waits for those writes.
static RenderBufferBarrier SurveyBufferBarrier()
{
    static bool s_primed = false;
    if (!s_primed)
    {
        s_primed = true;
        g_commandLists[g_frame]->barriers(RenderBarrierStage::GRAPHICS,
            RenderBufferBarrier(g_conditionalSurveyBuffer.get(), RenderBufferAccess::READ | RenderBufferAccess::WRITE));
    }

    return RenderBufferBarrier(g_conditionalSurveyBuffer.get(), RenderBufferAccess::READ | RenderBufferAccess::WRITE);
}
#endif

static void FlushBarriers()
{
    if (!g_barrierMap.empty())
    {
        for (auto& [texture, layout] : g_barrierMap)
            g_barriers.emplace_back(texture, layout);

        {
#if defined(__SWITCH__)
            SurveyBarrierScope surveyBarrierScope;
            g_resolveStats.barrierBatches++;
            FrameLogBarriers(g_barriers.size());
            if (g_switchRenderer.preciseSurveyBarriers && g_surveyWritesPending && g_conditionalSurveyBuffer != nullptr)
            {
                const RenderBufferBarrier surveyBarrier = SurveyBufferBarrier();
                g_commandLists[g_frame]->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COPY, &surveyBarrier, 1,
                    g_barriers.data(), uint32_t(g_barriers.size()));
                g_surveyWritesPending = false;
            }
            else
#endif
            g_commandLists[g_frame]->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COPY, g_barriers);
        }

        g_barrierMap.clear();
        g_barriers.clear();
    }
}

// Zeroes `count` words of g_conditionalSurveyBuffer from word `first`, ordered after everything recorded before and
// before everything recorded after, as the port's reset of a counter at each survey was (the same barriers and copy).
static void ClearConditionalSurveyWords(uint32_t first, uint32_t count)
{
    auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(count * sizeof(uint32_t)));
    memset(uploadBuffer->map(), 0, count * sizeof(uint32_t));
    uploadBuffer->unmap();

    auto& commandList = g_commandLists[g_frame];
#if defined(__SWITCH__)
    SurveyBarrierScope surveyBarrierScope;
    g_rendererStats.surveyBufferResets++;
#endif
    commandList->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(g_conditionalSurveyBuffer.get(), RenderBufferAccess::WRITE));
    commandList->copyBufferRegion(g_conditionalSurveyBuffer->at(first * sizeof(uint32_t)), uploadBuffer->at(0), count * sizeof(uint32_t));
    commandList->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(g_conditionalSurveyBuffer.get(), RenderBufferAccess::READ | RenderBufferAccess::WRITE));
#if defined(__SWITCH__)
    // The copy's barriers above order the survey writes before it (conservative masks while they were pending).
    g_surveyWritesPending = false;
#endif

    g_tempBuffers[g_frame].emplace_back(std::move(uploadBuffer));
}

static std::unique_ptr<uint8_t[]> g_shaderCache;
static std::unique_ptr<uint8_t[]> g_buttonBcDiff;

static void LoadEmbeddedResources()
{
    switch (g_backend)
    {
    case Backend::VULKAN:
        g_shaderCache = std::make_unique<uint8_t[]>(g_spirvCacheDecompressedSize);
        ZSTD_decompress(g_shaderCache.get(), g_spirvCacheDecompressedSize, g_compressedSpirvCache, g_spirvCacheCompressedSize);
        break;
#if defined(MARATHON_RECOMP_D3D12)
    case Backend::D3D12:
        g_shaderCache = std::make_unique<uint8_t[]>(g_dxilCacheDecompressedSize);
        ZSTD_decompress(g_shaderCache.get(), g_dxilCacheDecompressedSize, g_compressedDxilCache, g_dxilCacheCompressedSize);
        break;
#elif defined(MARATHON_RECOMP_METAL)
    case Backend::METAL:
        g_shaderCache = std::make_unique<uint8_t[]>(g_airCacheDecompressedSize);
        ZSTD_decompress(g_shaderCache.get(), g_airCacheDecompressedSize, g_compressedAirCache, g_airCacheCompressedSize);
        break;
#endif
    default:
        assert(false);
    }

    g_buttonBcDiff = decompressZstd(g_button_bc_diff, g_button_bc_diff_uncompressed_size);
}

enum class CsdFilterState
{
    Unknown,
    On,
    Off
};

static CsdFilterState g_csdFilterState;

static ankerl::unordered_dense::set<GuestSurface*> g_pendingSurfaceCopies;
static ankerl::unordered_dense::set<GuestSurface*> g_pendingResolves;

#if defined(__SWITCH__)
// Render thread: the frame's command list is being recorded (from BeginCommandList to its end in ExecuteCommandList).
// Outside it g_textures still names the last frame's slots, which nothing samples before BeginCommandList resets them.
static bool g_commandListRecording = false;
#endif

enum class RenderCommandType
{
    SetRenderState,
    DestructResource,
    UnlockTextureRect,
    UnlockBuffer16,
    UnlockBuffer32,
    DrawImGui,
    ExecuteCommandList,
    BeginCommandList,
    StretchRect,
    SetRenderTarget,
    SetDepthStencilSurface,
    ExecutePendingStretchRectCommands,
    Clear,
    SetViewport,
    SetTexture,
    SetScissorRect,
    SetSamplerState,
    SetBooleans,
    SetVertexShaderConstants,
    SetPixelShaderConstants,
    AddPipeline,
    DrawPrimitive,
    DrawIndexedPrimitive,
    DrawPrimitiveUP,
    SetVertexDeclaration,
    SetVertexShader,
    SetStreamSource,
    SetIndices,
    SetPixelShader,
    SetConditionalSurvey,
    SetConditionalRendering,
#if defined(__SWITCH__)
    SetClipPlane,
    ExecuteCommandBatch,
    SignalFence,
    UnlockBufferSnapshot,
#endif
};

#if defined(__SWITCH__)
struct RenderCommandBatch;
#endif

struct RenderCommand
{
    RenderCommandType type;
    union
    {
#if defined(__SWITCH__)
        struct
        {
            // SwitchPresentOnRenderThread: the render thread presents and starts the next frame itself.
            bool pipelined;
            // SwitchPresentWithoutRecordWait: the D3D thread does not wait for this frame to be recorded, so the values
            // ProcExecuteCommandList reads from its state are taken here, when Present sends the command (they are what
            // the render thread read before: the D3D thread changed none of them until the frame was recorded).
            bool captured;
            float brightness;
            uint32_t viewportWidth;
            uint32_t viewportHeight;
        } executeCommandList;

        struct
        {
            // SwitchDeferredBufferUnlocks: the buffer's bytes at Unlock, in the frame's command memory.
            GuestBuffer* buffer;
            const uint8_t* snapshot;
            uint32_t elementSize;
        } unlockBufferSnapshot;

        struct
        {
            float plane[4];
        } setClipPlane;

        // SwitchZeroCopyBatches: `count` commands of a batch buffer, run in place.
        struct
        {
            RenderCommandBatch* batch;
            uint32_t count;
        } executeCommandBatch;

        // Every command sent before it has been processed (SyncWithRenderThread).
        struct
        {
            uint32_t value;
        } signalFence;
#endif

        struct
        {
            GuestRenderState type;
            uint32_t value;
        } setRenderState;

        struct 
        {
            GuestResource* resource;
        } destructResource;

        struct
        {
            GuestTexture* texture;
        } unlockTextureRect;

        struct
        {
            GuestBuffer* buffer;
        } unlockBuffer;

        struct 
        {
            GuestDevice* device;
            uint32_t flags;
            GuestTexture* texture;
            uint32_t destSliceOrFace;
        } stretchRect;

        struct 
        {
            GuestSurface* renderTarget;
        } setRenderTarget;

        struct 
        {
            GuestSurface* depthStencil;
        } setDepthStencilSurface;

        struct 
        {
            uint32_t flags;
            float color[4];
            float z;
            uint32_t stencil;
        } clear;

        struct 
        {
            float x;
            float y;
            float width;
            float height;
            float minDepth;
            float maxDepth;
        } setViewport;

        struct 
        {
            uint32_t index;
            GuestTexture* texture;
        } setTexture;

        struct 
        {
            int32_t left;
            int32_t top;
            int32_t right;
            int32_t bottom;
        } setScissorRect;

        struct
        {
            uint32_t index;
            uint32_t data0;
            uint32_t data3;
            uint32_t data5;
        } setSamplerState;

        struct
        {
            uint32_t booleans;
        } setBooleans;

        struct
        {
            uint8_t* memory;
            uint32_t index;
            uint32_t size;
        } setVertexShaderConstants;  
        
        struct
        {
            uint8_t* memory;
            uint32_t index;
            uint32_t size;
        } setPixelShaderConstants;

        struct
        {
            XXH64_hash_t hash;
            RenderPipeline* pipeline;
        } addPipeline;

        struct 
        {
            uint32_t primitiveType; 
            uint32_t startVertex; 
            uint32_t primitiveCount;
        } drawPrimitive;

        struct 
        {
            uint32_t primitiveType;
            int32_t baseVertexIndex; 
            uint32_t startIndex;
            uint32_t primCount;
        } drawIndexedPrimitive;

        struct 
        {
            uint32_t primitiveType;
            uint32_t primitiveCount; 
            uint8_t* vertexStreamZeroData;
            uint32_t vertexStreamZeroSize;
            uint32_t vertexStreamZeroStride;
            CsdFilterState csdFilterState;
        } drawPrimitiveUP;

        struct 
        {
            GuestVertexDeclaration* vertexDeclaration;
        } setVertexDeclaration;

        struct 
        {
            GuestShader* shader;
        } setVertexShader;

        struct 
        {
            uint32_t index;
            GuestBuffer* buffer;
            uint32_t offset;
            uint32_t stride;
        } setStreamSource;

        struct 
        {
            GuestBuffer* buffer;
        } setIndices;

        struct 
        {
            GuestShader* shader;
        } setPixelShader;

        struct
        {
            bool enabled;
            uint32_t index;
        } setConditionalSurvey;

        struct
        {
            bool enabled;
            uint32_t index;
        } setConditionalRendering;
    };
};

#if defined(__SWITCH__)
// moodycamel spins 10,000 times (tens of microseconds) before sleeping whenever the render thread catches up
// with the game thread. At priority 0x2C that spin is not time-sliced: it keeps the guest threads (0x3B) off
// that core. Spin ~1,000 times (a few microseconds) instead; the commands and their order are the same.
struct RenderQueueTraits : moodycamel::ConcurrentQueueDefaultTraits
{
    static const int MAX_SEMA_SPINS = 1000;
};

static moodycamel::BlockingConcurrentQueue<RenderCommand, RenderQueueTraits> g_renderQueue;
#else
static moodycamel::BlockingConcurrentQueue<RenderCommand> g_renderQueue;
#endif

#if defined(__SWITCH__)
// [Switch] SwitchBatchRenderCommands. The D3D thread's commands are gathered and handed to the render thread in
// batches instead of one enqueue per command. Every enqueue can wake the render thread when it went to sleep waiting
// for work, which happens between most draws: a kernel signal paid on the D3D thread, and a 0x2C thread that can
// preempt it. The order is unchanged: everything the D3D thread sends goes through the same batch. Every command
// either carries its data (constants and DrawPrimitiveUP vertices are copied into g_intermediaryUploadAllocator,
// which is reset only once the render thread has read the frame; clear, viewport, scissor and clip plane values are
// in the command) or is flushed before the game can change that data or waits for it: texture unlocks (the render
// thread copies the texture's memory), ImGui, the three Present commands and every change of D3D thread. Buffer
// unlocks are no render commands in this port: they write the GPU buffer on the calling thread, and the GPU reads
// it only once the frame is submitted, so batching changes nothing a draw reads. Other threads enqueue directly,
// as before: their order relative to the D3D thread was never defined (moodycamel orders the items of one producer
// only). All of this is defined before g_renderThread, which starts during static initialisation.
constexpr uint32_t RENDER_COMMAND_BATCH_SIZE = 512;

// [Switch] SwitchZeroCopyBatches. A batch is handed to the render thread as one command naming its buffer
// (ExecuteCommandBatch), which the render thread runs in place and then gives back, instead of being copied command
// by command into the queue and out of it again. The D3D thread then fills another buffer of the pool; when none is
// free (the render thread far behind), it copies the batch into the queue as before. Only the D3D thread takes
// buffers and only the render thread gives them back: a single-producer, single-consumer ring of free buffers (a
// change of D3D thread is ordered by the ownership hooks).
struct RenderCommandBatch
{
    RenderCommand commands[RENDER_COMMAND_BATCH_SIZE];
};

// A power of two, so the free-running counters index the ring continuously across their uint32 wrap (a ring of 33
// would map the entries either side of the wrap to the same slots). The ring never holds more than all the buffers,
// and fewer while one is being given back, so it needs no spare slot to tell full from empty.
constexpr uint32_t RENDER_COMMAND_BATCH_POOL = 32;
static_assert((RENDER_COMMAND_BATCH_POOL & (RENDER_COMMAND_BATCH_POOL - 1)) == 0, "the ring must be a power of two");

struct RenderCommandBatchPool
{
    RenderCommandBatch batches[RENDER_COMMAND_BATCH_POOL];
    RenderCommandBatch* free[RENDER_COMMAND_BATCH_POOL];
    std::atomic<uint32_t> head{ 0 }; // Next to take (D3D thread).
    std::atomic<uint32_t> tail{ 0 }; // Next to give back (render thread).

    RenderCommandBatchPool()
    {
        for (uint32_t i = 0; i < RENDER_COMMAND_BATCH_POOL; i++)
            free[i] = &batches[i];
        tail.store(RENDER_COMMAND_BATCH_POOL, std::memory_order_release);
    }

    RenderCommandBatch* Take()
    {
        const uint32_t index = head.load(std::memory_order_relaxed);
        if (index == tail.load(std::memory_order_acquire))
            return nullptr;

        RenderCommandBatch* batch = free[index & (RENDER_COMMAND_BATCH_POOL - 1)];
        head.store(index + 1, std::memory_order_release);
        return batch;
    }

    void GiveBack(RenderCommandBatch* batch)
    {
        const uint32_t index = tail.load(std::memory_order_relaxed);
        free[index & (RENDER_COMMAND_BATCH_POOL - 1)] = batch;
        tail.store(index + 1, std::memory_order_release);
    }
};

static RenderCommandBatchPool g_renderCommandBatchPool;

// D3D thread only. SwitchLargerCommandBatches and SwitchIdleRenderThreadBatches use more of the storage than the
// default 128 commands (g_renderCommandBatchCapacity). With SwitchZeroCopyBatches, `commands` is the pool buffer
// `batch` instead of `storage`.
struct DeferredRenderCommands
{
    RenderCommand storage[RENDER_COMMAND_BATCH_SIZE];
    RenderCommand* commands = storage;
    RenderCommandBatch* batch = nullptr;
    uint32_t count = 0;
};

static DeferredRenderCommands g_deferredRenderCommands;

// [Switch] SwitchRenderQueueToken: the D3D thread's batches go through a producer of their own instead of the queue's
// per-thread implicit producers, which moodycamel finds by a hash lookup of the thread on every enqueue. A producer
// takes one thread at a time: the ownership hooks hand it from one D3D thread to the next (release: flush and
// fence; acquire: fence, then claim), which also keeps a thread's direct commands (its own producer) in order with
// its batches (see the hooks).
static moodycamel::ProducerToken g_renderQueueToken(g_renderQueue);

// [Switch] SwitchLargerCommandBatches: a batch holds up to 256 commands and goes out once it has 128 at a draw,
// instead of 128 and 64 (LocalRenderCommandQueue::submit): half the hand-overs. SwitchIdleRenderThreadBatches: up to
// 512 commands; while the render thread waits for work (g_renderThreadWaiting, set around its wait) every hand-over
// wakes it with a system call made on the D3D thread, so the batch then goes out only when nearly full (448). Set in
// CreateHostDevice, before the first render command.
static uint32_t g_renderCommandBatchCapacity = 128;
static uint32_t g_renderCommandBatchThreshold = 64;
static std::atomic<bool> g_renderThreadWaiting{ false };

// Commands a batch collects before it goes out at a draw (SwitchBatchSeveralDraws).
static uint32_t RenderCommandBatchThreshold()
{
    if (g_switchRenderer.idleRenderThreadBatches && g_renderThreadWaiting.load(std::memory_order_relaxed))
        return g_renderCommandBatchCapacity - 64;

    return g_renderCommandBatchThreshold;
}

static void FlushDeferredRenderCommands()
{
    auto& deferred = g_deferredRenderCommands;
    if (deferred.count != 0)
    {
        if (g_passProfilerEnabled)
            AddGameThreadCount(g_profilerBatchesHandedOver, 1);

        // SwitchZeroCopyBatches: the buffer itself goes to the render thread, the D3D thread takes the next one.
        if (deferred.batch != nullptr)
        {
            if (RenderCommandBatch* next = g_renderCommandBatchPool.Take())
            {
                RenderCommand cmd;
                cmd.type = RenderCommandType::ExecuteCommandBatch;
                cmd.executeCommandBatch.batch = deferred.batch;
                cmd.executeCommandBatch.count = deferred.count;
                if (g_switchRenderer.renderQueueToken)
                    g_renderQueue.enqueue(g_renderQueueToken, cmd);
                else
                    g_renderQueue.enqueue(cmd);

                deferred.batch = next;
                deferred.commands = next->commands;
                deferred.count = 0;
                return;
            }
        }

        if (g_switchRenderer.renderQueueToken)
            g_renderQueue.enqueue_bulk(g_renderQueueToken, deferred.commands, deferred.count);
        else
            g_renderQueue.enqueue_bulk(deferred.commands, deferred.count);

        deferred.count = 0;
    }

    // The first buffer (or one again after the pool ran out while the batch was in the storage).
    if (g_switchRenderer.zeroCopyBatches && deferred.batch == nullptr)
    {
        if (RenderCommandBatch* batch = g_renderCommandBatchPool.Take())
        {
            deferred.batch = batch;
            deferred.commands = batch->commands;
        }
    }
}

static bool ShouldBatchRenderCommands()
{
    return g_switchRenderer.batchRenderCommands && IsPresentThread();
}

// Diagnostics of the hand-over of the device between threads (see the ownership hooks): the commands threads other
// than the D3D thread sent while batching was on, by type. Resource destruction and texture uploads come from
// loading threads anyway; any other command means a thread used the device without owning it.
static std::atomic<uint32_t> g_directRenderCommands[64];

static void CountDirectRenderCommand(RenderCommandType type)
{
    if (g_switchRenderer.batchRenderCommands && uint32_t(type) < std::size(g_directRenderCommands))
        g_directRenderCommands[uint32_t(type)].fetch_add(1, std::memory_order_relaxed);
}
#endif

// flush: the command must reach the render thread now (the caller waits for it, or it reads memory the game may
// change right after this call).
static void EnqueueRenderCommand(const RenderCommand& cmd, [[maybe_unused]] bool flush = false)
{
#if defined(__SWITCH__)
    if (ShouldBatchRenderCommands())
    {
        auto& batch = g_deferredRenderCommands;
        if (batch.count == g_renderCommandBatchCapacity)
            FlushDeferredRenderCommands();

        batch.commands[batch.count++] = cmd;

        if (flush)
            FlushDeferredRenderCommands();

        return;
    }

    CountDirectRenderCommand(cmd.type);
#endif
    g_renderQueue.enqueue(cmd);
}

#if defined(__SWITCH__)
// Fences (RenderCommandType::SignalFence) the render thread processes after every command sent before them: the
// changes of D3D thread use them (see the ownership hooks), and so does a D3D-thread read of the render thread's state.
// One at a time, under g_d3dThreadMutex, so the last one the render thread processed is this one or an older one.
static std::mutex g_d3dThreadMutex;
static uint32_t g_d3dThreadFenceValue = 0; // Under g_d3dThreadMutex.
static std::atomic<uint32_t> g_renderThreadFence{ 0 }; // The value of the last one processed.

// Returns once the render thread has processed every command this thread sent before, through the token (the D3D
// thread's batches) or through this thread's own producer. Call with g_d3dThreadMutex held.
static void SyncWithRenderThread(bool throughToken)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SignalFence;
    cmd.signalFence.value = ++g_d3dThreadFenceValue;

    if (throughToken)
        g_renderQueue.enqueue(g_renderQueueToken, cmd);
    else
        g_renderQueue.enqueue(cmd);

    uint32_t done = g_renderThreadFence.load(std::memory_order_acquire);
    while (done != cmd.signalFence.value)
    {
        g_renderThreadFence.wait(done, std::memory_order_acquire);
        done = g_renderThreadFence.load(std::memory_order_acquire);
    }
}

// For a D3D-thread read of the render thread's state: without batches, it saw every command the D3D thread had sent
// whenever the render thread had caught up. Batches hold commands back, so the batch is handed over and processed
// first.
static void CatchUpWithD3DThread()
{
    if (!ShouldBatchRenderCommands())
        return;

    std::lock_guard lock(g_d3dThreadMutex);
    FlushDeferredRenderCommands();
    SyncWithRenderThread(g_switchRenderer.renderQueueToken);
}
#endif

#if defined(__SWITCH__)
// [Switch] SwitchSkipRedundantRenderStates, SwitchSkipRedundantSamplerStates. The game sets most render and sampler
// states before every draw, mostly to the values they already have. The render thread applies each of the filtered
// ones as an assignment (ProcSetRenderState, ProcSetSamplerState: the same value again changes nothing), and nothing
// else writes what they set, so the D3D thread remembers the last value it sent of each and does not send it again:
// it arrives in order after the one sent before, so the render thread holds that value. Sampler states only while
// the anisotropic filtering setting they were converted with stays the same. A state sent by another thread (the
// queue does not order it against this thread's) or a change of D3D thread makes it forget everything, so the next
// value of each state is sent again.
struct StateFilter
{
    uint32_t epoch = ~0u;
    uint32_t renderStates[256];
    uint64_t renderStatesKnown[4]{};
    uint32_t samplerStates[16][3];
    uint32_t samplerStatesKnown = 0;
    uint32_t anisotropicFiltering = 0;
};

static StateFilter g_stateFilter; // D3D thread only.

// Whether the filter may be used on this thread; brings it up to date. Other threads make it forget.
static bool EnterStateFilter()
{
    if (!IsPresentThread())
    {
        g_stateFilterEpoch.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }

    auto& filter = g_stateFilter;
    const uint32_t epoch = g_stateFilterEpoch.load(std::memory_order_acquire);
    const uint32_t anisotropicFiltering = Config::AnisotropicFiltering;
    if (filter.epoch != epoch)
    {
        filter.epoch = epoch;
        std::fill(std::begin(filter.renderStatesKnown), std::end(filter.renderStatesKnown), 0);
        filter.samplerStatesKnown = 0;
    }

    if (filter.anisotropicFiltering != anisotropicFiltering)
    {
        filter.anisotropicFiltering = anisotropicFiltering;
        filter.samplerStatesKnown = 0;
    }

    return true;
}

// True when the render thread already has `value` for render state `type` (the command is not needed).
static bool RenderStateUnchanged(uint32_t type, uint32_t value)
{
    if (!EnterStateFilter() || type >= std::size(g_stateFilter.renderStates))
        return false;

    auto& filter = g_stateFilter;
    uint64_t& known = filter.renderStatesKnown[type / 64];
    const uint64_t bit = uint64_t(1) << (type % 64);
    if ((known & bit) != 0 && filter.renderStates[type] == value)
        return true;

    known |= bit;
    filter.renderStates[type] = value;
    return false;
}

// The render states the filter leaves alone: their effect depends on more than their value.
static constexpr bool IsFilteredRenderState(GuestRenderState type)
{
    switch (type)
    {
    // SetAlphaTestMode reads the render target's sample count at the time.
    case D3DRS_ALPHATESTENABLE:
    // Applied only while the draw at the time is not depth-only (the CSM:3 hack in ProcSetRenderState).
    case D3DRS_SCISSORTESTENABLE:
    // These also do renderTargetAndDepthStencil |= pipelineState even when unchanged, which makes the next
    // SetFramebuffer look the framebuffer up again. A draw finds the one already bound, but ProcDrawImGui asks for
    // the backbuffer alone, so without that look-up ImGui could draw into the framebuffer of the last draw.
    case D3DRS_ZENABLE:
    case D3DRS_STENCILENABLE:
    case D3DRS_COLORWRITEENABLE:
        return false;
    default:
        return true;
    }
}
#endif

template<GuestRenderState TType>
static void SetRenderState(GuestDevice* device, uint32_t value)
{
#if defined(__SWITCH__)
    if constexpr (IsFilteredRenderState(TType))
    {
        if (g_switchRenderer.skipRedundantRenderStates && RenderStateUnchanged(TType, value))
        {
            if (g_passProfilerEnabled)
                AddGameThreadCount(g_profilerStatesSkipped, 1);

            return;
        }
    }
#endif

    RenderCommand cmd;
    cmd.type = RenderCommandType::SetRenderState;
    cmd.setRenderState.type = TType;
    cmd.setRenderState.value = value;
    EnqueueRenderCommand(cmd);
}

static void SetRenderStateUnimplemented(GuestDevice* device, uint32_t value)
{
    LOGF_WARNING("{:x}\n", value);
}

static void SetAlphaTestMode(bool enable)
{
    uint32_t specConstants = 0;
    bool enableAlphaToCoverage = false;

    if (enable)
    {
        enableAlphaToCoverage = Config::TransparencyAntiAliasing && g_renderTarget != nullptr && g_renderTarget->sampleCount != RenderSampleCount::COUNT_1;

        if (enableAlphaToCoverage)
            specConstants = SPEC_CONSTANT_ALPHA_TO_COVERAGE;
        else
            specConstants = SPEC_CONSTANT_ALPHA_TEST;
    }

    specConstants |= (g_pipelineState.specConstants & ~(SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE));

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.enableAlphaToCoverage, enableAlphaToCoverage);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);
}

static RenderBlend ConvertBlendMode(uint32_t blendMode)
{
    switch (blendMode)
    {
    case D3DBLEND_ZERO:
        return RenderBlend::ZERO;
    case D3DBLEND_ONE:
        return RenderBlend::ONE;
    case D3DBLEND_SRCCOLOR:
        return RenderBlend::SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:
        return RenderBlend::INV_SRC_COLOR;
    case D3DBLEND_SRCALPHA:
        return RenderBlend::SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:
        return RenderBlend::INV_SRC_ALPHA;
    case D3DBLEND_DESTCOLOR:
        return RenderBlend::DEST_COLOR;
    case D3DBLEND_INVDESTCOLOR:
        return RenderBlend::INV_DEST_COLOR;
    case D3DBLEND_DESTALPHA:
        return RenderBlend::DEST_ALPHA;
    case D3DBLEND_INVDESTALPHA:
        return RenderBlend::INV_DEST_ALPHA;
    default:
        assert(false && "Invalid blend mode");
        return RenderBlend::ZERO;
    }
}

static RenderBlendOperation ConvertBlendOp(uint32_t blendOp)
{
    switch (blendOp)
    {
    case D3DBLENDOP_ADD:
        return RenderBlendOperation::ADD;
    case D3DBLENDOP_SUBTRACT:
        return RenderBlendOperation::SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT:
        return RenderBlendOperation::REV_SUBTRACT;
    case D3DBLENDOP_MIN:
        return RenderBlendOperation::MIN;
    case D3DBLENDOP_MAX:
        return RenderBlendOperation::MAX;
    default:
        assert(false && "Unknown blend operation");
        return RenderBlendOperation::ADD;
    }
}

static RenderComparisonFunction ConvertCompareFunc(uint32_t compareFunc)
{
    switch (compareFunc)
    {
    case D3DCMP_NEVER:
        return RenderComparisonFunction::NEVER;
    case D3DCMP_LESS:
        return RenderComparisonFunction::LESS;
    case D3DCMP_EQUAL:
        return RenderComparisonFunction::EQUAL;
    case D3DCMP_LESSEQUAL:
        return RenderComparisonFunction::LESS_EQUAL;
    case D3DCMP_GREATER:
        return RenderComparisonFunction::GREATER;
    case D3DCMP_NOTEQUAL:
        return RenderComparisonFunction::NOT_EQUAL;
    case D3DCMP_GREATEREQUAL:
        return RenderComparisonFunction::GREATER_EQUAL;
    case D3DCMP_ALWAYS:
        return RenderComparisonFunction::ALWAYS;
    default:
        assert(false && "Unknown comparison function");
        return RenderComparisonFunction::NEVER;
    }
}

static RenderStencilOp ConvertStencilOp(uint32_t stencilOp)
{
    switch (stencilOp)
    {
    case D3DSTENCILOP_KEEP:
        return RenderStencilOp::KEEP;
    case D3DSTENCILOP_ZERO:
        return RenderStencilOp::ZERO;
    case D3DSTENCILOP_REPLACE:
        return RenderStencilOp::REPLACE;
    case D3DSTENCILOP_INCRSAT:
        return RenderStencilOp::INCREMENT_AND_CLAMP;
    case D3DSTENCILOP_DECRSAT:
        return RenderStencilOp::DECREMENT_AND_CLAMP;
    case D3DSTENCILOP_INVERT:
        return RenderStencilOp::INVERT;
    case D3DSTENCILOP_INCR:
        return RenderStencilOp::INCREMENT_AND_WRAP;
    case D3DSTENCILOP_DECR:
        return RenderStencilOp::DECREMENT_AND_WRAP;
    default:
        assert(false && "Unknown stencil op");
        return RenderStencilOp::KEEP;
    }
}

static void ProcSetRenderState(const RenderCommand& cmd)
{
    uint32_t value = cmd.setRenderState.value;

    switch (cmd.setRenderState.type)
    {
    case D3DRS_ZENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zEnable, value != 0);
        g_dirtyStates.renderTargetAndDepthStencil |= g_dirtyStates.pipelineState;
        break;
    }
    case D3DRS_ZWRITEENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zWriteEnable, value != 0);
        break;
    }
    case D3DRS_STENCILENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilEnable, value != 0);
        g_dirtyStates.renderTargetAndDepthStencil |= g_dirtyStates.pipelineState;
        break;
    }
    case D3DRS_TWOSIDEDSTENCILMODE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilTwoSided, value != 0);
        break;
    }
    case D3DRS_ALPHATESTENABLE:
    {
        SetAlphaTestMode(value != 0);
        break;
    }
    case D3DRS_SRCBLEND:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.srcBlend, ConvertBlendMode(value));
        break;
    }
    case D3DRS_DESTBLEND:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.destBlend, ConvertBlendMode(value));
        break;
    }
    case D3DRS_CULLMODE:
    {
        RenderCullMode cullMode;

        switch (value) {
        case D3DCULL_NONE_CCW:
        case D3DCULL_NONE_CW:
            cullMode = RenderCullMode::NONE;
            break;
        case D3DCULL_FRONT_CCW:
        case D3DCULL_FRONT_CW:
            cullMode = RenderCullMode::FRONT;
            break;
        case D3DCULL_BACK_CCW:
        case D3DCULL_BACK_CW:
            cullMode = RenderCullMode::BACK;
            break;
        default:
            assert(false && "Invalid cull mode");
            cullMode = RenderCullMode::NONE;
            break;
        }

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.cullMode, cullMode);
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.frontFace, value < D3DCULL_NONE_CW ? RenderFrontFace::COUNTER_CLOCKWISE : RenderFrontFace::CLOCKWISE);
        break;
    }
    case D3DRS_ZFUNC:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zFunc, ConvertCompareFunc(value));
        break;
    }
    case D3DRS_STENCILFUNC:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilFunc, ConvertCompareFunc(value));
        break;
    }
    case D3DRS_STENCILFAIL:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilFail, ConvertStencilOp(value));
        break;
    }
    case D3DRS_STENCILZFAIL:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilZFail, ConvertStencilOp(value));
        break;
    }
    case D3DRS_STENCILPASS:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilPass, ConvertStencilOp(value));
        break;
    }
    case D3DRS_CCW_STENCILFUNC:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilFuncCCW, ConvertCompareFunc(value));
        break;
    }
    case D3DRS_CCW_STENCILFAIL:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilFailCCW, ConvertStencilOp(value));
        break;
    }
    case D3DRS_CCW_STENCILZFAIL:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilZFailCCW, ConvertStencilOp(value));
        break;
    }
    case D3DRS_CCW_STENCILPASS:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilPassCCW, ConvertStencilOp(value));
        break;
    }
    case D3DRS_STENCILREF:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilRef, value);
        break;
    }
    case D3DRS_STENCILMASK:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilMask, value);
        break;
    }
    case D3DRS_STENCILWRITEMASK:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.stencilWriteMask, value);
        break;
    }
    case D3DRS_ALPHAREF:
    {
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.alphaThreshold, float(value) / 256.0f);
        break;
    }
    case D3DRS_ALPHABLENDENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.alphaBlendEnable, value != 0);
        break;
    }
    case D3DRS_BLENDOP:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.blendOp, ConvertBlendOp(value));
        break;
    }
    case D3DRS_SCISSORTESTENABLE:
    {
        // HACK: Ignore scissor test on depth-only draws to allow CSM:3 to properly scale
        if (g_pipelineState.depthStencilFormat == RenderFormat::UNKNOWN || g_pipelineState.renderTargetFormat != RenderFormat::UNKNOWN)
            SetDirtyValue(g_dirtyStates.scissorRect, g_scissorTestEnable, value != 0);
        break;
    }
    case D3DRS_SLOPESCALEDEPTHBIAS:
    {
        if (g_capabilities.dynamicDepthBias)
            SetDirtyValue(g_dirtyStates.depthBias, g_slopeScaledDepthBias, *reinterpret_cast<float*>(&value));
        else 
            SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.slopeScaledDepthBias, *reinterpret_cast<float*>(&value));

        break;
    }
    case D3DRS_DEPTHBIAS:
    {
        if (g_capabilities.dynamicDepthBias)
            SetDirtyValue(g_dirtyStates.depthBias, g_depthBias, int32_t(*reinterpret_cast<float*>(&value) * (1 << 24)));
        else
            SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthBias, int32_t(*reinterpret_cast<float*>(&value)* (1 << 24)));

        break;
    }
    case D3DRS_SRCBLENDALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.srcBlendAlpha, ConvertBlendMode(value));
        break;
    }
    case D3DRS_DESTBLENDALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.destBlendAlpha, ConvertBlendMode(value));
        break;
    }
    case D3DRS_BLENDOPALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.blendOpAlpha, ConvertBlendOp(value));
        break;
    }
    case D3DRS_COLORWRITEENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.colorWriteEnable, value);
        g_dirtyStates.renderTargetAndDepthStencil |= g_dirtyStates.pipelineState;
        break;
    }
    case D3DRS_CLIPPLANEENABLE:
    {
        // HACK: Only check for clip pane 0
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlaneEnabled, (value & 1) == 1);
#if defined(__SWITCH__)
        // [Switch] SwitchClipDistanceSpecialization: the plane state also selects the pipeline (SanitizePipelineState).
        // A plain assignment from the value, like the shared constant's (the state filter relies on that).
        if (g_clipDistanceSpecialization)
            SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.clipPlaneEnabled, uint8_t((value & 1) == 1));
#endif
    }
    }
}

static const std::pair<GuestRenderState, PPCFunc*> g_setRenderStateFunctions[] =
{
    { D3DRS_ZENABLE, HostToGuestFunction<SetRenderState<D3DRS_ZENABLE>> },
    { D3DRS_ZWRITEENABLE, HostToGuestFunction<SetRenderState<D3DRS_ZWRITEENABLE>> },
    { D3DRS_ALPHATESTENABLE, HostToGuestFunction<SetRenderState<D3DRS_ALPHATESTENABLE>> },
    { D3DRS_SRCBLEND, HostToGuestFunction<SetRenderState<D3DRS_SRCBLEND>> },
    { D3DRS_DESTBLEND, HostToGuestFunction<SetRenderState<D3DRS_DESTBLEND>> },
    { D3DRS_CULLMODE, HostToGuestFunction<SetRenderState<D3DRS_CULLMODE>> },
    { D3DRS_ZFUNC, HostToGuestFunction<SetRenderState<D3DRS_ZFUNC>> },
    { D3DRS_ALPHAREF, HostToGuestFunction<SetRenderState<D3DRS_ALPHAREF>> },
    { D3DRS_ALPHABLENDENABLE, HostToGuestFunction<SetRenderState<D3DRS_ALPHABLENDENABLE>> },
    { D3DRS_BLENDOP, HostToGuestFunction<SetRenderState<D3DRS_BLENDOP>> },
    { D3DRS_SCISSORTESTENABLE, HostToGuestFunction<SetRenderState<D3DRS_SCISSORTESTENABLE>> },
    { D3DRS_SLOPESCALEDEPTHBIAS, HostToGuestFunction<SetRenderState<D3DRS_SLOPESCALEDEPTHBIAS>> },
    { D3DRS_DEPTHBIAS, HostToGuestFunction<SetRenderState<D3DRS_DEPTHBIAS>> },
    { D3DRS_SRCBLENDALPHA, HostToGuestFunction<SetRenderState<D3DRS_SRCBLENDALPHA>> },
    { D3DRS_DESTBLENDALPHA, HostToGuestFunction<SetRenderState<D3DRS_DESTBLENDALPHA>> },
    { D3DRS_BLENDOPALPHA, HostToGuestFunction<SetRenderState<D3DRS_BLENDOPALPHA>> },
    { D3DRS_COLORWRITEENABLE, HostToGuestFunction<SetRenderState<D3DRS_COLORWRITEENABLE>> },
    { D3DRS_STENCILENABLE, HostToGuestFunction<SetRenderState<D3DRS_STENCILENABLE>> },
    { D3DRS_TWOSIDEDSTENCILMODE, HostToGuestFunction<SetRenderState<D3DRS_TWOSIDEDSTENCILMODE>> },
    { D3DRS_STENCILFAIL, HostToGuestFunction<SetRenderState<D3DRS_STENCILFAIL>> },
    { D3DRS_STENCILZFAIL, HostToGuestFunction<SetRenderState<D3DRS_STENCILZFAIL>> },
    { D3DRS_STENCILPASS, HostToGuestFunction<SetRenderState<D3DRS_STENCILPASS>> },
    { D3DRS_STENCILFUNC, HostToGuestFunction<SetRenderState<D3DRS_STENCILFUNC>> },
    { D3DRS_STENCILREF, HostToGuestFunction<SetRenderState<D3DRS_STENCILREF>> },
    { D3DRS_STENCILMASK, HostToGuestFunction<SetRenderState<D3DRS_STENCILMASK>> },
    { D3DRS_STENCILWRITEMASK, HostToGuestFunction<SetRenderState<D3DRS_STENCILWRITEMASK>> },
    { D3DRS_CCW_STENCILFAIL, HostToGuestFunction<SetRenderState<D3DRS_CCW_STENCILFAIL>> },
    { D3DRS_CCW_STENCILZFAIL, HostToGuestFunction<SetRenderState<D3DRS_CCW_STENCILZFAIL>> },
    { D3DRS_CCW_STENCILPASS, HostToGuestFunction<SetRenderState<D3DRS_CCW_STENCILPASS>> },
    { D3DRS_CCW_STENCILFUNC, HostToGuestFunction<SetRenderState<D3DRS_CCW_STENCILFUNC>> },
    { D3DRS_CLIPPLANEENABLE, HostToGuestFunction<SetRenderState<D3DRS_CLIPPLANEENABLE>> }
};

static std::unique_ptr<RenderShader> g_copyShader;

static std::unique_ptr<RenderShader> g_copyColorShader;
static ankerl::unordered_dense::map<RenderFormat, std::unique_ptr<RenderPipeline>> g_copyColorPipelines;
static std::unique_ptr<RenderPipeline> g_copyDepthPipeline;
#if defined(__SWITCH__)
// SwitchDepthArrayTexturesD32: copy_depth into the depth array textures, which are D32_FLOAT (CreateTexture).
static std::unique_ptr<RenderPipeline> g_copyDepthPipelineD32;
#endif

static std::unique_ptr<RenderShader> g_resolveMsaaColorShaders[3];
static ankerl::unordered_dense::map<RenderFormat, std::array<std::unique_ptr<RenderPipeline>, 3>> g_resolveMsaaColorPipelines;
static std::unique_ptr<RenderPipeline> g_resolveMsaaDepthPipelines[3];

enum
{
    GAUSSIAN_BLUR_3X3,
    GAUSSIAN_BLUR_5X5,
    GAUSSIAN_BLUR_7X7,
    GAUSSIAN_BLUR_9X9,
    GAUSSIAN_BLUR_COUNT
};

static std::unique_ptr<GuestShader> g_gaussianBlurShaders[GAUSSIAN_BLUR_COUNT];

static std::unique_ptr<GuestShader> g_csdFilterShader;
static GuestShader* g_csdShader;

static std::unique_ptr<GuestShader> g_enhancedBurnoutBlurVSShader;
static std::unique_ptr<GuestShader> g_enhancedBurnoutBlurPSShader;

static std::unique_ptr<GuestShader> g_conditionalSurveyPSShader;

#if defined(MARATHON_RECOMP_D3D12)

#define CREATE_SHADER(NAME) \
    g_device->createShader( \
        (g_backend == Backend::VULKAN) ? g_##NAME##_spirv : g_##NAME##_dxil, \
        (g_backend == Backend::VULKAN) ? sizeof(g_##NAME##_spirv) : sizeof(g_##NAME##_dxil), \
        "shaderMain", \
        (g_backend == Backend::VULKAN) ? RenderShaderFormat::SPIRV : RenderShaderFormat::DXIL)

#elif defined(MARATHON_RECOMP_METAL)

#define CREATE_SHADER(NAME) \
    g_device->createShader( \
        (g_backend == Backend::VULKAN) ? g_##NAME##_spirv : g_##NAME##_air, \
        (g_backend == Backend::VULKAN) ? sizeof(g_##NAME##_spirv) : sizeof(g_##NAME##_air), \
        "shaderMain", \
        (g_backend == Backend::VULKAN) ? RenderShaderFormat::SPIRV : RenderShaderFormat::METAL)

#else

#define CREATE_SHADER(NAME) \
    g_device->createShader(g_##NAME##_spirv, sizeof(g_##NAME##_spirv), "shaderMain", RenderShaderFormat::SPIRV)

#endif

#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
// [Switch] A hand-written shader reads its constants through the uniform buffers when its SPIR-V declares set 5 (its
// constants follow SPEC_CONSTANT_CONSTANTS_UBO like the translated ones) or reads none through pointers. Only for
// shaders whose every pointer read is behind that bit: csd_filter_ps, for one, mixes both and must keep the pushes.
#define MARK_CONSTANTS_THROUGH_UBO(GUEST_SHADER, SPIRV) \
    (GUEST_SHADER)->constantsThroughUbo.store(switch_spirv::ReadsConstantsThroughUbo(SPIRV, sizeof(SPIRV), CONSTANTS_UBO_SET_INDEX), std::memory_order_release)

// Lazy push constants: with the uniform buffers the translated shaders never read the push constant pointers, so the
// pointers are only pushed before draws whose shaders still read them (the hand-written ones that were not
// converted), which saves up to three vkCmdPushConstants per draw and the root table update NVK does for each.
// Pending: the address of each block's last upload. Pushed: what the push constant bytes hold (0 = unknown).
static uint64_t g_pendingRootAddresses[3];
static uint64_t g_pushedRootAddresses[3];

// Wherever the push constant bytes are written by anything else (the port's copy draws, the gamma pass, ImGui's
// pipeline layout) and at every command list start.
static void InvalidatePushedRootAddresses()
{
    memset(g_pushedRootAddresses, 0, sizeof(g_pushedRootAddresses));
}

static bool ShaderReadsConstantsThroughUbo(const GuestShader* shader)
{
    return shader == nullptr || shader->constantsThroughUbo.load(std::memory_order_acquire);
}

static void PushRootAddressesIfNeeded()
{
    // The pixel shader the pipeline has: the survey's while a conditional survey runs (CreateGraphicsPipeline).
    const GuestShader* pixelShader = g_pipelineState.enableConditionalSurvey ? g_conditionalSurveyPSShader.get() : g_pipelineState.pixelShader;
    bool needed = !ShaderReadsConstantsThroughUbo(g_pipelineState.vertexShader) || !ShaderReadsConstantsThroughUbo(pixelShader);

#if defined(SPEC_CONSTANT_RELATIVE_FROM_MEMORY) && defined(SHADER_FLAG_RELATIVE_CONSTANTS)
    // SwitchIndexedConstantsFromMemory: these vertex shaders read their a0-indexed arrays through the pointer
    // (SanitizePipelineState gives their pipelines SPEC_CONSTANT_RELATIVE_FROM_MEMORY).
    const GuestShader* vertexShader = g_pipelineState.vertexShader;
    if (g_relativeFromMemory && vertexShader != nullptr && vertexShader->shaderCacheEntry != nullptr &&
        (vertexShader->shaderCacheEntry->flags & SHADER_FLAG_RELATIVE_CONSTANTS) != 0)
    {
        needed = true;
    }
#endif

    if (!needed)
        return;

    auto& commandList = g_commandLists[g_frame];
    for (size_t i = 0; i < 3; i++)
    {
        if (g_pendingRootAddresses[i] != g_pushedRootAddresses[i])
        {
            commandList->setGraphicsPushConstants(0, &g_pendingRootAddresses[i], 8 * i, 8);
            g_pushedRootAddresses[i] = g_pendingRootAddresses[i];
            g_rendererStats.rootAddressPushes++;
        }
    }
}

// Where the vertex, pixel and shared constant blocks were uploaded last, and what set 5 is bound to. One set only
// addresses its own 16 MB upload buffer, so the three blocks must be in the same one (FlushRenderStateForRenderThread).
struct ConstantsUboBinding
{
    RenderDescriptorSet* sets[3]{};
    uint32_t offsets[3]{};
    RenderDescriptorSet* boundSet = nullptr;
    uint32_t boundOffsets[3]{};

    void Record(size_t index, const UploadAllocation& allocation)
    {
        sets[index] = allocation.constantsSet;
        offsets[index] = uint32_t(allocation.offset);
    }

    bool InOneBuffer() const
    {
        return sets[0] == sets[1] && sets[1] == sets[2];
    }

    void Bind(RenderCommandList* commandList)
    {
        if (sets[0] == nullptr || !InOneBuffer())
            return;

        if (boundSet == sets[0] && memcmp(boundOffsets, offsets, sizeof(offsets)) == 0)
            return;

        commandList->setGraphicsDescriptorSetDynamic(sets[0], CONSTANTS_UBO_SET_INDEX, offsets, 3);
        boundSet = sets[0];
        memcpy(boundOffsets, offsets, sizeof(offsets));
    }
};

static ConstantsUboBinding g_constantsUboBinding;
#endif

#ifdef _WIN32
static bool DetectWine()
{
    HMODULE dllHandle = GetModuleHandle("ntdll.dll");
    return dllHandle != nullptr && GetProcAddress(dllHandle, "wine_get_version") != nullptr;
}
#endif

static constexpr size_t SAMPLER_DESCRIPTOR_SIZE = 1024;

static std::unique_ptr<GuestTexture> g_imFontTexture;
static std::unique_ptr<RenderPipelineLayout> g_imPipelineLayout;
static std::unique_ptr<RenderPipeline> g_imPipeline;
static std::unique_ptr<RenderPipeline> g_imAdditivePipeline;

template<typename T>
static void ExecuteCopyCommandList(const T& function)
{
    std::lock_guard lock(g_copyMutex);

    g_copyCommandList->begin();
    function();
    g_copyCommandList->end();
    g_copyQueue->executeCommandLists(g_copyCommandList.get(), g_copyCommandFence.get());
    g_copyQueue->waitForCommandFence(g_copyCommandFence.get());
}

static constexpr uint32_t PITCH_ALIGNMENT = 0x100;
static constexpr uint32_t PLACEMENT_ALIGNMENT = 0x200;

struct ImGuiPushConstants
{
    ImVec2 boundsMin{};
    ImVec2 boundsMax{};
    ImU32 gradientTopLeft{};
    ImU32 gradientTopRight{};
    ImU32 gradientBottomRight{};
    ImU32 gradientBottomLeft{};
    uint32_t shaderModifier{};
    uint32_t texture2DDescriptorIndex{};
    ImVec2 displaySize{};
    ImVec2 inverseDisplaySize{};
    ImVec2 origin{ 0.0f, 0.0f };
    ImVec2 scale{ 1.0f, 1.0f };
    ImVec2 proceduralOrigin{ 0.0f, 0.0f };
    float outline{};
};

extern ImFontBuilderIO g_fontBuilderIO;

static void CreateImGuiBackend()
{
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

#ifdef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    IM_DELETE(io.Fonts);
    io.Fonts = ImFontAtlasSnapshot::Load();
#else
    io.Fonts->AddFontDefault();
    ImFontAtlasSnapshot::GenerateGlyphRanges();
#endif

    InitImGuiUtils();
    OptionsMenu::Init();
    InstallerWizard::Init();

    ImGui_ImplSDL2_InitForOther(GameWindow::s_pWindow);

#ifdef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    g_imFontTexture = LoadTexture(
        decompressZstd(g_im_font_atlas_texture, g_im_font_atlas_texture_uncompressed_size).get(), g_im_font_atlas_texture_uncompressed_size);
#else
    io.Fonts->FontBuilderIO = &g_fontBuilderIO;
    io.Fonts->Build();

    g_imFontTexture = std::make_unique<GuestTexture>(ResourceType::Texture);

    uint8_t* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    RenderTextureDesc textureDesc;
    textureDesc.dimension = RenderTextureDimension::TEXTURE_2D;
    textureDesc.width = width;
    textureDesc.height = height;
    textureDesc.depth = 1;
    textureDesc.mipLevels = 1;
    textureDesc.arraySize = 1;
    textureDesc.format = RenderFormat::R8G8B8A8_UNORM;

    RenderTextureDesc achievedTextureDesc = textureDesc;
    g_imFontTexture->textureHolder = CreateTextureChecked(textureDesc, "imgui-font", &achievedTextureDesc);
    g_imFontTexture->texture = g_imFontTexture->textureHolder.get();

    uint32_t rowPitch = (width * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
    uint32_t slicePitch = (rowPitch * height + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);
    auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(slicePitch));
    uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

    if (rowPitch == (width * 4))
    {
        memcpy(mappedMemory, pixels, slicePitch);
    }
    else
    {
        for (size_t i = 0; i < height; i++)
        {
            memcpy(mappedMemory, pixels, width * 4);
            pixels += width * 4;
            mappedMemory += rowPitch;
        }
    }

    uploadBuffer->unmap();

    ExecuteCopyCommandList([&]
        {
            g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(g_imFontTexture->texture, RenderTextureLayout::COPY_DEST));

            g_copyCommandList->copyTextureRegion(
                RenderTextureCopyLocation::Subresource(g_imFontTexture->texture, 0),
                RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), RenderFormat::R8G8B8A8_UNORM, width, height, 1, rowPitch / 4, 0));
        });

    g_imFontTexture->layout = RenderTextureLayout::COPY_DEST;

    RenderTextureViewDesc textureViewDesc;
    textureViewDesc.format = textureDesc.format;
    textureViewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    textureViewDesc.mipLevels = 1;
    g_imFontTexture->textureView = g_imFontTexture->texture->createTextureView(textureViewDesc);

    g_imFontTexture->descriptorIndex = g_textureDescriptorAllocator.allocate();
    SetTextureDescriptor(g_imFontTexture->descriptorIndex, g_imFontTexture->texture, achievedTextureDesc.width, achievedTextureDesc.height,
        RenderTextureLayout::SHADER_READ, g_imFontTexture->textureView.get(), textureViewDesc.mipLevels);
#endif

    io.Fonts->SetTexID(g_imFontTexture.get());

    RenderPipelineLayoutBuilder pipelineLayoutBuilder;
    pipelineLayoutBuilder.begin(false, true);

    // Must match the heap's layout (g_textureDescriptorCount, set in CreateHostDevice before this runs): the
    // same descriptor set is bound with this layout.
    RenderDescriptorSetBuilder descriptorSetBuilder;
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addTexture(0, g_textureDescriptorCount);
    descriptorSetBuilder.end(true, g_textureDescriptorCount);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    descriptorSetBuilder.begin();
    descriptorSetBuilder.addSampler(0, SAMPLER_DESCRIPTOR_SIZE);
    descriptorSetBuilder.end(true, SAMPLER_DESCRIPTOR_SIZE);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    pipelineLayoutBuilder.addPushConstant(0, 2, sizeof(ImGuiPushConstants), RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);

    pipelineLayoutBuilder.end();
    g_imPipelineLayout = pipelineLayoutBuilder.create(g_device.get());

    auto vertexShader = CREATE_SHADER(imgui_vs);
    auto pixelShader = CREATE_SHADER(imgui_ps);

    RenderInputElement inputElements[3];
    inputElements[0] = RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32_FLOAT, 0, offsetof(ImDrawVert, pos));
    inputElements[1] = RenderInputElement("TEXCOORD", 0, 1, RenderFormat::R32G32_FLOAT, 0, offsetof(ImDrawVert, uv));
    inputElements[2] = RenderInputElement("COLOR", 0, 2, RenderFormat::R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col));

    RenderInputSlot inputSlot(0, sizeof(ImDrawVert));

    RenderGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.pipelineLayout = g_imPipelineLayout.get();
    pipelineDesc.vertexShader = vertexShader.get();
    pipelineDesc.pixelShader = pixelShader.get();
    pipelineDesc.renderTargetFormat[0] = BACKBUFFER_FORMAT;
    pipelineDesc.renderTargetBlend[0] = RenderBlendDesc::AlphaBlend();
    pipelineDesc.renderTargetCount = 1;
    pipelineDesc.inputElements = inputElements;
    pipelineDesc.inputElementsCount = std::size(inputElements);
    pipelineDesc.inputSlots = &inputSlot;
    pipelineDesc.inputSlotsCount = 1;
    g_imPipeline = g_device->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.renderTargetBlend[0].dstBlend = RenderBlend::ONE;
    g_imAdditivePipeline = g_device->createGraphicsPipeline(pipelineDesc);

#ifndef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    ImFontAtlasSnapshot snapshot;
    snapshot.Snap();

    FILE* file = fopen("im_font_atlas.bin", "wb");
    if (file)
    {
        fwrite(snapshot.data.data(), 1, snapshot.data.size(), file);
        fclose(file);
    }

    ddspp::Header header;
    ddspp::HeaderDXT10 headerDX10;
    ddspp::encode_header(ddspp::R8G8B8A8_UNORM, width, height, 1, ddspp::Texture2D, 1, 1, header, headerDX10);

    file = fopen("im_font_atlas.dds", "wb");
    if (file)
    {
        fwrite(&ddspp::DDS_MAGIC, 4, 1, file);
        fwrite(&header, sizeof(header), 1, file);
        fwrite(&headerDX10, sizeof(headerDX10), 1, file);
        fwrite(pixels, 4, width * height, file);
        fclose(file);
    }
#endif
}

static void CheckSwapChain()
{
    g_swapChain->setVsyncEnabled(Config::VSync);
#if defined(__SWITCH__)
    // Retry a failed acquire/present on the same swap chain; only recreate on a
    // genuine needsResize. This WSI needs every buffer-queue slot free to
    // recreate, which is not guaranteed mid-frame, so a recreate on a transient
    // failure can fail permanently and stall the display stack.
    g_swapChainValid = !g_swapChain->needsResize();
#else
    g_swapChainValid &= !g_swapChain->needsResize();
#endif

    if (!g_swapChainValid)
    {
        Video::WaitForGPU();
        g_backBuffer->framebuffers.clear();
        g_swapChainValid = g_swapChain->resize();
        g_needsResize = g_swapChainValid;
    }

    if (g_swapChainValid)
    {
        g_swapChainAcquireProfiler.Begin();
        g_swapChainValid = g_swapChain->acquireTexture(g_acquireSemaphores[g_frame].get(), &g_backBufferIndex);
        g_swapChainAcquireProfiler.End();
    }

    if (g_needsResize)
        Video::ComputeViewportDimensions();

    g_backBuffer->width = Video::s_viewportWidth;
    g_backBuffer->height = Video::s_viewportHeight;
}

static void BeginCommandList()
{
    g_renderTarget = g_backBuffer;
    g_depthStencil = nullptr;
    g_framebuffer = nullptr;

    g_pipelineState.renderTargetFormat = BACKBUFFER_FORMAT;
    g_pipelineState.depthStencilFormat = RenderFormat::UNKNOWN;

    if (g_swapChainValid)
    {
        uint32_t width = Video::s_viewportWidth;
        uint32_t height = Video::s_viewportHeight;

        if (g_intermediaryBackBufferTextureWidth != width ||
            g_intermediaryBackBufferTextureHeight != height)
        {
            if (g_intermediaryBackBufferTextureDescriptorIndex == NULL)
                g_intermediaryBackBufferTextureDescriptorIndex = g_textureDescriptorAllocator.allocate();

            Video::WaitForGPU(); // Fine to wait for GPU, this'll only happen during resize.

            const RenderTextureDesc intermediaryDesc = RenderTextureDesc::Texture2D(width, height, 1, BACKBUFFER_FORMAT, RenderTextureFlag::RENDER_TARGET);
            RenderTextureDesc intermediaryAchieved = intermediaryDesc;
            g_intermediaryBackBufferTexture = CreateTextureChecked(intermediaryDesc, "intermediary-backbuffer", &intermediaryAchieved);
            SetTextureDescriptor(g_intermediaryBackBufferTextureDescriptorIndex, g_intermediaryBackBufferTexture.get(),
                intermediaryAchieved.width, intermediaryAchieved.height, RenderTextureLayout::SHADER_READ, nullptr, intermediaryAchieved.mipLevels);

            g_intermediaryBackBufferTextureWidth = width;
            g_intermediaryBackBufferTextureHeight = height;

            g_backBuffer->framebuffers.clear();
        }

        g_backBuffer->texture = g_intermediaryBackBufferTexture.get();
    }
    else
    {
        g_backBuffer->texture = g_backBuffer->textureHolder.get();
    }

    g_backBuffer->layout = RenderTextureLayout::UNKNOWN;

    for (size_t i = 0; i < 16; i++)
    {
        g_sharedConstants.texture2DIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D;
        g_sharedConstants.texture2DArrayIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D_ARRAY;
        g_sharedConstants.textureCubeIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE;
#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
        UpdateTextureSlotTables(uint32_t(i));
#endif
    }

    memset(g_textures, 0, sizeof(g_textures));

    auto& commandList = g_commandLists[g_frame];

    commandList->begin();
#if defined(__SWITCH__)
    g_commandListRecording = true;

    if (g_switchRenderer.queryResetPerQuery)
    {
        // [Switch] SwitchQueryResetPerQuery. NVK resets several timestamp queries at once with a copy-engine
        // fill, which switches the channel to the copy engine and back at the start of every frame; a single
        // query is reset with a 3D-engine semaphore write. Each query is still reset before it is written.
        for (uint32_t i = 0; i < NUM_QUERIES; i++)
            commandList->resetQueryPool(g_queryPools[g_frame].get(), i, 1);
    }
    else
#endif
    {
        commandList->resetQueryPool(g_queryPools[g_frame].get(), 0, NUM_QUERIES);
    }
    commandList->writeTimestamp(g_queryPools[g_frame].get(), 0);
#if defined(__SWITCH__)
    PassProfilerBeginFrame();
#endif
    commandList->setGraphicsPipelineLayout(g_pipelineLayout.get());
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 1);
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 2);
    commandList->setGraphicsDescriptorSet(g_samplerDescriptorSet.get(), 3);
    commandList->setGraphicsDescriptorSet(g_conditionalSurveyDescriptorSet.get(), 4);

#if defined(MARATHON_RECOMP_SWITCH_SURVEY_SLOTS)
    // SwitchSurveySlots: every slot holds 0 before anything reads or writes the buffer (0 is no generation).
    if (g_surveySlots && !g_surveySlotState.bufferCleared)
    {
        ClearConditionalSurveyWords(0, SURVEY_SLOT_COUNT);
        g_surveySlotState.bufferCleared = true;
    }
#endif

#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    // A new command buffer: nothing is bound or pushed yet, and every constant block is uploaded again before the
    // first draw (all states are dirty).
    const uint32_t nullOffsets[3]{};
    commandList->setGraphicsDescriptorSetDynamic(g_constantsUboNullSet.get(), CONSTANTS_UBO_SET_INDEX, nullOffsets, 3);
    g_constantsUboBinding = {};
    InvalidatePushedRootAddresses();
#endif
#if defined(__SWITCH__)
    g_vertexConstantBytesUploaded = 0;
    g_pixelConstantBytesUploaded = 0;

    g_renderFrameCounter++;
    g_lastColorSurface = nullptr;
    g_appliedViewportValid = false;
    g_appliedScissorValid = false;
#endif

    g_readyForCommands = true;
    g_readyForCommands.notify_one();
}

template<typename T>
static void ApplyLowEndDefault(ConfigDef<T> &configDef, T newDefault, bool &changed)
{
    if (configDef.IsDefaultValue() && !configDef.IsLoadedFromConfig)
    {
        configDef = newDefault;
        changed = true;
    }
    
    configDef.DefaultValue = newDefault;
}

static void ApplyLowEndDefaults()
{
    bool changed = false;

    ApplyLowEndDefault(Config::AntiAliasing, EAntiAliasing::MSAA2x, changed);
    ApplyLowEndDefault(Config::ShadowResolution, EShadowResolution::x1024, changed);
    ApplyLowEndDefault(Config::ReflectionResolution, EReflectionResolution::Quarter, changed);
    ApplyLowEndDefault(Config::TransparencyAntiAliasing, false, changed);

    if (changed) 
    {
        Config::Save();
    }
}

#if defined(__SWITCH__)
static void ReportRendererState(std::string& out);
#endif

bool Video::CreateHostDevice(const char *sdlVideoDriver, bool graphicsApiRetry)
{
#if defined(__SWITCH__)
    // Before the first render command: a batch's size must not change while it gathers commands.
    g_renderCommandBatchCapacity = g_switchRenderer.idleRenderThreadBatches ? RENDER_COMMAND_BATCH_SIZE : g_switchRenderer.largerCommandBatches ? 256 : 128;
    g_renderCommandBatchThreshold = g_switchRenderer.largerCommandBatches ? 128 : 64;

    os::switch_stall_watch::SetStateReporter(ReportRendererState);
#endif

    for (uint32_t i = 0; i < 16; i++)
        g_inputSlots[i].index = i;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    GameWindow::Init(sdlVideoDriver);

#if defined(MARATHON_RECOMP_D3D12)
    g_backend = (DetectWine() || Config::GraphicsAPI == EGraphicsAPI::Vulkan) ? Backend::VULKAN : Backend::D3D12;
#elif defined(MARATHON_RECOMP_METAL)
    g_backend = Config::GraphicsAPI == EGraphicsAPI::Vulkan ? Backend::VULKAN : Backend::METAL;
#endif

    // Attempt to create the possible backends using a vector of function pointers. Whichever succeeds first will be the chosen API.
    using RenderInterfaceFunction = std::unique_ptr<RenderInterface>(void);
    std::vector<RenderInterfaceFunction *> interfaceFunctions;

#ifdef MARATHON_RECOMP_D3D12
    bool allowVulkanRedirection = true;

    if (graphicsApiRetry)
    {
        // If we are attempting to create again after a reboot due to a crash, swap the order.
        g_backend = (g_backend == Backend::VULKAN) ? Backend::D3D12 : Backend::VULKAN;

        // Don't allow redirection to Vulkan if we are retrying after a crash, 
        // so the user can at least boot the game with D3D12 if Vulkan fails to work.
        allowVulkanRedirection = false;
    }

    interfaceFunctions.push_back((g_backend == Backend::VULKAN) ? CreateVulkanInterfaceWrapper : CreateD3D12Interface);
    interfaceFunctions.push_back((g_backend == Backend::VULKAN) ? CreateD3D12Interface : CreateVulkanInterfaceWrapper);
#elif defined(MARATHON_RECOMP_METAL)
    interfaceFunctions.push_back((g_backend == Backend::VULKAN) ? CreateVulkanInterfaceWrapper : CreateMetalInterface);
    interfaceFunctions.push_back((g_backend == Backend::VULKAN) ? CreateMetalInterface : CreateVulkanInterfaceWrapper);
#else
    interfaceFunctions.push_back(CreateVulkanInterfaceWrapper);
#endif

    for (size_t i = 0; i < interfaceFunctions.size(); i++)
    {
        RenderInterfaceFunction* interfaceFunction = interfaceFunctions[i];

#ifdef MARATHON_RECOMP_D3D12
        // Wrap the device creation in __try/__except to survive from driver crashes.
        __try
#endif
        {
            g_interface = interfaceFunction();
            if (g_interface == nullptr)
            {
                continue;
            }

            g_device = g_interface->createDevice(Config::GraphicsDevice);
            if (g_device != nullptr)
            {
                const RenderDeviceDescription &deviceDescription = g_device->getDescription();
                
#if defined(MARATHON_RECOMP_D3D12)
                if (interfaceFunction == CreateD3D12Interface)
                {
                    if (allowVulkanRedirection)
                    {
                        bool redirectToVulkan = false;

                        // ...
                        // There used to be driver redirections here, but they are all free from Vulkan purgatory for now...
                        // ...

                        if (redirectToVulkan)
                        {
                            g_device.reset();
                            g_interface.reset();

                            // In case Vulkan fails to initialize, we will try D3D12 again afterwards, 
                            // just to get the game to boot. This only really happens in very old Intel GPU drivers.
                            if (g_backend != Backend::VULKAN)
                            {
                                interfaceFunctions.push_back(CreateD3D12Interface);
                                allowVulkanRedirection = false;
                            }

                            continue;
                        }
                    }
                }

                g_backend = (interfaceFunction == CreateVulkanInterfaceWrapper) ? Backend::VULKAN : Backend::D3D12;
#elif defined(MARATHON_RECOMP_METAL)
                g_backend = (interfaceFunction == CreateVulkanInterfaceWrapper) ? Backend::VULKAN : Backend::METAL;
#endif
                // Enable triangle strip workaround if we are on AMD, as there is a bug where
                // restart indices cause triangles to be culled incorrectly. Converting them to degenerate triangles fixes it.
                g_triangleStripWorkaround = (deviceDescription.vendor == RenderDeviceVendor::AMD);

                break;
            }
        }
#ifdef MARATHON_RECOMP_D3D12
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (graphicsApiRetry)
            {
                // If we were retrying, and this also failed, then we'll show the user neither of the graphics APIs succeeded.
                return false;
            }
            else
            {
                // If this is the first crash we ran into, reboot and try the other graphics API.
                os::process::StartProcess(os::process::GetExecutablePath(), { "--graphics-api-retry" });
                std::_Exit(0);
            }
        }
#endif
    }

    if (g_device == nullptr)
    {
        return false;
    }

#ifdef MARATHON_RECOMP_D3D12
    if (graphicsApiRetry)
    {
        // If we managed to create a device after retrying it in a reboot, remember the one we picked.
        Config::GraphicsAPI = g_backend == Backend::VULKAN ? EGraphicsAPI::Vulkan : EGraphicsAPI::D3D12;
    }
#endif

    g_capabilities = g_device->getCapabilities();

    LoadEmbeddedResources();

    constexpr uint64_t LowEndMemoryLimit = 2048ULL * 1024ULL * 1024ULL;
    RenderDeviceDescription deviceDescription = g_device->getDescription();
    bool lowEndType = deviceDescription.type != RenderDeviceType::UNKNOWN && deviceDescription.type != RenderDeviceType::DISCRETE;
    bool lowEndMemory = deviceDescription.dedicatedVideoMemory < LowEndMemoryLimit;
    bool lowEndUMA = deviceDescription.type == RenderDeviceType::UNKNOWN && g_capabilities.uma;
#if defined(__SWITCH__)
    // Force low-end defaults: the Switch GPU (Tegra X1) is low-end, but NVK
    // reports the ~5 GB process mapping budget as VRAM, so the heuristics above
    // misclassify it as high-end and default to unplayable settings.
    lowEndType = true;
#endif
    if (lowEndType || lowEndMemory || lowEndUMA)
    {
        // Switch to low end defaults if a non-discrete GPU was detected or a low amount of VRAM was detected.
        // Checking for UMA on D3D12 seems to be a reliable way to detect integrated GPUs.
        ApplyLowEndDefaults();
    }

    const RenderSampleCounts colourSampleCount = g_device->getSampleCountsSupported(RenderFormat::R16G16B16A16_FLOAT);
    const RenderSampleCounts depthSampleCount  = g_device->getSampleCountsSupported(RenderFormat::D32_FLOAT);
    const RenderSampleCounts commonSampleCount = colourSampleCount & depthSampleCount;

    // Disable specific MSAA levels if they are not supported.
    if ((commonSampleCount & RenderSampleCount::COUNT_2) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA2x);
    if ((commonSampleCount & RenderSampleCount::COUNT_4) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA4x);
    if ((commonSampleCount & RenderSampleCount::COUNT_8) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA8x);

#if defined(__SWITCH__)
    // MSAA hangs the GPU channel on NVK at the event renderer; keep it off.
    Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA2x);
    Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA4x);
    Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA8x);

    // 8192²x4 depth is a single 2 GB texture that cannot be allocated in one
    // piece; the downsized fallback then mismatches the guest's render size and
    // hangs the GPU. Cap shadows at 4096.
    Config::ShadowResolution.InaccessibleValues.emplace(EShadowResolution::x8192);
    Config::ShadowResolution.SnapToNearestAccessibleValue(false);
#endif

    // Set Anti-Aliasing to nearest supported level.
    Config::AntiAliasing.SnapToNearestAccessibleValue(false);

    g_queue = g_device->createCommandQueue(RenderCommandListType::DIRECT);

    for (auto& commandList : g_commandLists)
        commandList = g_queue->createCommandList();

    for (auto& commandFence : g_commandFences)
        commandFence = g_device->createCommandFence();

    for (auto& queryPool : g_queryPools)
        queryPool = g_device->createQueryPool(NUM_QUERIES);

#if defined(__SWITCH__)
    // [Switch] SwitchGpuPassProfiler, SwitchGpuDrawProfiler (which turns the pass profiler on as well), SwitchFrameLog:
    // before the first command list (BeginCommandList below).
    g_drawProfilerEnabled = g_switchRenderer.gpuDrawProfiler;
    g_passProfilerEnabled = g_switchRenderer.gpuPassProfiler || g_drawProfilerEnabled;
    g_gpuSlowFrameMs = g_passProfilerEnabled ? double(g_switchRenderer.gpuSlowFrameMs) : 0.0;
    g_frameLogEnabled = g_switchRenderer.frameLog;
    if (g_passProfilerEnabled)
    {
        for (auto& frame : g_passProfilerFrames)
        {
            frame.queries = g_device->createQueryPool(PASS_PROFILER_MAX_PASSES + 1);
            frame.passes.reserve(PASS_PROFILER_MAX_PASSES);

            if (g_drawProfilerEnabled)
            {
                for (auto& pool : frame.drawQueries)
                    pool = g_device->createQueryPool(DRAW_PROFILER_POOL_SIZE);
            }
        }

        if (g_drawProfilerEnabled)
        {
            g_shaderSpirvBlake3 = std::make_unique<std::atomic<uint32_t>[]>(g_shaderCacheEntryCount);
            fprintf(stderr, "GPU pass and draw profiler on: per-pass and per-draw GPU times every %u frames%s.\n",
                PASS_PROFILER_REPORT_FRAMES, _mesa_blake3_compute != nullptr ? "" : " (no BLAKE3 in this build: shaders named by hash)");
        }
        else
        {
            fprintf(stderr, "GPU pass profiler on: per-pass GPU times every %u frames.\n", PASS_PROFILER_REPORT_FRAMES);
        }
    }
#endif

    g_copyQueue = g_device->createCommandQueue(RenderCommandListType::COPY);
    g_copyCommandList = g_copyQueue->createCommandList();
    g_copyCommandFence = g_device->createCommandFence();

    if (g_backend == Backend::D3D12)
    {
        g_discardCommandList = g_queue->createCommandList();
        g_discardCommandFence = g_device->createCommandFence();
    }

    uint32_t bufferCount = 2;

    switch (Config::TripleBuffering)
    {
    case ETripleBuffering::Auto:
        switch (g_backend) {
        case Backend::VULKAN:
            // Defaulting to 3 is fine if presentWait as supported, as the maximum frame latency allowed is only 1.
            bufferCount = g_device->getCapabilities().presentWait ? 3 : 2;
            break;
        case Backend::D3D12:
            // Defaulting to 3 is fine on D3D12 thanks to flip discard model.
            bufferCount = 3;
            break;
        case Backend::METAL:
            bufferCount = 2;
            break;
        }

        break;
    case ETripleBuffering::On:
        bufferCount = 3;
        break;
    case ETripleBuffering::Off:
        bufferCount = 2;
        break;
    }

    RenderSwapChainDesc swapChainDesc;
    swapChainDesc.renderWindow = GameWindow::s_renderWindow;
    swapChainDesc.textureCount = bufferCount;
    swapChainDesc.format = BACKBUFFER_FORMAT;
    swapChainDesc.maxFrameLatency = Config::MaxFrameLatency;
    swapChainDesc.enablePresentWait = g_capabilities.presentWait;

    g_swapChain = g_queue->createSwapChain(swapChainDesc);
    g_swapChain->setVsyncEnabled(Config::VSync);
    g_swapChainValid = !g_swapChain->needsResize();

    for (auto& acquireSemaphore : g_acquireSemaphores)
        acquireSemaphore = g_device->createCommandSemaphore();
    
    for (auto& renderSemaphore : g_renderSemaphores)
        renderSemaphore = g_device->createCommandSemaphore();

    RenderPipelineLayoutBuilder pipelineLayoutBuilder;
    pipelineLayoutBuilder.begin(false, true);

#if defined(__SWITCH__)
    if (g_switchRenderer.compactTextureHeap)
    {
        g_textureDescriptorCount = TEXTURE_DESCRIPTOR_COMPACT_SIZE;
        g_textureDescriptorAllocator.limit = TEXTURE_DESCRIPTOR_COMPACT_SIZE;
    }
#endif

    RenderDescriptorSetBuilder descriptorSetBuilder;
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addTexture(0, g_textureDescriptorCount);
    descriptorSetBuilder.end(true, g_textureDescriptorCount);

    g_textureDescriptorSet = descriptorSetBuilder.create(g_device.get());
    
    for (size_t i = 0; i < TEXTURE_DESCRIPTOR_NULL_COUNT; i++)
    {
        auto& texture = g_blankTextures[i];
        auto& textureView = g_blankTextureViews[i];

        RenderTextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.format = RenderFormat::R8_UNORM;

        RenderTextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::ZERO, RenderSwizzle::ZERO, RenderSwizzle::ZERO, RenderSwizzle::ZERO);
        viewDesc.mipLevels = 1;

        switch (i)
        {
        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D:
            desc.dimension = RenderTextureDimension::TEXTURE_2D;
            desc.arraySize = 1;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
            break;

        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D_ARRAY:
            desc.dimension = RenderTextureDimension::TEXTURE_2D;
            desc.arraySize = 1;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
            break;

        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE:
            desc.dimension = RenderTextureDimension::TEXTURE_2D;
            desc.arraySize = 6;
            desc.flags = RenderTextureFlag::CUBE;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_CUBE;
            break;

        default:
            assert(false && "Unknown null descriptor dimension");
            break;
        }

        texture = CreateTextureChecked(desc, "null-descriptor");
        textureView = texture->createTextureView(viewDesc);

        SetTextureDescriptor(uint32_t(i), texture.get(), 1, 1, RenderTextureLayout::SHADER_READ, textureView.get(), 1);
    }

#if defined(__SWITCH__)
    g_nullTextureDescriptorsWritten = true;
#endif

    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addSampler(0, SAMPLER_DESCRIPTOR_SIZE);
    descriptorSetBuilder.end(true, SAMPLER_DESCRIPTOR_SIZE);
    
    g_samplerDescriptorSet = descriptorSetBuilder.create(g_device.get());
    auto& [descriptorIndex, sampler] = g_samplerStates[XXH3_64bits(&g_samplerDescs[0], sizeof(RenderSamplerDesc))];
    descriptorIndex = 1;
    sampler = g_device->createSampler(g_samplerDescs[0]);
    g_samplerDescriptorSet->setSampler(0, sampler.get());

    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    RenderBufferDesc conditionalSurveyBufferDesc;
    conditionalSurveyBufferDesc.size = CONDITIONAL_SURVEY_MAX * sizeof(uint32_t);
#if defined(MARATHON_RECOMP_SWITCH_SURVEY_SLOTS)
    // [Switch] SwitchSurveySlots: one word per slot, zeroed before the first command list's draws (BeginCommandList).
    // Every survey index starts never surveyed (slot 0 and SURVEY_NEVER_SURVEYED in words 312 and 316, see
    // SetConditionalSurveySlot), and every other slot is free.
    g_surveySlots = g_switchRenderer.surveySlots;
    if (g_surveySlots)
    {
        conditionalSurveyBufferDesc.size = SURVEY_SLOT_COUNT * sizeof(uint32_t);

        auto& state = g_surveySlotState;
        state = {};
        std::fill(std::begin(state.lastGeneration), std::end(state.lastGeneration), SURVEY_NEVER_SURVEYED);
        for (uint32_t slot = 1; slot < SURVEY_SLOT_COUNT; slot++)
            state.freeSlots.push_back(slot);

        g_sharedConstants.conditionalSurveyIndex = 0;
        g_sharedConstants.conditionalRenderingIndex = SURVEY_NEVER_SURVEYED;
        g_postMaskSpecConstants |= SPEC_CONSTANT_SURVEY_SLOTS;
    }
#elif defined(__SWITCH__)
    if (g_switchRenderer.surveySlots)
        fprintf(stderr, "Switch renderer: SwitchSurveySlots is unavailable (conditional_survey_slots_ps not generated, or shader_common.h has no SPEC_CONSTANT_SURVEY_SLOTS).\n");
#endif
    conditionalSurveyBufferDesc.heapType = RenderHeapType::DEFAULT;
    conditionalSurveyBufferDesc.flags = RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS;
    g_conditionalSurveyBuffer = g_device->createBuffer(conditionalSurveyBufferDesc);

    RenderDescriptorSetBuilder conditionalSurveyDescriptorSetBuilder;
    conditionalSurveyDescriptorSetBuilder.begin();
    conditionalSurveyDescriptorSetBuilder.addReadWriteStructuredBuffer(0);
    conditionalSurveyDescriptorSetBuilder.end();
    g_conditionalSurveyDescriptorSet = conditionalSurveyDescriptorSetBuilder.create(g_device.get());

    RenderBufferStructuredView conditionalSurveyStructuredView(sizeof(uint32_t));
    g_conditionalSurveyDescriptorSet->setBuffer(0, g_conditionalSurveyBuffer.get(), 0, &conditionalSurveyStructuredView);

    pipelineLayoutBuilder.addDescriptorSet(conditionalSurveyDescriptorSetBuilder);

#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    g_constantsUbo = g_switchRenderer.constantsUbo;
    if (g_constantsUbo)
        g_postMaskSpecConstants |= SPEC_CONSTANT_CONSTANTS_UBO;

    // Set 5: the vertex, pixel and shared constant blocks (bindings 0/1/2), after the survey buffer's set 4.
    g_constantsUboSetBuilder.begin();
    g_constantsUboSetBuilder.addConstantBufferDynamic(0);
    g_constantsUboSetBuilder.addConstantBufferDynamic(1);
    g_constantsUboSetBuilder.addConstantBufferDynamic(2);
    g_constantsUboSetBuilder.end();
    pipelineLayoutBuilder.addDescriptorSet(g_constantsUboSetBuilder);

    const uint32_t constantsUboNullSize = uint32_t(std::max({ sizeof(g_vertexShaderConstants), sizeof(g_pixelShaderConstants), sizeof(SharedConstants) }));
    g_constantsUboNullBuffer = g_device->createBuffer(RenderBufferDesc::DefaultBuffer(constantsUboNullSize, RenderBufferFlag::CONSTANT));
    g_constantsUboNullSet = g_constantsUboSetBuilder.create(g_device.get());
    g_constantsUboNullSet->setBuffer(0, g_constantsUboNullBuffer.get(), sizeof(g_vertexShaderConstants));
    g_constantsUboNullSet->setBuffer(1, g_constantsUboNullBuffer.get(), sizeof(g_pixelShaderConstants));
    g_constantsUboNullSet->setBuffer(2, g_constantsUboNullBuffer.get(), sizeof(SharedConstants));
#elif defined(__SWITCH__)
    if (g_switchRenderer.constantsUbo)
        fprintf(stderr, "Switch renderer: SwitchConstantsUBO is unavailable (shader_common.h has no SPEC_CONSTANT_CONSTANTS_UBO).\n");
#endif

#if defined(__SWITCH__)
    // [Switch] Renderer stage 5: the translated shaders' other specialization bits (shader_common.h). Decided here, before
    // the first pipeline; every pipeline gets them through SanitizePipelineState. Without the translator's support (an
    // unpatched shader_common.h) the options stay off.
#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
    if (g_switchRenderer.textureSizeConstants)
        g_postMaskSpecConstants |= SPEC_CONSTANT_TEXTURE_SIZE;
    if (g_switchRenderer.verifyTextureSizes)
        g_postMaskSpecConstants |= SPEC_CONSTANT_TEXTURE_SIZE_VERIFY;
    if (g_switchRenderer.shadowGather)
        g_postMaskSpecConstants |= SPEC_CONSTANT_SHADOW_GATHER;
    if (g_switchRenderer.verifyShadowGather)
        g_postMaskSpecConstants |= SPEC_CONSTANT_SHADOW_GATHER_VERIFY;

    g_textureSlotTables = (g_postMaskSpecConstants & (SPEC_CONSTANT_TEXTURE_SIZE | SPEC_CONSTANT_TEXTURE_SIZE_VERIFY |
        SPEC_CONSTANT_SHADOW_GATHER | SPEC_CONSTANT_SHADOW_GATHER_VERIFY)) != 0;
    g_shadowGatherSpecialization = g_switchRenderer.shadowGatherSpecialization && (g_postMaskSpecConstants & SPEC_CONSTANT_SHADOW_GATHER) != 0;
    for (size_t i = 0; i < g_shaderCacheEntryCount; i++)
        g_gatherSlotsRead |= g_shaderCacheEntries[i].gatherSlots;
#else
    if (g_switchRenderer.textureSizeConstants || g_switchRenderer.verifyTextureSizes || g_switchRenderer.shadowGather)
        fprintf(stderr, "Switch renderer: SwitchTextureSizeConstants and SwitchShadowGather are unavailable (shader_common.h has no shared constant tables).\n");
#endif

#if defined(SPEC_CONSTANT_ALPHA_TEST_EARLY_OUT) && defined(SPEC_CONSTANT_ALPHA_TEST_SINK) && defined(SPEC_CONSTANT_BLEND_SKIP_ALPHA) && \
    defined(SPEC_CONSTANT_BLEND_SKIP_ZERO)
    g_alphaTestEarlyOutBit = g_switchRenderer.alphaTestEarlyOut ? SPEC_CONSTANT_ALPHA_TEST_EARLY_OUT : 0;
    g_alphaTestSinkBit = g_switchRenderer.alphaTestSink ? SPEC_CONSTANT_ALPHA_TEST_SINK : 0;
    g_skipTransparentPixels = g_switchRenderer.skipTransparentPixels;
#endif
#if defined(VERTEX_SWAPS_SPECIALIZED)
    g_vertexSwapSpecialization = g_switchRenderer.vertexSwapSpecialization;
#endif
#if defined(SPEC_CONSTANT_NO_CLIP_DISTANCE) && defined(SHADER_FLAG_CLIP_DISTANCE_SPECIALIZED)
    g_clipDistanceSpecialization = g_switchRenderer.clipDistanceSpecialization;
#endif
#if defined(SPEC_CONSTANT_RELATIVE_FROM_MEMORY) && defined(SHADER_FLAG_RELATIVE_CONSTANTS) && defined(MARATHON_RECOMP_SWITCH_CONSTANTS_UBO)
    // Only differs from the uniform buffers, so only with them.
    g_relativeFromMemory = g_constantsUbo && g_switchRenderer.indexedConstantsFromMemory;
#endif
#endif

    if (g_backend != Backend::D3D12)
    {
        pipelineLayoutBuilder.addPushConstant(0, 4, 24, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
    }
    else
    {
        pipelineLayoutBuilder.addRootDescriptor(0, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addRootDescriptor(1, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addRootDescriptor(2, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addPushConstant(3, 4, 4, RenderShaderStageFlag::PIXEL); // For copy/resolve shaders.
    }
    pipelineLayoutBuilder.end();
    
    g_pipelineLayout = pipelineLayoutBuilder.create(g_device.get());

    g_copyShader = CREATE_SHADER(copy_vs);
    g_copyColorShader = CREATE_SHADER(copy_color_ps);
    auto copyDepthShader = CREATE_SHADER(copy_depth_ps);

    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = g_copyShader.get();
    desc.pixelShader = copyDepthShader.get();
    desc.depthFunction = RenderComparisonFunction::ALWAYS;
    desc.depthEnabled = true;
    desc.depthWriteEnabled = true;
    desc.depthTargetFormat = RenderFormat::D32_FLOAT_S8_UINT;
    g_copyDepthPipeline = g_device->createGraphicsPipeline(desc);

#if defined(__SWITCH__)
    if (g_switchRenderer.depthArrayTexturesD32 || g_switchRenderer.depthTexturesD32)
    {
        desc.depthTargetFormat = RenderFormat::D32_FLOAT;
        g_copyDepthPipelineD32 = g_device->createGraphicsPipeline(desc);
    }
#endif

    g_resolveMsaaColorShaders[0] = CREATE_SHADER(resolve_msaa_color_2x);
    g_resolveMsaaColorShaders[1] = CREATE_SHADER(resolve_msaa_color_4x);
    g_resolveMsaaColorShaders[2] = CREATE_SHADER(resolve_msaa_color_8x);

    for (size_t i = 0; i < std::size(g_resolveMsaaDepthPipelines); i++)
    {
        std::unique_ptr<RenderShader> pixelShader;
        switch (i)
        {
        case 0:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_2x);
            break;
        case 1:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_4x);
            break;
        case 2:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_8x);
            break;
        }

        desc = {};
        desc.pipelineLayout = g_pipelineLayout.get();
        desc.vertexShader = g_copyShader.get();
        desc.pixelShader = pixelShader.get();
        desc.depthFunction = RenderComparisonFunction::ALWAYS;
        desc.depthEnabled = true;
        desc.depthWriteEnabled = true;
        desc.depthTargetFormat = RenderFormat::D32_FLOAT_S8_UINT;
        g_resolveMsaaDepthPipelines[i] = g_device->createGraphicsPipeline(desc);
    }

    for (auto& shader : g_gaussianBlurShaders)
        shader = std::make_unique<GuestShader>(ResourceType::PixelShader);

    g_gaussianBlurShaders[GAUSSIAN_BLUR_3X3]->shader = CREATE_SHADER(gaussian_blur_3x3);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_5X5]->shader = CREATE_SHADER(gaussian_blur_5x5);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_7X7]->shader = CREATE_SHADER(gaussian_blur_7x7);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_9X9]->shader = CREATE_SHADER(gaussian_blur_9x9);

    g_csdFilterShader = std::make_unique<GuestShader>(ResourceType::PixelShader);
    g_csdFilterShader->shader = CREATE_SHADER(csd_filter_ps);

    g_enhancedBurnoutBlurVSShader = std::make_unique<GuestShader>(ResourceType::VertexShader);
    g_enhancedBurnoutBlurVSShader->shader = CREATE_SHADER(enhanced_burnout_blur_vs);

    g_enhancedBurnoutBlurPSShader = std::make_unique<GuestShader>(ResourceType::PixelShader);
    g_enhancedBurnoutBlurPSShader->shader = CREATE_SHADER(enhanced_burnout_blur_ps);

    g_conditionalSurveyPSShader = std::make_unique<GuestShader>(ResourceType::PixelShader);
#if defined(__SWITCH__)
    {
        const uint8_t* surveySpirv = g_conditional_survey_ps_spirv;
        size_t surveySpirvSize = sizeof(g_conditional_survey_ps_spirv);
#if defined(MARATHON_RECOMP_SWITCH_SURVEY_STORE_SHADER)
        // [Switch] SwitchSurveyPlainStore (conditional_survey_store_ps.hlsl): every fragment that passes the depth
        // test stores 1 into the survey's counter instead of adding 1 to it; all of them hit the same address, so
        // the atomics were serialised. The counter is only ever compared with zero (the conditional rendering
        // prologue; nothing reads it back), and it is zero before the first such fragment either way.
        if (g_switchRenderer.surveyPlainStore)
        {
            surveySpirv = g_conditional_survey_store_ps_spirv;
            surveySpirvSize = sizeof(g_conditional_survey_store_ps_spirv);
        }
#else
        if (g_switchRenderer.surveyPlainStore)
            fprintf(stderr, "Switch renderer: SwitchSurveyPlainStore is unavailable (conditional_survey_store_ps not generated).\n");
#endif
#if defined(MARATHON_RECOMP_SWITCH_SURVEY_SLOTS)
        // [Switch] SwitchSurveySlots (conditional_survey_slots_ps.hlsl): every fragment that passes the depth test stores
        // the survey's generation into its slot (SetConditionalSurveySlot), whatever SwitchSurveyPlainStore says.
        if (g_surveySlots)
        {
            surveySpirv = g_conditional_survey_slots_ps_spirv;
            surveySpirvSize = sizeof(g_conditional_survey_slots_ps_spirv);
        }
#endif
        g_conditionalSurveyPSShader->shader = g_device->createShader(surveySpirv, surveySpirvSize, "shaderMain", RenderShaderFormat::SPIRV);

        // The survey pipelines use this shader instead of the game's (CreateGraphicsPipeline): its only constant is
        // g_conditionalSurveyIndex, read through shader_common.h, and it reads no interpolator.
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
        g_conditionalSurveyPSShader->constantsThroughUbo.store(
            switch_spirv::ReadsConstantsThroughUbo(surveySpirv, surveySpirvSize, CONSTANTS_UBO_SET_INDEX), std::memory_order_release);
#endif
        g_conditionalSurveyPSShader->inputLocationsRead.store(switch_spirv::InputLocationsRead(surveySpirv, surveySpirvSize), std::memory_order_release);
    }
#else
    g_conditionalSurveyPSShader->shader = CREATE_SHADER(conditional_survey_ps);
#endif

    CreateImGuiBackend();

#if defined(__SWITCH__) && defined(MARATHON_RECOMP_SWITCH_GAMMA_PUSH_SHADER)
    // [Switch] SwitchGammaPushConstants (gamma_correction_push_ps.hlsl): see ProcExecuteCommandList.
    g_gammaPushConstants = g_switchRenderer.gammaPushConstants;
    auto gammaCorrectionShader = g_gammaPushConstants ?
        g_device->createShader(g_gamma_correction_push_ps_spirv, sizeof(g_gamma_correction_push_ps_spirv), "shaderMain", RenderShaderFormat::SPIRV) :
        CREATE_SHADER(gamma_correction_ps);
#else
#if defined(__SWITCH__)
    if (g_switchRenderer.gammaPushConstants)
        fprintf(stderr, "Switch renderer: SwitchGammaPushConstants is unavailable (gamma_correction_push_ps not generated).\n");
#endif
    auto gammaCorrectionShader = CREATE_SHADER(gamma_correction_ps);
#endif

    desc = {};
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = g_copyShader.get();
    desc.pixelShader = gammaCorrectionShader.get();
    desc.renderTargetFormat[0] = BACKBUFFER_FORMAT;
    desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
    desc.renderTargetCount = 1;
    g_gammaCorrectionPipeline = g_device->createGraphicsPipeline(desc);

    // NOTE: We initially allocate this on host memory to make the installer work, even if the 4 GB memory allocation fails.
    g_backBufferHolder = std::make_unique<GuestSurface>(ResourceType::RenderTarget);

    g_backBuffer = g_backBufferHolder.get();
    g_backBuffer->width = 1280;
    g_backBuffer->height = 720;
    g_backBuffer->format = BACKBUFFER_FORMAT;
    g_backBuffer->textureHolder = CreateTextureChecked(RenderTextureDesc::Texture2D(1, 1, 1, BACKBUFFER_FORMAT, RenderTextureFlag::RENDER_TARGET), "backbuffer");

    Video::ComputeViewportDimensions();
    CheckSwapChain();
    BeginCommandList();

    RenderTextureBarrier blankTextureBarriers[TEXTURE_DESCRIPTOR_NULL_COUNT];
    for (size_t i = 0; i < TEXTURE_DESCRIPTOR_NULL_COUNT; i++)
        blankTextureBarriers[i] = RenderTextureBarrier(g_blankTextures[i].get(), RenderTextureLayout::SHADER_READ);

    {
#if defined(__SWITCH__)
        SurveyBarrierScope surveyBarrierScope;
#endif
        g_commandLists[g_frame]->barriers(RenderBarrierStage::NONE, blankTextureBarriers, std::size(blankTextureBarriers));
    }

    return true;
}

static uint32_t g_waitForGPUCount = 0;

void Video::WaitForGPU()
{
    g_waitForGPUCount++;

    // Wait for all queued frames to finish.
    for (size_t i = 0; i < NUM_FRAMES; i++)
    {
        if (g_commandListStates[i])
        {
            g_queue->waitForCommandFence(g_commandFences[i].get());
            g_commandListStates[i] = false;
        }
    }

    // Execute an empty command list and wait for it to end to guarantee that any remaining presentation has finished.
    g_commandLists[0]->begin();
    g_commandLists[0]->end();
    g_queue->executeCommandLists(g_commandLists[0].get(), g_commandFences[0].get());
    g_queue->waitForCommandFence(g_commandFences[0].get());
}

static uint32_t getSetAddress(uint32_t base, int index) {
    uint32_t entryOffset = index * 0xC;
    uint32_t entryAddress = base + entryOffset;
    uint32_t setAddress = entryAddress + sizeof(uint32_t);
    return setAddress;
}

static uint32_t CreateDevice(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, be<uint32_t>* a6)
{
    LOGF_WARNING("{:p} {:p} {:p} {:p} {:p} {:p}\n", reinterpret_cast<void*>(a1), reinterpret_cast<void*>(a2), reinterpret_cast<void*>(a3), reinterpret_cast<void*>(a4), reinterpret_cast<void*>(a5), reinterpret_cast<void*>(a6));
    
    g_xdbfTextureCache = std::unordered_map<uint16_t, GuestTexture*>();

    for (auto &achievement : g_xdbfWrapper.GetAchievements(XDBF_LANGUAGE_ENGLISH))
    {
        if (!achievement.pImageBuffer || !achievement.ImageBufferSize)
            continue;

        g_xdbfTextureCache[achievement.ID] =
            LoadTexture((uint8_t *)achievement.pImageBuffer, achievement.ImageBufferSize).release();
    }

    // Move backbuffer to guest memory.
    assert(!g_memory.IsInMemoryRange(g_backBuffer) && g_backBufferHolder != nullptr);
    g_backBuffer = g_userHeap.AllocPhysical<GuestSurface>(std::move(*g_backBufferHolder));

    // Check for stale reference. BeginCommandList() gets called before CreateDevice() which is where the assignment happens.
    if (g_renderTarget == g_backBufferHolder.get()) g_renderTarget = g_backBuffer;
    if (g_depthStencil == g_backBufferHolder.get()) g_depthStencil = g_backBuffer;

    // Free the host backbuffer.
    g_backBufferHolder = nullptr;

    auto device = g_userHeap.AllocPhysical<GuestDevice>();
    memset(device, 0, sizeof(*device));

    // Append render state functions to the end of guest function table.
    uint32_t functionOffsetUnimplemented = PPC_CODE_BASE + PPC_CODE_SIZE;
    g_memory.InsertFunction(functionOffsetUnimplemented, HostToGuestFunction<SetRenderStateUnimplemented>);
    
    uint32_t functionOffset = 0x82B79868;
    for (size_t i = 0; i < std::size(device->setRenderStateFunctions); i++) {
        device->setRenderStateFunctions[i] = functionOffsetUnimplemented;
    }

    // InsertFucntion doesn't work, so we have to do this manually in the end
    for (auto& [state, function] : g_setRenderStateFunctions)
    {
        auto funcOffset = getSetAddress(functionOffset, state/4);
        uint32_t addr = __builtin_bswap32(*(uint32_t*)g_memory.Translate(funcOffset));
        printf("state %d of %x is %x\n", state, funcOffset, addr);
        g_memory.InsertFunction(addr, function);
        device->setRenderStateFunctions[state / 4] = addr;
    }

    for (size_t i = 0; i < std::size(device->setSamplerStateFunctions); i++)
        device->setSamplerStateFunctions[i] = *reinterpret_cast<uint32_t*>(g_memory.Translate(0x82B79CFC + i * 0xC));

    device->viewport.width = 1280.0f;
    device->viewport.height = 720.0f;
    device->viewport.maxZ = 1.0f;

    *a6 = g_memory.MapVirtual(device);

    return 0;
}

static void DestructResource(GuestResource* resource) 
{
    // Needed for hack in CreateSurface (remove if fix it)
    if (resource->type == ResourceType::RenderTarget || resource->type == ResourceType::DepthStencil)
    {
        const auto surface = reinterpret_cast<GuestSurface*>(resource);
        if (surface->wasCached) {
            return;
        }
    }
    RenderCommand cmd;
    cmd.type = RenderCommandType::DestructResource;
    cmd.destructResource.resource = resource;
    EnqueueRenderCommand(cmd);
}

#if defined(__SWITCH__)
static bool ForgetPendingResolves(GuestResource* resource);
static void CascadeForget(GuestResource* resource);
#endif

static void ProcDestructResource(const RenderCommand& cmd)
{
    const auto& args = cmd.destructResource;
#if defined(__SWITCH__)
    // SwitchCascadeAdoption: the shadow surface or the cascaded shadow map goes.
    CascadeForget(args.resource);

    // A surface still read through pending copies waits for the next Present (ForgetPendingResolves).
    if (ForgetPendingResolves(args.resource))
        return;
#endif
    g_tempResources[g_frame].push_back(args.resource);
}

static uint32_t ComputeTexturePitch(GuestTexture* texture)
{
    return (texture->width * RenderFormatSize(texture->format) + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
}

#if defined(__SWITCH__)
// SwitchPresentOnRenderThread: before the D3D thread writes a texture's or buffer's memory again, the render thread
// has finished presenting the previous frame (submitted it, waited for the frame slot it reuses and acquired the
// next image), which is where it stood when Present returned before. It then processes this frame's commands as
// they come, as without the mode: an earlier unlock of the same texture is copied as early as before, and a buffer
// write (straight into the GPU buffer) lands after the previous frame's submission, as before.
static void WaitForPresentTail()
{
    const uint32_t sent = g_presentTailsSent.load(std::memory_order_acquire);
    uint32_t done = g_presentTailsDone.load(std::memory_order_acquire);
    if (done == sent || !IsPresentThread())
        return;

    while (done != sent)
    {
        g_presentTailsDone.wait(done, std::memory_order_acquire);
        done = g_presentTailsDone.load(std::memory_order_acquire);
    }
}
#endif

static void LockTextureRect(GuestTexture* texture, uint32_t, GuestLockedRect* lockedRect) 
{
#if defined(__SWITCH__)
    WaitForPresentTail();
#endif
    uint32_t pitch = ComputeTexturePitch(texture);
    uint32_t slicePitch = pitch * texture->height;

    if (texture->mappedMemory == nullptr)
        texture->mappedMemory = g_userHeap.AllocPhysical(slicePitch, 0x10);

    lockedRect->pitch = pitch;
    lockedRect->bits = g_memory.MapVirtual(texture->mappedMemory);
}

static void UnlockTextureRect(GuestTexture* texture) 
{
    assert(std::this_thread::get_id() == g_presentThreadId);

    RenderCommand cmd;
    cmd.type = RenderCommandType::UnlockTextureRect;
    cmd.unlockTextureRect.texture = texture;
    // Flushed: the render thread copies the texture's memory, which the game may write again after this call.
    EnqueueRenderCommand(cmd, true);
}

#if defined(__SWITCH__)
static void MakeCopyThePortMade(GuestTexture* texture, uint32_t trigger);
#endif

static void ProcUnlockTextureRect(const RenderCommand& cmd)
{
    const auto& args = cmd.unlockTextureRect;

#if defined(__SWITCH__)
    g_rendererStats.textureUpdates++;

    // SwitchCascadeAdoption: the slices held for the texture first; R no longer holds a copy of any of its slices.
    CascadeTextureUpdated(args.texture);

    // A copy the port had made before this update (owed, or kept over a Present) is made first.
    if (args.texture->copyOwed || args.texture->pendingCarried)
        MakeCopyThePortMade(args.texture, RESOLVE_COPY_OTHER);

    // A depth texture is a depth target without TRANSFER_DST with SwitchZcull, unless it is NO_ZCULL (the depth array
    // textures, which SwitchDepthArrayTexturesD32 also gives another texel size): the game is not known to update any.
    if (RenderFormatIsDepth(args.texture->format))
    {
        static bool s_logged = false;
        if (!s_logged)
        {
            s_logged = true;
            fprintf(stderr, "Switch renderer: the game updated a depth texture from the CPU (%ux%u, format %u); "
                "SwitchZcull and SwitchDepthArrayTexturesD32 assume it does not.\n", args.texture->width, args.texture->height,
                uint32_t(args.texture->format));
        }
    }
#endif

    AddBarrier(args.texture, RenderTextureLayout::COPY_DEST);
    FlushBarriers();

    uint32_t pitch = ComputeTexturePitch(args.texture);
    uint32_t slicePitch = pitch * args.texture->height;

    auto allocation = g_uploadAllocators[g_frame].allocate(slicePitch, PLACEMENT_ALIGNMENT);
    memcpy(allocation.memory, args.texture->mappedMemory, slicePitch);

    g_commandLists[g_frame]->copyTextureRegion(
        RenderTextureCopyLocation::Subresource(args.texture->texture, 0),
        RenderTextureCopyLocation::PlacedFootprint(allocation.buffer, args.texture->format, args.texture->width, args.texture->height, 1, pitch / RenderFormatSize(args.texture->format), allocation.offset));
}

static void* LockBuffer(GuestBuffer* buffer, uint32_t flags)
{
    buffer->lockedReadOnly = (flags & 0x10) != 0;

    if (buffer->mappedMemory == nullptr)
        buffer->mappedMemory = g_userHeap.AllocPhysical(buffer->dataSize, 0x10);

    return buffer->mappedMemory;
}

static void* LockVertexBuffer(GuestBuffer* buffer, uint32_t, uint32_t, uint32_t flags)
{
    return LockBuffer(buffer, flags);
}

static std::atomic<uint32_t> g_bufferUploadCount = 0;


template<typename T>
static void UnlockBuffer(GuestBuffer* buffer, bool useCopyQueue)
{
    auto copyBuffer = [&](T* dest)
        {
            auto src = reinterpret_cast<const T*>(buffer->mappedMemory);

#if defined(__SWITCH__) && defined(__aarch64__)
            // As many elements as the loop below copies: one per started sizeof(T) bytes.
            CopyByteSwapped(dest, src, (size_t(buffer->dataSize) + sizeof(T) - 1) / sizeof(T));
#else
            for (size_t i = 0; i < buffer->dataSize; i += sizeof(T))
            {
                *dest = ByteSwap(*src);
                ++dest;
                ++src;
            }
#endif
        };

    if (useCopyQueue && g_capabilities.gpuUploadHeap)
    {
        copyBuffer(reinterpret_cast<T*>(buffer->buffer->map()));
        buffer->buffer->unmap();
    }
    else
    {
        auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(buffer->dataSize));
        copyBuffer(reinterpret_cast<T*>(uploadBuffer->map()));
        uploadBuffer->unmap();

        if (useCopyQueue)
        {
            ExecuteCopyCommandList([&]
                {
                    g_copyCommandList->copyBufferRegion(buffer->buffer->at(0), uploadBuffer->at(0), buffer->dataSize);
                });
        }
        else
        {
            auto& commandList = g_commandLists[g_frame];

            commandList->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(buffer->buffer.get(), RenderBufferAccess::WRITE));
            commandList->copyBufferRegion(buffer->buffer->at(0), uploadBuffer->at(0), buffer->dataSize);
            commandList->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(buffer->buffer.get(), RenderBufferAccess::READ));

            g_tempBuffers[g_frame].emplace_back(std::move(uploadBuffer));
        }
    }

    g_bufferUploadCount++;
}

template<typename T>
static void UnlockBuffer(GuestBuffer* buffer)
{
    if (!buffer->lockedReadOnly)
    {
#if defined(__SWITCH__)
        // SwitchDeferredBufferUnlocks: on the D3D thread (the one thread that allocates in the command memory), the
        // bytes the copy below would read (one element per started sizeof(T) bytes) are taken now, and the render
        // thread writes them into the GPU buffer when it gets to the command: after the previous frame's present tail,
        // the point WaitForPresentTail waits for below, and before any later command of this thread. Later writes of
        // the game to the buffer's memory do not reach the GPU buffer before their own Unlock, as before.
        const uint32_t snapshotSize = uint32_t((size_t(buffer->dataSize) + sizeof(T) - 1) / sizeof(T) * sizeof(T));
        if (g_switchRenderer.deferredBufferUnlocks && g_capabilities.gpuUploadHeap && IsPresentThread() &&
            snapshotSize != 0 && snapshotSize <= IntermediaryUploadAllocator::SIZE)
        {
            RenderCommand cmd;
            cmd.type = RenderCommandType::UnlockBufferSnapshot;
            cmd.unlockBufferSnapshot.buffer = buffer;
            cmd.unlockBufferSnapshot.snapshot = CurrentIntermediaryUploadAllocator().allocate(buffer->mappedMemory, snapshotSize);
            cmd.unlockBufferSnapshot.elementSize = sizeof(T);
            EnqueueRenderCommand(cmd);
            return;
        }

        // The GPU buffer is written here, on the calling thread (see WaitForPresentTail).
        WaitForPresentTail();
#endif
        UnlockBuffer<T>(buffer, true);
    }
}

#if defined(__SWITCH__)
// SwitchDeferredBufferUnlocks: the copy UnlockBuffer<T>(buffer, true) makes on the gpuUploadHeap path, from the snapshot.
static void ProcUnlockBufferSnapshot(const RenderCommand& cmd)
{
    const auto& args = cmd.unlockBufferSnapshot;
    GuestBuffer* buffer = args.buffer;
    void* destination = buffer->buffer->map();
    const size_t count = (size_t(buffer->dataSize) + args.elementSize - 1) / args.elementSize;
    if (args.elementSize == sizeof(uint32_t))
        CopyByteSwapped(reinterpret_cast<uint32_t*>(destination), reinterpret_cast<const uint32_t*>(args.snapshot), count);
    else
        CopyByteSwapped(reinterpret_cast<uint16_t*>(destination), reinterpret_cast<const uint16_t*>(args.snapshot), count);
    buffer->buffer->unmap();
    g_bufferUploadCount++;
}
#endif

static void ProcUnlockBuffer16(const RenderCommand& cmd)
{
    UnlockBuffer<uint16_t>(cmd.unlockBuffer.buffer, false);
}

static void ProcUnlockBuffer32(const RenderCommand& cmd)
{
    UnlockBuffer<uint32_t>(cmd.unlockBuffer.buffer, false);
}

static void UnlockVertexBuffer(GuestBuffer* buffer)
{
    UnlockBuffer<uint32_t>(buffer);
}

static void GetVertexBufferDesc(GuestBuffer* buffer, GuestBufferDesc* desc) 
{
    desc->size = buffer->dataSize;
}

static void* LockIndexBuffer(GuestBuffer* buffer, uint32_t, uint32_t, uint32_t flags) 
{
    return LockBuffer(buffer, flags);
}

static void UnlockIndexBuffer(GuestBuffer* buffer) 
{
    if (buffer->guestFormat == D3DFMT_INDEX32)
        UnlockBuffer<uint32_t>(buffer);
    else
        UnlockBuffer<uint16_t>(buffer);
}

static void GetIndexBufferDesc(GuestBuffer* buffer, GuestBufferDesc* desc)
{
    desc->format = buffer->guestFormat;
    desc->size = buffer->dataSize;
}

static void GetSurfaceDesc(GuestSurface* surface, GuestSurfaceDesc* desc) 
{
    if (surface->width == 0 && surface->height == 0) {
        LOGF_WARNING("{:p} {:d} {:d} \n", reinterpret_cast<void*>(desc), surface->width, surface->height);
        __builtin_trap();
    }
    desc->width = surface->width;
    desc->height = surface->height;
    desc->format = surface->guestFormat;
    desc->type = 4; // D3DRTYPE_SURFACE
    // desc->multiSampleType = 0;
    if (surface->sampleCount == RenderSampleCount::COUNT_1) {
        desc->multiSampleType = 0;
    } else if (surface->sampleCount == RenderSampleCount::COUNT_2) {
        desc->multiSampleType = 1;
    } else {
        desc->multiSampleType = 2;
    }
    desc->multiSampleQuality = 0;
    desc->usage = 0;
}

static void GetVertexDeclaration(GuestVertexDeclaration* vertexDeclaration, GuestVertexElement* vertexElements, be<uint32_t>* count) 
{
    memcpy(vertexElements, vertexDeclaration->vertexElements.get(), vertexDeclaration->vertexElementCount * sizeof(GuestVertexElement));
    *count = vertexDeclaration->vertexElementCount;
}

static uint32_t HashVertexDeclaration(uint32_t vertexDeclaration) 
{
    // Vertex declarations are cached on host side, so the pointer itself can be used.
    return vertexDeclaration;
}

static const char *DeviceTypeName(RenderDeviceType type)
{
    switch (type) 
    {
    case RenderDeviceType::INTEGRATED:
        return "Integrated";
    case RenderDeviceType::DISCRETE:
        return "Discrete";
    case RenderDeviceType::VIRTUAL:
        return "Virtual";
    case RenderDeviceType::CPU:
        return "CPU";
    default:
        return "Unknown";
    }
}

static void DrawProfiler()
{
    bool toggleProfiler = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F1] != 0;

#if defined(__SWITCH__)
    // [Switch] The console has no F1 key (unless a USB keyboard is plugged in, which still toggles it): [Switch]
    // SwitchShowProfiler shows the window from the start. An overlay the tester asks for; off by default.
    static bool s_profilerVisibilityConfigured = false;
    if (!s_profilerVisibilityConfigured)
    {
        s_profilerVisibilityConfigured = true;
        g_profilerVisible = g_switchRenderer.showProfiler;
    }
#endif

    if (!g_profilerWasToggled && toggleProfiler)
    {
        g_profilerVisible = !g_profilerVisible;

        GameWindow::SetFullscreenCursorVisibility(App::s_isInit ? g_profilerVisible : true);
    }

    g_profilerWasToggled = toggleProfiler;

    if (!g_profilerVisible)
        return;

    ImFont* font = ImFontAtlasSnapshot::GetFont("FOT-RodinPro-DB.otf");
    float defaultScale = font->Scale;
    font->Scale = ImGui::GetDefaultFont()->FontSize / font->FontSize;
    ImGui::PushFont(font);

#define IMGUI_GENERIC_ROW(name, value, ...) \
    ImGui::TableNextColumn(); \
    ImGui::Text(name); \
    ImGui::TableNextColumn(); \
    ImGui::Text(value, __VA_ARGS__);

    if (ImGui::Begin("Profiler", &g_profilerVisible))
    {
        g_applicationValues[g_profilerValueIndex] = App::s_deltaTime * 1000.0;

        const double applicationAvg = std::accumulate(g_applicationValues, g_applicationValues + PROFILER_VALUE_COUNT, 0.0) / PROFILER_VALUE_COUNT;
        double gpuFrameAvg = g_gpuFrameProfiler.UpdateAndReturnAverage();
        double presentAvg = g_presentProfiler.UpdateAndReturnAverage();
        double frameFenceAvg = g_frameFenceProfiler.UpdateAndReturnAverage();
        double presentWaitAvg = g_presentWaitProfiler.UpdateAndReturnAverage();
        double swapChainAcquireAvg = g_swapChainAcquireProfiler.UpdateAndReturnAverage();

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImPlot::BeginPlot("Frame Time"))
            {
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 20.0);
                ImPlot::SetupAxis(ImAxis_Y1, "ms", ImPlotAxisFlags_None);
                ImPlot::PlotLine<double>("Application", g_applicationValues, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
                ImPlot::PlotLine<double>("GPU Frame", g_gpuFrameProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
                ImPlot::PlotLine<double>("Present", g_presentProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
                ImPlot::PlotLine<double>("Present Wait", g_presentWaitProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
                ImPlot::PlotLine<double>("Frame Fence", g_frameFenceProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
                ImPlot::PlotLine<double>("Swap Chain Acquire", g_swapChainAcquireProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
                ImPlot::EndPlot();
            }

            g_profilerValueIndex = (g_profilerValueIndex + 1) % PROFILER_VALUE_COUNT;

            if (ImGui::BeginTable("Performance", 5))
            {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Current Time");
                ImGui::TableSetupColumn("Average Time");
                ImGui::TableSetupColumn("Current FPS");
                ImGui::TableSetupColumn("Average FPS");
                ImGui::TableHeadersRow();

                auto drawPerfRow = [](const char* name, double ms, double msAvg, bool showFPS = false, double fps = 0, double fpsAvg = 0)
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", name);
                    ImGui::TableNextColumn();
                    ImGui::Text("%g ms", ms);
                    ImGui::TableNextColumn();
                    ImGui::Text("%g ms", msAvg);
                    ImGui::TableNextColumn();

                    if (showFPS)
                        ImGui::Text("%g FPS", fps);

                    ImGui::TableNextColumn();

                    if (showFPS)
                        ImGui::Text("%g FPS", fpsAvg);
                };

                // -------- Name ---------------- Current Time --------------------------- Average Time -------- Current FPS ----------------------------- Average FPS ---------- //
                drawPerfRow("Application",        App::s_deltaTime * 1000.0,               applicationAvg, true, 1.0 / App::s_deltaTime,                   1000.0 / applicationAvg);
                drawPerfRow("GPU Frame",          g_gpuFrameProfiler.value.load(),         gpuFrameAvg,    true, 1000.0 / g_gpuFrameProfiler.value.load(), 1000.0 / gpuFrameAvg   );
                drawPerfRow("Present",            g_presentProfiler.value.load(),          presentAvg,     true, 1000.0 / g_presentProfiler.value.load(),  1000.0 / presentAvg    );
                drawPerfRow("Present Wait",       g_presentWaitProfiler.value.load(),      presentWaitAvg                                                                         );
                drawPerfRow("Frame Fence",        g_frameFenceProfiler.value.load(),       frameFenceAvg                                                                          );
                drawPerfRow("Swap Chain Acquire", g_swapChainAcquireProfiler.value.load(), swapChainAcquireAvg                                                                    );

                ImGui::EndTable();
            }

            ImGui::Separator();

            ImGui::Checkbox("Show FPS", &Config::ShowFPS.Value);
        }

        if (g_userHeap.heap != nullptr && g_userHeap.physicalHeap != nullptr)
        {
            if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
            {
                O1HeapDiagnostics diagnostics, physicalDiagnostics;
                {
                    std::lock_guard lock(g_userHeap.mutex);
                    diagnostics = o1heapGetDiagnostics(g_userHeap.heap);
                }
                {
                    std::lock_guard lock(g_userHeap.physicalMutex);
                    physicalDiagnostics = o1heapGetDiagnostics(g_userHeap.physicalHeap);
                }

                if (ImGui::BeginTable("Memory", 2))
                {
                    IMGUI_GENERIC_ROW("Heap Allocated", "%d MB", int32_t(diagnostics.allocated / (1024 * 1024)));
                    IMGUI_GENERIC_ROW("Physical Heap Allocated", "%d MB", int32_t(diagnostics.allocated / (1024 * 1024)));

                    ImGui::EndTable();
                }
            }
        }

        if (ImGui::CollapsingHeader("GPU", ImGuiTreeNodeFlags_DefaultOpen))
        {
            std::string backend;

            switch (g_backend)
            {
                case Backend::VULKAN:
                    backend = "Vulkan";
                    break;

                case Backend::D3D12:
                    backend = "D3D12";
                    break;

                case Backend::METAL:
                    backend = "Metal";
                    break;
            }

            if (ImGui::BeginTable("GPU", 2))
            {
                IMGUI_GENERIC_ROW("API", "%s", backend.c_str());

                if (auto pSDLVideoDriver = SDL_GetCurrentVideoDriver())
                {
                    IMGUI_GENERIC_ROW("SDL Video Driver", "%s", pSDLVideoDriver);
                }

                IMGUI_GENERIC_ROW("Device", "%s", g_device->getDescription().name.c_str());
                IMGUI_GENERIC_ROW("Device Type", "%s", DeviceTypeName(g_device->getDescription().type));
                IMGUI_GENERIC_ROW("VRAM", "%.2f MiB", (double)(g_device->getDescription().dedicatedVideoMemory) / (1024.0 * 1024.0));
                IMGUI_GENERIC_ROW("GPU Waits", "%d", int32_t(g_waitForGPUCount));
                IMGUI_GENERIC_ROW("Buffer Uploads", "%d", int32_t(g_bufferUploadCount));

                IMGUI_GENERIC_ROW("Resolution", "%dx%d (%dx%d)",
                    Video::s_viewportWidth, Video::s_viewportHeight,
                    uint32_t(round(Video::s_viewportWidth * Config::ResolutionScale)),
                    uint32_t(round(Video::s_viewportHeight * Config::ResolutionScale)));

                ImGui::EndTable();
            }

            ImGui::Separator();

            if (ImGui::TreeNode("Devices"))
            {
                ImGui::Indent();

                if (ImGui::BeginTable("Devices", 2))
                {
                    auto deviceIndex = 0;

                    for (const auto& deviceName : g_interface->getDeviceNames())
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("Device #%d", deviceIndex++);
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", deviceName.c_str());
                        ImGui::SameLine();
                    }

                    ImGui::EndTable();
                }

                ImGui::Unindent();
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Features"))
            {
                ImGui::Indent();

                if (ImGui::BeginTable("Features", 2))
                {
                    IMGUI_GENERIC_ROW("Dynamic Depth Bias", "%s", g_capabilities.dynamicDepthBias ? "Supported" : "Unsupported");
                    IMGUI_GENERIC_ROW("GPU Upload Heap", "%s", g_capabilities.gpuUploadHeap ? "Supported" : "Unsupported");
                    IMGUI_GENERIC_ROW("Hardware Resolve Modes", "%s", g_capabilities.resolveModes ? "Supported" : "Unsupported");
                    IMGUI_GENERIC_ROW("Present Wait", "%s", g_capabilities.presentWait ? "Supported" : "Unsupported");
                    IMGUI_GENERIC_ROW("Triangle Fan", "%s", g_capabilities.triangleFan ? "Supported" : "Unsupported");
                    IMGUI_GENERIC_ROW("Triangle Strip Workaround", "%s", g_triangleStripWorkaround ? "Enabled" : "Disabled");
                    IMGUI_GENERIC_ROW("UMA", "%s", g_capabilities.uma ? "Supported" : "Unsupported");

                    ImGui::EndTable();
                }

                ImGui::Unindent();
                ImGui::TreePop();
            }
        }
    }

#undef IMGUI_GENERIC_ROW

    ImGui::End();
    ImGui::PopFont();

    font->Scale = defaultScale;
}

static void DrawFPS()
{
    if (!Config::ShowFPS)
        return;

    double time = ImGui::GetTime();
    static double updateTime = time;
    static double fps = 0;
    static double totalDeltaTime = 0.0;
    static uint32_t totalDeltaCount = 0;

    totalDeltaTime += g_presentProfiler.value.load();
    totalDeltaCount++;

    if (time - updateTime >= 1.0f)
    {
        fps = 1000.0 / std::max(totalDeltaTime / double(totalDeltaCount), 1.0);
        updateTime = time;
        totalDeltaTime = 0.0;
        totalDeltaCount = 0;
    }

    auto drawList = ImGui::GetBackgroundDrawList();

    auto fmt = fmt::format("FPS: {:.2f}", fps);
    auto font = ImFontAtlasSnapshot::GetFont("FOT-RodinPro-DB.otf");
    auto fontSize = Scale(10);
    auto textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0, fmt.c_str());

    ImVec2 min = { Scale(40), Scale(30) };
    ImVec2 max = { min.x + std::max(Scale(75), textSize.x + Scale(10)), min.y + Scale(15) };
    ImVec2 textPos = { min.x + Scale(2), CENTRE_TEXT_VERT(min, max, textSize) + Scale(0.2f) };

    drawList->AddRectFilled(min, max, IM_COL32(0, 0, 0, 200));
    drawList->AddText(font, fontSize, textPos, IM_COL32_WHITE, fmt.c_str());
}

static void DrawImGui()
{
    ImGui_ImplSDL2_NewFrame();

    auto& io = ImGui::GetIO();
    io.DisplaySize = { float(Video::s_viewportWidth), float(Video::s_viewportHeight) };

    // ImGui doesn't know that we center the screen for specific aspect ratio
    // settings, which causes mouse events to not work correctly. To fix this, 
    // we can adjust the mouse events before ImGui processes them.
    uint32_t width = g_swapChain->getWidth();
    uint32_t height = g_swapChain->getHeight();
    float mousePosScaleX = float(width) / float(GameWindow::s_width);
    float mousePosScaleY = float(height) / float(GameWindow::s_height);
    float mousePosOffsetX = (width - Video::s_viewportWidth) / 2.0f;
    float mousePosOffsetY = (height - Video::s_viewportHeight) / 2.0f;
    for (int i = 0; i < io.Ctx->InputEventsQueue.Size; i++)
    {
        auto& e = io.Ctx->InputEventsQueue[i];
        if (e.Type == ImGuiInputEventType_MousePos)
        {
            if (e.MousePos.PosX != -FLT_MAX)
            {
                e.MousePos.PosX *= mousePosScaleX;
                e.MousePos.PosX -= mousePosOffsetX;
            }

            if (e.MousePos.PosY != -FLT_MAX)
            {
                e.MousePos.PosY *= mousePosScaleY;
                e.MousePos.PosY -= mousePosOffsetY;
            }
        }
    }

    ImGui::NewFrame();

    ResetImGuiCallbacks();

#ifdef ASYNC_PSO_DEBUG
    if (ImGui::Begin("Async PSO Stats"))
    {
        ImGui::Text("Pipelines Created In Render Thread: %d", g_pipelinesCreatedInRenderThread.load());
        ImGui::Text("Pipelines Created Asynchronously: %d", g_pipelinesCreatedAsynchronously.load());
        ImGui::Text("Pipelines Dropped: %d", g_pipelinesDropped.load());
        ImGui::Text("Pipelines Currently Compiling: %d", g_pipelinesCurrentlyCompiling.load());
        ImGui::Text("Compiling Pipeline Task Count: %d", g_compilingPipelineTaskCount.load());
        ImGui::Text("Pending Pipeline Task Count: %d", g_pendingPipelineTaskCount.load());

        std::lock_guard lock(g_debugMutex);
        ImGui::TextUnformatted(g_pipelineDebugText.c_str());
    }
    ImGui::End();
#endif

    UpdateImGuiUtils();
    AchievementMenu::Draw();
    OptionsMenu::Draw();
    InstallerWizard::Draw();
    ButtonWindow::Draw();
    MessageWindow::Draw();
    AchievementOverlay::Draw();
    Fader::Draw();
    BlackBar::Draw();

    assert(ImGui::GetBackgroundDrawList()->_ClipRectStack.Size == 1 && "Some clip rects were not removed from the stack!");

    DrawFPS();
    DrawProfiler();
    ImGui::Render();

    auto drawData = ImGui::GetDrawData();
    if (drawData->CmdListsCount != 0)
    {
        RenderCommand cmd;
        cmd.type = RenderCommandType::DrawImGui;
        // Flushed: the render thread reads ImGui's draw data.
        EnqueueRenderCommand(cmd, true);
    }
}

static void SetFramebuffer(GuestSurface *renderTarget, GuestSurface *depthStencil, bool settingForClear);

static void ProcDrawImGui(const RenderCommand& cmd)
{
    // Make sure the backbuffer is the current target.
    AddBarrier(g_backBuffer, RenderTextureLayout::COLOR_WRITE);
    FlushBarriers();
    SetFramebuffer(g_backBuffer, nullptr, false);

    auto& commandList = g_commandLists[g_frame];
    auto pipeline = g_imPipeline.get();

#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    // ImGui's pipeline layout: its push constants replace the pointers, and set 5 is bound again by the next draw.
    g_constantsUboBinding.boundSet = nullptr;
    InvalidatePushedRootAddresses();
#endif

    commandList->setGraphicsPipelineLayout(g_imPipelineLayout.get());
    commandList->setPipeline(pipeline);
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
    commandList->setGraphicsDescriptorSet(g_samplerDescriptorSet.get(), 1);

    auto& drawData = *ImGui::GetDrawData();
    commandList->setViewports(RenderViewport(drawData.DisplayPos.x, drawData.DisplayPos.y, drawData.DisplaySize.x, drawData.DisplaySize.y));
#if defined(__SWITCH__)
    // Viewport and scissor are ImGui's from here on (FlushViewport's are no longer known: GetDrawViewport).
    g_appliedViewportValid = false;
    g_appliedScissorValid = false;
#endif

    ImGuiPushConstants pushConstants{};
    pushConstants.displaySize = drawData.DisplaySize;
    pushConstants.inverseDisplaySize = { 1.0f / drawData.DisplaySize.x, 1.0f / drawData.DisplaySize.y };
    commandList->setGraphicsPushConstants(0, &pushConstants);

    size_t pushConstantRangeMin = ~0;
    size_t pushConstantRangeMax = 0;

    auto setPushConstants = [&](void* destination, const void* source, size_t size)
        {
            bool dirty = memcmp(destination, source, size) != 0;

            memcpy(destination, source, size);

            if (dirty)
            {
                size_t offset = reinterpret_cast<size_t>(destination) - reinterpret_cast<size_t>(&pushConstants);
                pushConstantRangeMin = std::min(pushConstantRangeMin, offset);
                pushConstantRangeMax = std::max(pushConstantRangeMax, offset + size);
            }
        };

    ImRect clipRect{};

    for (int i = 0; i < drawData.CmdListsCount; i++)
    {
        auto& drawList = drawData.CmdLists[i];

        auto vertexBufferAllocation = g_uploadAllocators[g_frame].allocate<false>(drawList->VtxBuffer.Data, drawList->VtxBuffer.Size * sizeof(ImDrawVert), alignof(ImDrawVert));
        auto indexBufferAllocation = g_uploadAllocators[g_frame].allocate<false>(drawList->IdxBuffer.Data, drawList->IdxBuffer.Size * sizeof(uint16_t), alignof(uint16_t));

        const RenderVertexBufferView vertexBufferView(vertexBufferAllocation.buffer->at(vertexBufferAllocation.offset), drawList->VtxBuffer.Size * sizeof(ImDrawVert));
        const RenderInputSlot inputSlot(0, sizeof(ImDrawVert));
        commandList->setVertexBuffers(0, &vertexBufferView, 1, &inputSlot);

        const RenderIndexBufferView indexBufferView(indexBufferAllocation.buffer->at(indexBufferAllocation.offset), drawList->IdxBuffer.Size * sizeof(uint16_t), RenderFormat::R16_UINT);
        commandList->setIndexBuffer(&indexBufferView);

        for (int j = 0; j < drawList->CmdBuffer.Size; j++)
        {
            auto& drawCmd = drawList->CmdBuffer[j];
            if (drawCmd.UserCallback != nullptr)
            {
                auto callbackData = reinterpret_cast<const ImGuiCallbackData*>(drawCmd.UserCallbackData);

                switch (static_cast<ImGuiCallback>(reinterpret_cast<size_t>(drawCmd.UserCallback)))
                {
                case ImGuiCallback::SetGradient:
                    setPushConstants(&pushConstants.boundsMin, &callbackData->setGradient, sizeof(callbackData->setGradient));
                    break;       
                case ImGuiCallback::SetShaderModifier:
                    setPushConstants(&pushConstants.shaderModifier, &callbackData->setShaderModifier, sizeof(callbackData->setShaderModifier));
                    break;
                case ImGuiCallback::SetOrigin:
                    setPushConstants(&pushConstants.origin, &callbackData->setOrigin, sizeof(callbackData->setOrigin));
                    break;
                case ImGuiCallback::SetScale:
                    setPushConstants(&pushConstants.scale, &callbackData->setScale, sizeof(callbackData->setScale));
                    break;       
                case ImGuiCallback::SetMarqueeFade:
                    setPushConstants(&pushConstants.boundsMin, &callbackData->setMarqueeFade, sizeof(callbackData->setMarqueeFade));
                    break;
                case ImGuiCallback::SetOutline:
                    setPushConstants(&pushConstants.outline, &callbackData->setOutline, sizeof(callbackData->setOutline));
                    break;
                case ImGuiCallback::SetProceduralOrigin:
                    setPushConstants(&pushConstants.proceduralOrigin, &callbackData->setProceduralOrigin, sizeof(callbackData->setProceduralOrigin));
                    break;
                case ImGuiCallback::SetAdditive:
                {
                    auto pipelineToSet = callbackData->setAdditive.enabled ? g_imAdditivePipeline.get() : g_imPipeline.get();
                    if (pipeline != pipelineToSet)
                    {
                        commandList->setPipeline(pipelineToSet);
                        pipeline = pipelineToSet;
                    }
                    break;
                }
                default:
                    assert(false && "Unknown ImGui callback type.");
                    break;
                }
            }
            else
            {
                if (drawCmd.ClipRect.z <= drawCmd.ClipRect.x || drawCmd.ClipRect.w <= drawCmd.ClipRect.y)
                    continue;

                auto texture = reinterpret_cast<GuestTexture*>(drawCmd.TextureId);
                uint32_t descriptorIndex = TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D;
                if (texture != nullptr)
                {
                    if (texture->layout != RenderTextureLayout::SHADER_READ)
                    {
#if defined(__SWITCH__)
                        SurveyBarrierScope surveyBarrierScope;
#endif
                        commandList->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COPY,
                            RenderTextureBarrier(texture->texture, RenderTextureLayout::SHADER_READ));

                        texture->layout = RenderTextureLayout::SHADER_READ;
                    }

                    descriptorIndex = texture->descriptorIndex;

                    if (texture == g_imFontTexture.get())
                        descriptorIndex |= 0x80000000;

                    setPushConstants(&pushConstants.texture2DDescriptorIndex, &descriptorIndex, sizeof(descriptorIndex));
                }

                if (pushConstantRangeMin < pushConstantRangeMax)
                {
                    commandList->setGraphicsPushConstants(0, reinterpret_cast<const uint8_t*>(&pushConstants) + pushConstantRangeMin, pushConstantRangeMin, pushConstantRangeMax - pushConstantRangeMin);
                    pushConstantRangeMin = ~0;
                    pushConstantRangeMax = 0;
                }

                if (memcmp(&clipRect, &drawCmd.ClipRect, sizeof(clipRect)) != 0)
                {
                    commandList->setScissors(RenderRect(int32_t(drawCmd.ClipRect.x), int32_t(drawCmd.ClipRect.y), int32_t(drawCmd.ClipRect.z), int32_t(drawCmd.ClipRect.w)));
                    clipRect = drawCmd.ClipRect;
                }

                commandList->drawIndexedInstanced(drawCmd.ElemCount, 1, drawCmd.IdxOffset, drawCmd.VtxOffset, 0);
            }
        }
    }
}

// We have to check for this to properly handle the following situation:
// 1. Wait on swap chain.
// 2. Create loading thread.
// 3. Loading thread also waits on swap chain.
// 4. Loading thread presents and quits.
// 5. After the loading thread quits, application also presents.
static bool g_pendingWaitOnSwapChain = true;

#if defined(__SWITCH__)
// SwitchPresentOnRenderThread: the mode is only used without present wait (NVK has none; see ShouldPipelinePresent),
// where the wait below does nothing.
static bool PresentOnRenderThreadActive()
{
    return g_switchRenderer.presentOnRenderThread && !g_capabilities.presentWait && g_gamePresenting.load(std::memory_order_acquire);
}
#endif

void Video::WaitOnSwapChain()
{
#if defined(__SWITCH__)
    // The swap chain is the render thread's in this mode.
    if (PresentOnRenderThreadActive())
        return;
#endif

    if (g_pendingWaitOnSwapChain)
    {
        if (g_swapChainValid)
        {
            g_presentWaitProfiler.Begin();
            g_swapChain->wait();
            g_presentWaitProfiler.End();
        }

        g_pendingWaitOnSwapChain = false;
    }
}

static bool g_shouldPrecompilePipelines;
static std::atomic<bool> g_executedCommandList;

#if defined(__SWITCH__)
// Presents from threads that did not own the device while batching was on (diagnostics, see the ownership hooks).
static std::atomic<uint32_t> g_directPresents{ 0 };

// SwitchPresentOnRenderThread: waits until the render thread has finished presenting every frame sent before, on
// any thread (WaitForPresentTail waits on the D3D thread only).
static void WaitForAllPresentTails()
{
    uint32_t done = g_presentTailsDone.load(std::memory_order_acquire);
    while (done != g_presentTailsSent.load(std::memory_order_acquire))
    {
        g_presentTailsDone.wait(done, std::memory_order_acquire);
        done = g_presentTailsDone.load(std::memory_order_acquire);
    }
}

// SwitchPresentOnRenderThread: whether the render thread presents this frame and gets the next one ready. Only the
// game's frames (the installer's stay synchronous), from the thread that owns the device, and only when the swap
// chain is valid and does not need to be recreated. It is recreated only between frames on the presenting thread,
// as before: a frame that needs it is synchronous, so CheckSwapChain runs where it ran. Called after ImGui's frame
// (where the options change), where CheckSwapChain made the same vsync call and check. Nothing else uses the swap
// chain meanwhile: every earlier present has finished (Present waited for them) and the render thread only touches
// it at this frame's ExecuteCommandList, sent after this.
static bool ShouldPipelinePresent()
{
    if (!PresentOnRenderThreadActive() || !IsPresentThread() || !g_swapChainValid)
        return false;

    g_swapChain->setVsyncEnabled(Config::VSync);
    return !g_swapChain->needsResize();
}
#endif

#if defined(__SWITCH__)
// [Switch] SwitchPipelineCache: the persistent pipeline cache (cache/pipelines.bin, configured before the device is
// created: SwitchPerfInitRenderer) is written by a thread of its own at priority 0x3B, not lower:
// vkGetPipelineCacheData holds the driver's cache lock, which pipeline creation on the render thread also takes.
// Requested when a loading screen ends, and with SwitchPipelineCacheSaveDuringPlay at most once a minute; there is no
// save at exit (closing from HOME kills the process). The save returns at once when nothing was added since the
// last one. Only the file changes: pipelines are compiled the same, the cache only makes creating them faster.
static std::mutex g_pipelineCacheSaveMutex;
static std::condition_variable g_pipelineCacheSaveCondition;
static bool g_pipelineCacheSaveRequested = false;
static std::thread* g_pipelineCacheSaveThread = nullptr; // Never destroyed on purpose.

static void RequestPipelineCacheSave()
{
    if (!g_switchRenderer.pipelineCache || g_device == nullptr)
        return;

    if (g_pipelineCacheSaveThread == nullptr)
    {
        g_pipelineCacheSaveThread = new std::thread([]
            {
                SwitchSetCurrentThreadPriority(0x3B);

                while (true)
                {
                    {
                        std::unique_lock lock(g_pipelineCacheSaveMutex);
                        g_pipelineCacheSaveCondition.wait(lock, [] { return g_pipelineCacheSaveRequested; });
                        g_pipelineCacheSaveRequested = false;
                    }

                    if (g_device->savePipelineCache())
                        fprintf(stderr, "Pipeline cache saved.\n");
                }
            });
    }

    {
        std::lock_guard lock(g_pipelineCacheSaveMutex);
        g_pipelineCacheSaveRequested = true;
    }

    g_pipelineCacheSaveCondition.notify_one();
}

// The presenting thread, once per Present (the D3D thread hand-overs order the presents of the main and the loading
// thread). A loading screen has ended once its HUDLoading::Update (LoadingPatches::s_activeUpdates, counted while it
// is not finished) has not run for 30 presents.
static void UpdatePipelineCacheSaving()
{
    using namespace std::chrono_literals;

    static uint32_t s_loadingUpdates = 0;
    static uint32_t s_presentsSinceLoadingUpdate = 0;
    static bool s_loading = false;
    static auto s_lastRequest = std::chrono::steady_clock::now();

    if (!g_switchRenderer.pipelineCache)
        return;

    const uint32_t loadingUpdates = LoadingPatches::s_activeUpdates.load(std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();

    if (loadingUpdates != s_loadingUpdates)
    {
        s_loadingUpdates = loadingUpdates;
        s_presentsSinceLoadingUpdate = 0;
        s_loading = true;
    }
    else if (s_loading && ++s_presentsSinceLoadingUpdate >= 30)
    {
        s_loading = false;
        RequestPipelineCacheSave();
        s_lastRequest = now;
    }

    if (g_switchRenderer.pipelineCacheSaveDuringPlay && (now - s_lastRequest) >= 60s)
    {
        RequestPipelineCacheSave();
        s_lastRequest = now;
    }
}
#endif

void Video::Present() 
{
#if defined(__SWITCH__)
    // SwitchSlowFrameProfileMs: the game thread's work for this frame ends here (it only reads the clock).
    os::switch_cpu_profiler::FrameWorkEnd();

    // SwitchGpuPassProfiler: where this thread spends the frame (FrameTimeLaps; it only reads the clock).
    FrameTimeLaps frameTimes(g_passProfilerEnabled);

    // SwitchPresentOnRenderThread: everything below (ImGui's frame, the swap chain checks, the resets) sees the
    // previous frame's present finished, as when Present returned after it. Normally it already is.
    if (g_switchRenderer.presentOnRenderThread)
        WaitForAllPresentTails();

    // SwitchPresentWithoutRecordWait: every frame sent before is recorded (its present tail comes after the
    // recording), so the flag the last one set is cleared for this frame's wait, if it waits.
    if (g_switchRenderer.presentWithoutRecordWait)
        g_recordedCommandList.store(false, std::memory_order_relaxed);

    frameTimes.Lap(FRAME_TIME_RENDER_THREAD);

    if (g_switchRenderer.batchRenderCommands && !IsPresentThread())
        g_directPresents.fetch_add(1, std::memory_order_relaxed);
#endif

    g_readyForCommands = false;

    // The three Present commands are flushed: this thread waits for the end of the frame below.
    RenderCommand cmd;
    cmd.type = RenderCommandType::ExecutePendingStretchRectCommands;
    EnqueueRenderCommand(cmd, true);

    DrawImGui();

#if defined(__SWITCH__)
    frameTimes.Lap(FRAME_TIME_IMGUI);

    // Decided per frame and sent with the command, so both threads agree.
    const bool pipelined = ShouldPipelinePresent();
#endif

    cmd.type = RenderCommandType::ExecuteCommandList;
#if defined(__SWITCH__)
    cmd.executeCommandList.pipelined = pipelined;

    // SwitchPresentWithoutRecordWait: not for a frame after which this thread changes what the render thread may still
    // read while recording it (the viewport and the back buffer's size, below): that frame waits as before.
    const bool withoutRecordWait = pipelined && g_switchRenderer.presentWithoutRecordWait && !g_needsResize &&
        g_backBuffer->width == Video::s_viewportWidth && g_backBuffer->height == Video::s_viewportHeight;
    cmd.executeCommandList.captured = withoutRecordWait;
    cmd.executeCommandList.brightness = Config::Brightness;
    cmd.executeCommandList.viewportWidth = Video::s_viewportWidth;
    cmd.executeCommandList.viewportHeight = Video::s_viewportHeight;

    if (pipelined)
        g_presentTailsSent.fetch_add(1, std::memory_order_acq_rel);
#endif
    EnqueueRenderCommand(cmd, true);

    // All the shaders are available at this point. We can precompile embedded PSOs then.
    if (g_shouldPrecompilePipelines)
    {
//        EnqueuePipelineTask(PipelineTaskType::PrecompilePipelines, {});
        g_shouldPrecompilePipelines = false;
    }

#if defined(__SWITCH__)
    if (pipelined)
    {
        if (withoutRecordWait)
        {
            // SwitchPresentWithoutRecordWait: the render thread records this frame while this thread goes on. Its copies
            // stay in the current command memory; the next frame's go to the other one, whose frame was recorded before
            // the wait at the start of this Present returned.
            g_intermediaryUploadAllocatorIndex ^= 1;
            CurrentIntermediaryUploadAllocator().reset();
        }
        else
        {
            // The render thread has read everything this frame sent (this thread's copies of shader constants and
            // vertices included); it submits, presents and gets the next frame ready (PresentOnRenderThread).
            g_recordedCommandList.wait(false, std::memory_order_acquire);
            g_recordedCommandList.store(false, std::memory_order_relaxed);
        }
        frameTimes.Lap(FRAME_TIME_RENDER_THREAD);

        os::switch_overlay::OnPresent(Video::s_viewportWidth, Video::s_viewportHeight);

        g_pendingWaitOnSwapChain = true;
        if (!withoutRecordWait)
            CurrentIntermediaryUploadAllocator().reset();

        // What CheckSwapChain does for the game besides the swap chain stays here, on this thread and at this point
        // of the frame. The swap chain is not recreated (ShouldPipelinePresent), so these get the same values.
        if (g_needsResize)
        {
            Video::ComputeViewportDimensions();

            // SwitchPresentWithoutRecordWait: nothing in Marathon clears the flag once a swap chain creation or a
            // video option set it (Unleashed clears it in its render director hook, where its game remakes its
            // targets), so it stayed set from the first frames on and every frame took the record wait above (perf6:
            // 1.37 ms a frame at the CPU-limited spot). Its only readers on Switch are the two recomputations of the
            // viewport (here and CheckSwapChain), which give the same values again until the swap chain or one of
            // those options changes, and each of those sets it again; GameWindow::Update saw it during the frame.
            // The recomputation above is done after the record wait, as before.
            if (g_switchRenderer.presentWithoutRecordWait)
                g_needsResize = false;
        }

        g_backBuffer->width = Video::s_viewportWidth;
        g_backBuffer->height = Video::s_viewportHeight;
    }
    else
    {
#endif
    g_executedCommandList.wait(false);
    g_executedCommandList = false;
#if defined(__SWITCH__)
    frameTimes.Lap(FRAME_TIME_RENDER_THREAD);
#endif

    if (g_swapChainValid)
    {
        if (g_pendingWaitOnSwapChain)
        {
            g_presentWaitProfiler.Begin();
            g_swapChain->wait(); // Never gonna happen outside loading threads as explained above.
            g_presentWaitProfiler.End();
        }

        RenderCommandSemaphore* signalSemaphores[] = { g_renderSemaphores[g_frame].get() };
        g_swapChainValid = g_swapChain->present(g_backBufferIndex, signalSemaphores, std::size(signalSemaphores));
    }

#if defined(__SWITCH__)
    os::switch_overlay::OnPresent(Video::s_viewportWidth, Video::s_viewportHeight);
    frameTimes.Lap(FRAME_TIME_PRESENT);
#endif

    g_pendingWaitOnSwapChain = true;

    g_frame = g_nextFrame;
    g_nextFrame = (g_frame + 1) % NUM_FRAMES;

    if (g_commandListStates[g_frame])
    {
        g_frameFenceProfiler.Begin();
        g_queue->waitForCommandFence(g_commandFences[g_frame].get());
        g_frameFenceProfiler.End();
        g_commandListStates[g_frame] = false;

        // Update the GPU profiler with the results from the timestamps of the frame.
        g_queryPools[g_frame]->queryResults();
        const uint64_t *frameTimestamps = g_queryPools[g_frame]->getResults();
#if defined(__SWITCH__)
        // [Switch] NVK's GPU timer ticks every ~1.627 ns, not 1 (GpuTimestampMs). A displayed number only.
        g_gpuFrameProfiler.Set(GpuTimestampMs(int64_t(frameTimestamps[1] - frameTimestamps[0])));
#else
        g_gpuFrameProfiler.Set(double(frameTimestamps[1] - frameTimestamps[0]) / 1000000.0);
#endif
    }

    g_dirtyStates = DirtyStates(true);
    g_uploadAllocators[g_frame].reset();
    CurrentIntermediaryUploadAllocator().reset();
    g_triangleFanIndexData.reset();
    g_quadIndexData.reset();

#if defined(__SWITCH__)
    frameTimes.Lap(FRAME_TIME_GPU);
#endif
    CheckSwapChain();
#if defined(__SWITCH__)
    frameTimes.Lap(FRAME_TIME_ACQUIRE);
    }
#endif

    cmd.type = RenderCommandType::BeginCommandList;
    EnqueueRenderCommand(cmd, true);

    if (Config::FPS >= FPS_MIN && Config::FPS < FPS_MAX)
    {
        using namespace std::chrono_literals;

        static std::chrono::steady_clock::time_point s_next;

        auto now = std::chrono::steady_clock::now();

        if (now < s_next)
        {
#if defined(__SWITCH__)
            if (g_switchRenderer.frameLimiterSleep)
            {
                // [Switch] SwitchFrameLimiterSleep. svcSleepThread wakes within microseconds of the deadline;
                // the 2 ms yield spin below kept the game's thread busy on its core for nothing. Same deadline,
                // and still never left before it (the check below only covers the sleep's rounding).
                std::this_thread::sleep_until(s_next);

                while (std::chrono::steady_clock::now() < s_next)
                    std::this_thread::yield();
            }
            else
#endif
            {
                std::this_thread::sleep_for(std::chrono::floor<std::chrono::milliseconds>(s_next - now - 2ms));

                while ((now = std::chrono::steady_clock::now()) < s_next)
                    std::this_thread::yield();
            }
        }
        else
        {
            s_next = now;
        }

        s_next += 1000000000ns / Config::FPS;
    }

#if defined(__SWITCH__)
    frameTimes.Lap(FRAME_TIME_LIMITER);

    UpdatePipelineCacheSaving();
    os::switch_stall_watch::OnFrame();

    frameTimes.FinishPresent(pipelined);

    // SwitchSlowFrameProfileMs: the next frame's work starts here.
    os::switch_cpu_profiler::FrameWorkStart();
#endif

    g_presentProfiler.Reset();
}

void Video::StartPipelinePrecompilation()
{
    g_shouldPrecompilePipelines = true;
}

static void SetRootDescriptor(const UploadAllocation& allocation, size_t index)
{
    auto& commandList = g_commandLists[g_frame];

    if (g_backend != Backend::D3D12)
        commandList->setGraphicsPushConstants(0, &allocation.deviceAddress, 8 * index, 8);
    else
        commandList->setGraphicsRootDescriptor(allocation.buffer->at(allocation.offset), index);
}

#if defined(__SWITCH__)
// A draw's constant block: with SwitchConstantsUBO the pointer is only recorded, PushRootAddressesIfNeeded pushes it
// if one of the draw's shaders reads it.
static void SetRootDescriptorForDraw(const UploadAllocation& allocation, size_t index)
{
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    if (g_constantsUbo)
    {
        g_pendingRootAddresses[index] = allocation.deviceAddress;
        return;
    }
#endif

    SetRootDescriptor(allocation, index);
}
#endif

#if defined(__SWITCH__)
// SwitchPresentOnRenderThread: what Present did after the submission, on the render thread right after it (the D3D
// thread has gone on to its next frame): present the image, move to the next frame slot and wait for the GPU to
// finish the frame that used it last, reset that slot's allocators and the render state, acquire the next image.
// The next frame's commands (BeginCommandList first) wait in the queue meanwhile. The swap chain is the render
// thread's in this mode. What CheckSwapChain does for the game (the viewport and backbuffer sizes) stays in Present.
static void PresentOnRenderThread()
{
    // SwitchGpuPassProfiler: the frame's present, GPU wait and acquire, which Present leaves to this thread.
    FrameTimeLaps frameTimes(g_passProfilerEnabled);

    if (g_swapChainValid)
    {
        RenderCommandSemaphore* signalSemaphores[] = { g_renderSemaphores[g_frame].get() };
        g_swapChainValid = g_swapChain->present(g_backBufferIndex, signalSemaphores, std::size(signalSemaphores));
    }

    frameTimes.Lap(FRAME_TIME_PRESENT);

    g_frame = g_nextFrame;
    g_nextFrame = (g_frame + 1) % NUM_FRAMES;

    if (g_commandListStates[g_frame])
    {
        g_frameFenceProfiler.Begin();
        g_queue->waitForCommandFence(g_commandFences[g_frame].get());
        g_frameFenceProfiler.End();
        g_commandListStates[g_frame] = false;

        // Update the GPU profiler with the results from the timestamps of the frame (GpuTimestampMs: see Present).
        g_queryPools[g_frame]->queryResults();
        const uint64_t *frameTimestamps = g_queryPools[g_frame]->getResults();
        g_gpuFrameProfiler.Set(GpuTimestampMs(int64_t(frameTimestamps[1] - frameTimestamps[0])));
    }

    g_dirtyStates = DirtyStates(true);
    g_uploadAllocators[g_frame].reset();
    g_triangleFanIndexData.reset();
    g_quadIndexData.reset();
    frameTimes.Lap(FRAME_TIME_GPU);

    // CheckSwapChain's swap chain part. Present made sure, with this frame's vsync setting, that the swap chain did
    // not need to be recreated. If the window changed since (docking), it is still recreated only between frames on
    // the presenting thread: the next frame finds the swap chain invalid, is drawn without an image (as after a
    // failed acquire) and its Present is synchronous and recreates it.
    g_swapChainValid = !g_swapChain->needsResize();
    if (g_swapChainValid)
    {
        g_swapChainAcquireProfiler.Begin();
        g_swapChainValid = g_swapChain->acquireTexture(g_acquireSemaphores[g_frame].get(), &g_backBufferIndex);
        g_swapChainAcquireProfiler.End();
    }
    else
    {
        fprintf(stderr, "Switch renderer: the swap chain needs to be recreated; the next Present does it.\n");
    }

    frameTimes.Lap(FRAME_TIME_ACQUIRE);
    frameTimes.FinishRenderThreadPresent();

    g_presentTailsDone.fetch_add(1, std::memory_order_acq_rel);
    g_presentTailsDone.notify_all();
}
#endif

#if defined(__SWITCH__)
// The difference between two readings of a block of counters that only grow (ResolveStats, RendererStats): each report
// keeps its own previous reading, so several reports can read the same counters over windows of their own.
template<typename T>
static T CountersSince(const T& now, const T& previous)
{
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % sizeof(uint64_t) == 0);
    constexpr size_t COUNT = sizeof(T) / sizeof(uint64_t);

    uint64_t values[COUNT];
    uint64_t previousValues[COUNT];
    memcpy(values, &now, sizeof(values));
    memcpy(previousValues, &previous, sizeof(previousValues));
    for (size_t i = 0; i < COUNT; i++)
        values[i] -= previousValues[i];

    T difference;
    memcpy(&difference, values, sizeof(difference));
    return difference;
}

// The resolve statistics as three lines, per frame; `prefix` starts the first one.
static void AppendResolveStats(std::string& report, const char* prefix, const ResolveStats& stats, double frames)
{
    auto perFrame = [frames](uint64_t value) { return double(value) / frames; };

    uint64_t copies = 0;
    for (uint64_t count : stats.copies)
        copies += count;

    AppendFormat(report, "%s%.1f draws (%.1f surveys, %.1f no-op skipped, %.1f restores skipped), "
        "%.1f copies (%.1f before draws, %.1f at clears, %.1f at present, %.1f other; %.1f into arrays), %.2f Mpixels\n",
        prefix, perFrame(stats.draws), perFrame(stats.surveyDraws), perFrame(stats.noOpDraws),
        perFrame(stats.restoresSkipped), perFrame(copies), perFrame(stats.copies[RESOLVE_COPY_BEFORE_DRAW]),
        perFrame(stats.copies[RESOLVE_COPY_AT_CLEAR]), perFrame(stats.copies[RESOLVE_COPY_AT_PRESENT]),
        perFrame(stats.copies[RESOLVE_COPY_OTHER]), perFrame(stats.arrayCopies), perFrame(stats.copyPixels) / 1000000.0);
    AppendFormat(report, "  hand-overs: %.1f at clears, %.1f at draws (%.1f without marks); kept over present %.1f, depth dropped %.1f, "
        "owed %.1f; at draws not: %.1f blend, %.1f depth, %.1f textures, %.1f images, %.1f variant, %.1f shader; "
        "full coverage not proven: %.1f not a quad, %.1f shaders, %.1f shape, %.1f edges, %.1f viewport\n",
        perFrame(stats.handOversAtClear), perFrame(stats.coverageHandOvers), perFrame(stats.exactHandOvers), perFrame(stats.kept),
        perFrame(stats.depthDropped), perFrame(stats.owed), perFrame(stats.coverageMisses[COVERAGE_MISS_BLEND]),
        perFrame(stats.coverageMisses[COVERAGE_MISS_DEPTH]), perFrame(stats.coverageMisses[COVERAGE_MISS_TEXTURES]),
        perFrame(stats.coverageMisses[COVERAGE_MISS_IMAGE]), perFrame(stats.coverageMisses[COVERAGE_MISS_VARIANT]),
        perFrame(stats.coverageMisses[COVERAGE_MISS_SHADER]), perFrame(stats.exactCoverageMisses[EXACT_COVERAGE_MISS_NOT_QUAD]),
        perFrame(stats.exactCoverageMisses[EXACT_COVERAGE_MISS_SHADERS]), perFrame(stats.exactCoverageMisses[EXACT_COVERAGE_MISS_SHAPE]),
        perFrame(stats.exactCoverageMisses[EXACT_COVERAGE_MISS_EDGES]), perFrame(stats.exactCoverageMisses[EXACT_COVERAGE_MISS_VIEWPORT]));
    AppendFormat(report, "  clears: %.1f waited, %.1f skipped, %.1f with the uniform stencil; barriers: %.1f batches (%.1f conservative, "
        "%.1f inside a pass), %.1f eager sample transitions; %.1f framebuffer changes, %.1f draws kept attachments, "
        "%.1f read-only depth draws\n",
        perFrame(stats.clearsDeferred), perFrame(stats.clearsSkipped), perFrame(stats.stencilClearsWidened),
        perFrame(stats.barrierBatches), perFrame(stats.conservativeBarrierBatches), perFrame(stats.midPassBarrierBatches),
        perFrame(stats.eagerTransitions), perFrame(stats.framebufferChanges), perFrame(stats.keptAttachmentDraws),
        perFrame(stats.readOnlyDepthDraws));
    AppendFormat(report, "  not kept over present: %.1f back buffer, %.1f msaa, %.1f depth, %.1f released, %.1f texture, "
        "%.1f swizzle, %.1f format, %.1f size; read-only depth not: %.1f depth write, %.1f stencil write, %.1f msaa, "
        "%.1f pending, %.1f texture, %.1f format, %.1f size, %.1f view\n",
        perFrame(stats.keepRefused[KEEP_REFUSED_BACK_BUFFER]), perFrame(stats.keepRefused[KEEP_REFUSED_MSAA]),
        perFrame(stats.keepRefused[KEEP_REFUSED_DEPTH]), perFrame(stats.keepRefused[KEEP_REFUSED_RELEASED]),
        perFrame(stats.keepRefused[KEEP_REFUSED_TEXTURE]), perFrame(stats.keepRefused[KEEP_REFUSED_SWIZZLE]),
        perFrame(stats.keepRefused[KEEP_REFUSED_FORMAT]), perFrame(stats.keepRefused[KEEP_REFUSED_SIZE]),
        perFrame(stats.readOnlyRefused[READ_ONLY_REFUSED_DEPTH_WRITE]), perFrame(stats.readOnlyRefused[READ_ONLY_REFUSED_STENCIL_WRITE]),
        perFrame(stats.readOnlyRefused[READ_ONLY_REFUSED_MSAA]), perFrame(stats.readOnlyRefused[READ_ONLY_REFUSED_PENDING]),
        perFrame(stats.readOnlyRefused[READ_ONLY_REFUSED_TEXTURE]), perFrame(stats.readOnlyRefused[READ_ONLY_REFUSED_FORMAT]),
        perFrame(stats.readOnlyRefused[READ_ONLY_REFUSED_SIZE]), perFrame(stats.readOnlyRefused[READ_ONLY_REFUSED_VIEW]));
    if (g_switchRenderer.cascadeAdoption)
    {
        AppendFormat(report, "  cascades: %.1f resolves held, %.2f images adopted (%.1f fill-in copies, %.1f not needed), %.1f held "
            "layers copied, %.2f shadow surface restores, %.2f continued into the next layer\n",
            perFrame(stats.cascadeHeld), perFrame(stats.cascadeAdoptions), perFrame(stats.cascadeFillIns),
            perFrame(stats.cascadeFillsSkipped), perFrame(stats.cascadeHeldCopies), perFrame(stats.cascadeMaterialized),
            perFrame(stats.cascadeContinued));
    }
}

// SwitchResolveStats: once a frame on the render thread, a report every 300 frames (one write to stderr.log, by the
// report writer thread).
static void ReportResolveStats()
{
    static ResolveStats s_previous{};

    if (++g_resolveStatsFrames < PASS_PROFILER_REPORT_FRAMES)
        return;

    if (g_switchRenderer.resolveStats)
    {
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "[resolves] per frame over %u frames: ", g_resolveStatsFrames);

        std::string report;
        report.reserve(2048);
        AppendResolveStats(report, prefix, CountersSince(g_resolveStats, s_previous), double(g_resolveStatsFrames));
        WriteReportAsync(std::move(report));
    }

    s_previous = g_resolveStats;
    g_resolveStatsFrames = 0;
}

// SwitchGpuDrawProfiler and SwitchFrameLog: a shader's identity, which outlives the shader. Translated shaders: 1 + the
// index of their shader cache entry; the port's own shaders (no cache entry): their address with the top bit set
// (compared, never read); 0: none.
static uint64_t DrawProfilerShaderId(const GuestShader* shader)
{
    if (shader == nullptr)
        return 0;

    if (shader->shaderCacheEntry != nullptr)
        return uint64_t(shader->shaderCacheEntry - g_shaderCacheEntries) + 1;

    return uint64_t(reinterpret_cast<uintptr_t>(shader)) | (1ull << 63);
}

static uint32_t DrawProfilerShaderSpirv(uint64_t id)
{
    if (g_shaderSpirvBlake3 == nullptr || id == 0 || (id >> 63) != 0 || id > g_shaderCacheEntryCount)
        return 0;

    return g_shaderSpirvBlake3[id - 1].load(std::memory_order_relaxed);
}

// "HASH/spirv/File" (the draw profiler; tools/switch-gpu-profile.py finds the driver's statistics by the spirv part) or
// "HASH/File" (the frame log) for a translated shader, "port:name" for the port's own. No spaces: the tool splits on them.
static std::string DrawProfilerShaderName(uint64_t id, bool withSpirv)
{
    if (id == 0)
        return "none";

    if ((id >> 63) != 0)
    {
        const uintptr_t address = uintptr_t(id & ~(1ull << 63));
        auto is = [address](const GuestShader* shader) { return shader != nullptr && reinterpret_cast<uintptr_t>(shader) == address; };

        if (is(g_conditionalSurveyPSShader.get()))
            return "port:conditional_survey_ps";
        if (is(g_csdFilterShader.get()))
            return "port:csd_filter_ps";
        if (is(g_enhancedBurnoutBlurVSShader.get()))
            return "port:enhanced_burnout_blur_vs";
        if (is(g_enhancedBurnoutBlurPSShader.get()))
            return "port:enhanced_burnout_blur_ps";

        static constexpr const char* BLURS[] = { "port:gaussian_blur_3x3", "port:gaussian_blur_5x5", "port:gaussian_blur_7x7", "port:gaussian_blur_9x9" };
        for (size_t i = 0; i < std::size(g_gaussianBlurShaders) && i < std::size(BLURS); i++)
        {
            if (is(g_gaussianBlurShaders[i].get()))
                return BLURS[i];
        }

        // A game shader the port replaced with its own (CSD, blend colour alpha).
        return fmt::format("port:{:04X}", uint32_t(address & 0xFFFF));
    }

    if (id > g_shaderCacheEntryCount)
        return "?";

    const ShaderCacheEntry& entry = g_shaderCacheEntries[id - 1];

    // "shader/xenon/shader/np/Metal01.fxo" -> "Metal01".
    std::string_view file(entry.filename, strnlen(entry.filename, sizeof(entry.filename)));
    if (const size_t slash = file.find_last_of('/'); slash != std::string_view::npos)
        file.remove_prefix(slash + 1);
    if (const size_t dot = file.find_last_of('.'); dot != std::string_view::npos)
        file = file.substr(0, dot);

    std::string name = withSpirv ? fmt::format("{:016X}/{:08x}/{}", entry.hash, DrawProfilerShaderSpirv(id), file) :
        fmt::format("{:016X}/{}", entry.hash, file);
    std::replace(name.begin(), name.end(), ' ', '_');
    return name;
}

// The draw groups of the most expensive passes, after the pass table (which is sorted by then).
static void DrawProfilerPrint(double frames, std::string& report)
{
    static constexpr const char* Z_FUNC_NAMES[] = { "noZ", "never", "<", "==", "<=", ">", "!=", ">=", "always" };
    static constexpr size_t MAX_PASSES = 16;
    static constexpr size_t MAX_GROUPS = 16;

    std::vector<std::pair<const DrawProfilerKey*, const DrawProfilerTotal*>> groups;
    groups.reserve(g_drawProfilerTotals.size());
    for (const auto& [key, total] : g_drawProfilerTotals)
        groups.emplace_back(&key, &total);

    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.second->gpuMs > b.second->gpuMs; });

    size_t passesShown = 0;
    for (const auto& passTotal : g_passProfilerTotals)
    {
        const auto& p = passTotal.pass;
        const double passMs = passTotal.gpuMs / frames;
        if (passesShown == MAX_PASSES || passMs < 0.1)
            break;
        if (p.width == 0 || p.kind != PASS_PROFILER_PASS)
            continue;

        passesShown++;
        AppendFormat(report, "[gpu draws] %ux%u %s x%u depth %s #%u: %.2f ms, %.0f draws per frame; groups by shaders and state:\n",
            p.width, p.height, PassProfilerFormatName(p.colorFormat), p.samples, PassProfilerFormatName(p.depthFormat),
            passTotal.ordinal, passMs, double(passTotal.draws) / frames);

        size_t shownGroups = 0;
        size_t otherGroups = 0;
        double otherMs = 0.0;
        for (const auto& [key, total] : groups)
        {
            if (key->width != p.width || key->height != p.height || key->colorFormat != p.colorFormat ||
                key->depthFormat != p.depthFormat || key->samples != p.samples || key->ordinal != passTotal.ordinal)
            {
                continue;
            }

            const double ms = total->gpuMs / frames;
            if (shownGroups == MAX_GROUPS)
            {
                otherGroups++;
                otherMs += ms;
                continue;
            }

            shownGroups++;

            const uint32_t zFunc = std::min<uint32_t>(key->state >> DRAW_PROFILER_Z_FUNC_SHIFT, std::size(Z_FUNC_NAMES) - 1);
            const std::string pixelShader = (key->state & DRAW_PROFILER_NO_PIXEL_SHADER) != 0 ?
                std::string("none") : DrawProfilerShaderName(key->pixelShader, true);
            const std::string vertexShader = DrawProfilerShaderName(key->vertexShader, true);

            AppendFormat(report, "  %6.3f ms %6.1f draws %8.0f verts  ps %-25s vs %-25s z%s%s%s%s%s%s%s%s%s%s%s\n",
                ms, double(total->draws) / frames, double(total->count) / frames, pixelShader.c_str(), vertexShader.c_str(),
                Z_FUNC_NAMES[zFunc],
                (key->state & DRAW_PROFILER_Z_WRITE) != 0 ? " zwrite" : "",
                (key->state & DRAW_PROFILER_STENCIL) != 0 ? " stencil" : "",
                (key->state & DRAW_PROFILER_BLEND) != 0 ? " blend" : "",
                (key->state & DRAW_PROFILER_ALPHA_TEST) != 0 ? " alphatest" : "",
                (key->state & DRAW_PROFILER_ALPHA_TO_COVERAGE) != 0 ? " a2c" : "",
                (key->state & DRAW_PROFILER_REVERSE_Z) != 0 ? " reverseZ" : "",
                (key->state & DRAW_PROFILER_CLIP_PLANE) != 0 ? " clip" : "",
                (key->state & DRAW_PROFILER_CONDITIONAL) != 0 ? " conditional" : "",
                (key->state & DRAW_PROFILER_SURVEY) != 0 ? " survey" : "",
                (key->state & DRAW_PROFILER_COVERAGE) != 0 ? " coverage" : "");
        }

        if (otherGroups != 0)
            AppendFormat(report, "  %6.3f ms in %zu more groups\n", otherMs, otherGroups);
    }
}

// The entry of `totals` for a pass description and its order among passes with that description (added when missing
// and `add` is set).
static PassProfilerTotal* FindPassTotal(std::vector<PassProfilerTotal>& totals, const PassProfilerPass& pass, uint32_t ordinal,
    bool add)
{
    for (auto& candidate : totals)
    {
        const auto& p = candidate.pass;
        if (candidate.ordinal == ordinal && p.width == pass.width && p.height == pass.height &&
            p.colorFormat == pass.colorFormat && p.depthFormat == pass.depthFormat && p.samples == pass.samples &&
            p.kind == pass.kind)
        {
            return &candidate;
        }
    }

    if (!add)
        return nullptr;

    totals.push_back(PassProfilerTotal{ pass, ordinal, 0.0, 0 });
    return &totals.back();
}

// A pass as the pass table names it, without its time and draws (no spaces inside a field).
static std::string PassProfilerDescribe(const PassProfilerPass& p, uint32_t ordinal)
{
    if (p.kind >= PASS_PROFILER_COPIES && p.kind < PASS_PROFILER_GAMMA)
    {
        return fmt::format("{}x{} {} x{} copies {} #{}", p.width, p.height,
            PassProfilerFormatName(p.colorFormat != RenderFormat::UNKNOWN ? p.colorFormat : p.depthFormat), p.samples,
            ResolveCopyTriggerName(p.kind - PASS_PROFILER_COPIES), ordinal);
    }

    if (p.kind == PASS_PROFILER_GAMMA)
        return fmt::format("{}x{} {} gamma pass #{}", p.width, p.height, PassProfilerFormatName(p.colorFormat), ordinal);

    if (p.width == 0)
        return "frame start (no target)";

    return fmt::format("{}x{} {} x{} depth {} #{}", p.width, p.height, PassProfilerFormatName(p.colorFormat), p.samples,
        PassProfilerFormatName(p.depthFormat), ordinal);
}

static std::string DrawProfilerStateText(uint32_t state)
{
    static constexpr const char* Z_FUNC_NAMES[] = { "noZ", "never", "<", "==", "<=", ">", "!=", ">=", "always" };
    const uint32_t zFunc = std::min<uint32_t>(state >> DRAW_PROFILER_Z_FUNC_SHIFT, std::size(Z_FUNC_NAMES) - 1);
    return fmt::format("z{}{}{}{}{}{}{}{}{}{}{}", Z_FUNC_NAMES[zFunc],
        (state & DRAW_PROFILER_Z_WRITE) != 0 ? " zwrite" : "",
        (state & DRAW_PROFILER_STENCIL) != 0 ? " stencil" : "",
        (state & DRAW_PROFILER_BLEND) != 0 ? " blend" : "",
        (state & DRAW_PROFILER_ALPHA_TEST) != 0 ? " alphatest" : "",
        (state & DRAW_PROFILER_ALPHA_TO_COVERAGE) != 0 ? " a2c" : "",
        (state & DRAW_PROFILER_REVERSE_Z) != 0 ? " reverseZ" : "",
        (state & DRAW_PROFILER_CLIP_PLANE) != 0 ? " clip" : "",
        (state & DRAW_PROFILER_CONDITIONAL) != 0 ? " conditional" : "",
        (state & DRAW_PROFILER_SURVEY) != 0 ? " survey" : "",
        (state & DRAW_PROFILER_COVERAGE) != 0 ? " coverage" : "");
}

// SwitchGpuSlowFrameMs: a frame at or over the threshold goes into the slow-frame totals, and into the list of the
// report's slowest frames if it is one of them.
static void PassProfilerTakeSlowFrame(const PassProfilerFrame& frame, const std::vector<uint32_t>& ordinals,
    const std::vector<double>& passMs, uint64_t draws, double gpuFrameMs)
{
    g_gpuSlowFrames++;
    g_gpuSlowFrameMsSum += gpuFrameMs;
    g_gpuSlowFrameLongestMs = std::max(g_gpuSlowFrameLongestMs, gpuFrameMs);
    g_gpuSlowCounters.Add(frame.counters);
    g_gpuSlowDraws += draws;

    for (size_t i = 0; i < frame.passes.size(); i++)
    {
        PassProfilerTotal* total = FindPassTotal(g_gpuSlowPassTotals, frame.passes[i], ordinals[i], true);
        total->gpuMs += passMs[i];
        total->draws += frame.passes[i].draws;
    }

    if (g_gpuSlowestFrames.size() == GPU_SLOW_FRAMES_LISTED && gpuFrameMs <= g_gpuSlowestFrames.back().ms)
        return;

    GpuSlowFrame slow;
    slow.ms = gpuFrameMs;
    slow.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_passProfilerStartTime).count();
    slow.frameNumber = g_passProfilerFrameNumber;
    slow.draws = draws;
    slow.counters = frame.counters;

    // Its six most expensive passes of 0.3 ms or more.
    std::vector<size_t> order(frame.passes.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return passMs[a] > passMs[b]; });
    for (size_t i = 0; i < order.size() && i < 6 && passMs[order[i]] >= 0.3; i++)
    {
        const size_t pass = order[i];
        AppendFormat(slow.passes, "%s%.2f %s (%u draws)", i == 0 ? "" : ", ", passMs[pass],
            PassProfilerDescribe(frame.passes[pass], ordinals[pass]).c_str(), frame.passes[pass].draws);
    }

    auto position = std::find_if(g_gpuSlowestFrames.begin(), g_gpuSlowestFrames.end(),
        [&](const GpuSlowFrame& other) { return gpuFrameMs > other.ms; });
    g_gpuSlowestFrames.insert(position, std::move(slow));
    if (g_gpuSlowestFrames.size() > GPU_SLOW_FRAMES_LISTED)
        g_gpuSlowestFrames.pop_back();
}

// The slow-frame part of the report: what the slow frames did more than the average frame, pass by pass (and draw group
// by draw group), and the slowest frames whole. tools/switch-gpu-profile.py reads the "[gpu slow frames]" and
// "[gpu slow draws]" blocks.
static void GpuSlowFramesPrint(double frames, std::string& report)
{
    if (g_gpuSlowFrames == 0)
    {
        AppendFormat(report, "[gpu slow frames] none of %.0f frames with a GPU frame of %.0f ms or more\n", frames, g_gpuSlowFrameMs);
        return;
    }

    const double slowFrames = double(g_gpuSlowFrames);
    AppendFormat(report, "[gpu slow frames] %u of %.0f frames with a GPU frame of %.0f ms or more (longest %.1f ms, average %.1f ms; "
        "all frames %.2f ms)\n", g_gpuSlowFrames, frames, g_gpuSlowFrameMs, g_gpuSlowFrameLongestMs,
        g_gpuSlowFrameMsSum / slowFrames, g_passProfilerFrameMs / frames);

    const auto& s = g_gpuSlowCounters;
    const auto& a = g_gpuAllCounters;
    AppendFormat(report, "  per frame, slow vs all: %.0f vs %.0f draws, %.1f vs %.1f resolve copies (%.2f vs %.2f Mpixels), "
        "%.1f vs %.1f texture updates, %.1f vs %.1f framebuffer changes, %.1f vs %.1f barrier batches (%.1f vs %.1f inside a pass), "
        "%.0f vs %.0f KB of shader constants, %.2f vs %.2f pipelines created (%.2f vs %.2f ms)\n",
        double(g_gpuSlowDraws) / slowFrames, double(g_gpuAllDraws) / frames,
        double(s.resolveCopies) / slowFrames, double(a.resolveCopies) / frames,
        double(s.resolvePixels) / 1e6 / slowFrames, double(a.resolvePixels) / 1e6 / frames,
        double(s.textureUpdates) / slowFrames, double(a.textureUpdates) / frames,
        double(s.framebufferChanges) / slowFrames, double(a.framebufferChanges) / frames,
        double(s.barrierBatches) / slowFrames, double(a.barrierBatches) / frames,
        double(s.midPassBarrierBatches) / slowFrames, double(a.midPassBarrierBatches) / frames,
        double(s.constantBytes) / 1024.0 / slowFrames, double(a.constantBytes) / 1024.0 / frames,
        double(s.pipelines) / slowFrames, double(a.pipelines) / frames,
        double(s.pipelineUs) / 1000.0 / slowFrames, double(a.pipelineUs) / 1000.0 / frames);

    struct Row
    {
        std::string name;
        double slowMs, allMs, slowDraws, allDraws;
    };

    std::vector<Row> rows;
    for (const auto& slowTotal : g_gpuSlowPassTotals)
    {
        const PassProfilerTotal* all = FindPassTotal(g_passProfilerTotals, slowTotal.pass, slowTotal.ordinal, false);
        rows.push_back(Row{ PassProfilerDescribe(slowTotal.pass, slowTotal.ordinal), slowTotal.gpuMs / slowFrames,
            all != nullptr ? all->gpuMs / frames : 0.0, double(slowTotal.draws) / slowFrames,
            all != nullptr ? double(all->draws) / frames : 0.0 });
    }

    std::sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) { return x.slowMs - x.allMs > y.slowMs - y.allMs; });

    AppendFormat(report, "  passes by the time they add in slow frames (ms per slow frame vs per frame over all frames):\n");
    for (size_t i = 0; i < rows.size() && i < 16; i++)
    {
        const Row& row = rows[i];
        if (i >= 4 && row.slowMs - row.allMs < 0.1)
            break;

        AppendFormat(report, "  %+7.2f ms %6.2f vs %6.2f  %-40s draws %5.0f vs %5.0f\n", row.slowMs - row.allMs, row.slowMs,
            row.allMs, row.name.c_str(), row.slowDraws, row.allDraws);
    }

    AppendFormat(report, "  slowest frames:\n");
    for (const auto& frame : g_gpuSlowestFrames)
    {
        AppendFormat(report, "    %.1f ms (frame %u, %.1f s): %llu draws, %llu resolve copies, %llu texture updates, %llu framebuffer "
            "changes, %llu pipelines created (%.1f ms); %s\n", frame.ms, frame.frameNumber, frame.seconds,
            (unsigned long long)frame.draws, (unsigned long long)frame.counters.resolveCopies,
            (unsigned long long)frame.counters.textureUpdates, (unsigned long long)frame.counters.framebufferChanges,
            (unsigned long long)frame.counters.pipelines, double(frame.counters.pipelineUs) / 1000.0, frame.passes.c_str());
    }

    if (!g_drawProfilerEnabled || g_drawProfilerSlowTotals.empty())
        return;

    struct DrawRow
    {
        const DrawProfilerKey* key;
        double slowMs, allMs, slowDraws, allDraws;
    };

    std::vector<DrawRow> drawRows;
    drawRows.reserve(g_drawProfilerSlowTotals.size());
    for (const auto& [key, slowTotal] : g_drawProfilerSlowTotals)
    {
        auto all = g_drawProfilerTotals.find(key);
        const bool found = all != g_drawProfilerTotals.end();
        drawRows.push_back(DrawRow{ &key, slowTotal.gpuMs / slowFrames, found ? all->second.gpuMs / frames : 0.0,
            double(slowTotal.draws) / slowFrames, found ? double(all->second.draws) / frames : 0.0 });
    }

    std::sort(drawRows.begin(), drawRows.end(),
        [](const DrawRow& x, const DrawRow& y) { return x.slowMs - x.allMs > y.slowMs - y.allMs; });

    AppendFormat(report, "[gpu slow draws] draw groups by the time they add in slow frames (ms and draws per slow frame vs per frame "
        "over all frames):\n");
    for (size_t i = 0; i < drawRows.size() && i < 24; i++)
    {
        const DrawRow& row = drawRows[i];
        if (i >= 4 && row.slowMs - row.allMs < 0.05)
            break;

        const DrawProfilerKey& key = *row.key;
        PassProfilerPass pass{};
        pass.width = key.width;
        pass.height = key.height;
        pass.colorFormat = key.colorFormat;
        pass.depthFormat = key.depthFormat;
        pass.samples = key.samples;
        const std::string pixelShader = (key.state & DRAW_PROFILER_NO_PIXEL_SHADER) != 0 ?
            std::string("none") : DrawProfilerShaderName(key.pixelShader, true);

        AppendFormat(report, "  %+7.3f ms %6.3f vs %6.3f  %5.1f vs %5.1f draws  %s  ps %s vs %s %s\n", row.slowMs - row.allMs,
            row.slowMs, row.allMs, row.slowDraws, row.allDraws, PassProfilerDescribe(pass, key.ordinal).c_str(),
            pixelShader.c_str(), DrawProfilerShaderName(key.vertexShader, true).c_str(),
            DrawProfilerStateText(key.state).c_str());
    }
}

// Every 300 frames (PassProfilerCollect): the pass table, the per-frame counters since the previous report, and the draw
// groups. Formatted here, written by the report writer thread.
static void PassProfilerReport()
{
    std::sort(g_passProfilerTotals.begin(), g_passProfilerTotals.end(),
        [](const PassProfilerTotal& a, const PassProfilerTotal& b) { return a.gpuMs > b.gpuMs; });

    const double frames = double(g_passProfilerFrameCount);
    auto perFrame = [frames](uint64_t value) { return double(value) / frames; };
    auto kilobytesPerFrame = [frames](uint64_t value) { return double(value) / 1024.0 / frames; };
    auto percent = [](uint64_t part, uint64_t whole) { return whole != 0 ? 100.0 * double(part) / double(whole) : 0.0; };

    // The counters since the previous report.
    static ResolveStats s_previousResolveStats{};
    static RendererStats s_previousRendererStats{};
    static uint64_t s_previousCommands = 0;
    static uint64_t s_previousBatches = 0;
    static uint64_t s_previousStatesSkipped = 0;
    static uint64_t s_previousConstantBytesCopied = 0;
    static uint64_t s_previousConstantBytesSpanned = 0;
    static uint32_t s_previousBufferUploads = 0;

    auto since = [](uint64_t now, uint64_t& previous)
        {
            const uint64_t difference = now - previous;
            previous = now;
            return difference;
        };

    const ResolveStats resolves = CountersSince(g_resolveStats, s_previousResolveStats);
    s_previousResolveStats = g_resolveStats;
    const RendererStats stats = CountersSince(g_rendererStats, s_previousRendererStats);
    s_previousRendererStats = g_rendererStats;

    const uint64_t commands = since(g_renderCommandsTaken.load(std::memory_order_relaxed), s_previousCommands);
    const uint64_t batches = since(g_profilerBatchesHandedOver.load(std::memory_order_relaxed), s_previousBatches);
    const uint64_t statesSkipped = since(g_profilerStatesSkipped.load(std::memory_order_relaxed), s_previousStatesSkipped);
    const uint64_t constantBytesCopied = since(g_profilerConstantBytesCopied.load(std::memory_order_relaxed), s_previousConstantBytesCopied);
    const uint64_t constantBytesSpanned = since(g_profilerConstantBytesSpanned.load(std::memory_order_relaxed), s_previousConstantBytesSpanned);
    const uint32_t bufferUploadsNow = g_bufferUploadCount.load(std::memory_order_relaxed);
    const uint32_t bufferUploads = bufferUploadsNow - s_previousBufferUploads;
    s_previousBufferUploads = bufferUploadsNow;

    std::string report;
    report.reserve(16384);
    AppendFormat(report, "[gpu passes] average over %u frames, GPU frame %.2f ms:\n", g_passProfilerFrameCount, g_passProfilerFrameMs / frames);

    // Resolves, clears, barriers and framebuffers (ResolveStats, the same counters as SwitchResolveStats).
    AppendResolveStats(report, "  per frame: ", resolves, frames);

    AppendFormat(report, "  per frame: %.1f draws with conditional rendering, %.1f without a pixel stage; %.0f render commands "
        "(%.1f batches handed over); %.1f texture updates, %.1f vertex/index buffer uploads, %.2f survey buffer resets\n",
        perFrame(stats.conditionalRenderingDraws), perFrame(stats.drawsWithoutPixelStage), perFrame(commands), perFrame(batches),
        perFrame(stats.textureUpdates), perFrame(bufferUploads), perFrame(stats.surveyBufferResets));

    AppendFormat(report, "  constants per frame: %.1f KB uploaded (vertex %.1f, pixel %.1f, shared %.1f), %.1f KB of the blocks not "
        "copied, %.1f draws without their dirty pixel constants; %.0f of %.0f constant runs changed nothing; %.1f pointers pushed; "
        "game thread copied %.1f KB (%.1f KB spanned by the changed registers)\n",
        kilobytesPerFrame(stats.vertexConstantBytes + stats.pixelConstantBytes + stats.sharedConstantBytes),
        kilobytesPerFrame(stats.vertexConstantBytes), kilobytesPerFrame(stats.pixelConstantBytes), kilobytesPerFrame(stats.sharedConstantBytes),
        kilobytesPerFrame(stats.constantBytesTrimmed), perFrame(stats.pixelConstantsSkipped), perFrame(stats.constantSetsUnchanged),
        perFrame(stats.constantSets), perFrame(stats.rootAddressPushes), kilobytesPerFrame(constantBytesCopied),
        kilobytesPerFrame(constantBytesSpanned));

    AppendFormat(report, "  pipelines per frame: %.0f binds (%.1f%% from the lookup cache), %.2f created on the render thread "
        "(%.2f ms), %.2f variants requested, %.2f added; %.0f sampler states (%.1f%% from the cache); %.0f render and sampler "
        "states not sent\n",
        perFrame(stats.pipelineBinds), percent(stats.pipelineLookupHits, stats.pipelineBinds), perFrame(stats.pipelinesCreated),
        double(stats.pipelineCreationUs) / 1000.0 / frames, perFrame(stats.pipelineVariantsRequested),
        perFrame(stats.pipelineVariantsAdded), perFrame(stats.samplerStates), percent(stats.samplerCacheHits, stats.samplerStates),
        perFrame(statesSkipped));

    {
        // SwitchCompactTextureHeap is exact while fewer than its 16,384 descriptors are in use.
        uint32_t inUse;
        uint32_t highWater;
        {
            std::lock_guard lock(g_textureDescriptorAllocator.mutex);
            highWater = g_textureDescriptorAllocator.capacity;
            inUse = highWater - uint32_t(g_textureDescriptorAllocator.freed.size());
        }

        AppendFormat(report, "  texture descriptors: %u in use, %u at most, heap of %u\n", inUse, highWater, g_textureDescriptorCount);
    }

    FrameTimeTotals frameTimes;
    {
        std::lock_guard lock(g_frameTimesMutex);
        frameTimes = g_frameTimeTotals;
        g_frameTimeTotals = {};
    }

    if (frameTimes.frames != 0)
    {
        const double timedFrames = double(frameTimes.frames);
        char pipelined[128] = "";
        if (frameTimes.pipelinedFrames != 0)
        {
            snprintf(pipelined, sizeof(pipelined), " (%u of the %u frames presented, waited for the GPU and got the next image on the render thread)",
                frameTimes.pipelinedFrames, frameTimes.frames);
        }

        AppendFormat(report, "  game thread per frame: %.2f ms working (longest %.1f), then in Present %.2f ms drawing ImGui, %.2f waiting "
            "for the render thread, %.2f presenting, %.2f waiting for the GPU, %.2f for the next image, %.2f in the frame limiter%s\n",
            frameTimes.times[FRAME_TIME_WORK] / timedFrames, frameTimes.longestWork, frameTimes.times[FRAME_TIME_IMGUI] / timedFrames,
            frameTimes.times[FRAME_TIME_RENDER_THREAD] / timedFrames, frameTimes.times[FRAME_TIME_PRESENT] / timedFrames,
            frameTimes.times[FRAME_TIME_GPU] / timedFrames, frameTimes.times[FRAME_TIME_ACQUIRE] / timedFrames,
            frameTimes.times[FRAME_TIME_LIMITER] / timedFrames, pipelined);
    }

    {
        // Each registered thread's share of the time since the previous report (it never waits for the CPU profiler).
        std::string threads;
        os::switch_cpu_profiler::AppendThreadCpuUsage(threads);
        report += "  cpu: ";
        report += threads.empty() ? "-" : threads;
        AppendFormat(report, "; audio gaps since start %u\n", g_switchAudioUnderruns.load(std::memory_order_relaxed));
    }

    double shown = 0.0;
    size_t lines = 0;
    for (const auto& total : g_passProfilerTotals)
    {
        if (lines++ == 30)
            break;

        const auto& p = total.pass;
        const double ms = total.gpuMs / frames;
        shown += ms;
        if (p.kind >= PASS_PROFILER_COPIES && p.kind < PASS_PROFILER_GAMMA)
        {
            AppendFormat(report, "  %6.2f ms  %4ux%-4u %-7s x%u copies %-11s #%u  draws %5.1f\n", ms, p.width, p.height,
                PassProfilerFormatName(p.colorFormat != RenderFormat::UNKNOWN ? p.colorFormat : p.depthFormat), p.samples,
                ResolveCopyTriggerName(p.kind - PASS_PROFILER_COPIES), total.ordinal, double(total.draws) / frames);
        }
        else if (p.kind == PASS_PROFILER_GAMMA)
        {
            AppendFormat(report, "  %6.2f ms  %4ux%-4u %-7s gamma pass #%u  draws %5.0f\n", ms, p.width, p.height,
                PassProfilerFormatName(p.colorFormat), total.ordinal, double(total.draws) / frames);
        }
        else if (p.width == 0)
        {
            AppendFormat(report, "  %6.2f ms  frame start (no target)  draws %5.0f\n", ms, double(total.draws) / frames);
        }
        else
        {
            AppendFormat(report, "  %6.2f ms  %4ux%-4u %-7s x%u depth %-6s #%u  draws %5.0f\n", ms, p.width, p.height,
                PassProfilerFormatName(p.colorFormat), p.samples, PassProfilerFormatName(p.depthFormat), total.ordinal,
                double(total.draws) / frames);
        }
    }

    AppendFormat(report, "  (%.2f ms in the passes listed)\n", shown);

    if (g_drawProfilerEnabled && !g_drawProfilerTotals.empty())
        DrawProfilerPrint(frames, report);

    if (g_gpuSlowFrameMs > 0.0)
        GpuSlowFramesPrint(frames, report);

    WriteReportAsync(std::move(report));

    g_passProfilerTotals.clear();
    g_drawProfilerTotals.clear();
    g_passProfilerFrameCount = 0;
    g_passProfilerFrameMs = 0.0;

    g_gpuAllCounters = {};
    g_gpuSlowCounters = {};
    g_gpuAllDraws = 0;
    g_gpuSlowDraws = 0;
    g_gpuSlowFrames = 0;
    g_gpuSlowFrameMsSum = 0.0;
    g_gpuSlowFrameLongestMs = 0.0;
    g_gpuSlowPassTotals.clear();
    g_gpuSlowestFrames.clear();
    g_drawProfilerSlowTotals.clear();
}

// At the start of every command list (ProcBeginCommandList, render thread): the frame that last used this slot is done
// (Present or PresentOnRenderThread waited for its fence, or WaitForGPU did), so its timestamps are final.
static void PassProfilerCollect()
{
    auto& frame = g_passProfilerFrames[g_frame];
    if (!g_passProfilerEnabled || !frame.recorded || frame.passes.empty())
        return;

    frame.recorded = false;
    frame.queries->queryResults();
    const uint64_t* timestamps = frame.queries->getResults();

    // The frame's GPU time: from the frame start to the closing timestamp, written next to the frame's own two.
    const double gpuFrameMs = GpuTimestampMs(int64_t(timestamps[frame.passes.size()] - timestamps[0]));

    // Group by target description and by the order among passes with the same description.
    std::vector<uint32_t> ordinals(frame.passes.size());
    std::vector<double> passMs(frame.passes.size());
    uint64_t frameDraws = 0;
    for (size_t i = 0; i < frame.passes.size(); i++)
    {
        const auto& pass = frame.passes[i];
        const double ms = GpuTimestampMs(int64_t(timestamps[i + 1] - timestamps[i]));

        uint32_t ordinal = 0;
        for (size_t j = 0; j < i; j++)
        {
            const auto& other = frame.passes[j];
            if (other.width == pass.width && other.height == pass.height && other.colorFormat == pass.colorFormat &&
                other.depthFormat == pass.depthFormat && other.samples == pass.samples && other.kind == pass.kind)
            {
                ordinal++;
            }
        }
        ordinals[i] = ordinal;
        passMs[i] = ms;
        frameDraws += pass.draws;

        PassProfilerTotal* total = FindPassTotal(g_passProfilerTotals, pass, ordinal, true);
        total->gpuMs += ms;
        total->draws += pass.draws;
    }

    // SwitchGpuSlowFrameMs.
    g_passProfilerFrameNumber++;
    g_gpuAllCounters.Add(frame.counters);
    g_gpuAllDraws += frameDraws;
    const bool slowFrame = g_gpuSlowFrameMs > 0.0 && gpuFrameMs >= g_gpuSlowFrameMs;
    if (slowFrame)
        PassProfilerTakeSlowFrame(frame, ordinals, passMs, frameDraws, gpuFrameMs);

    if (g_drawProfilerEnabled && !frame.draws.empty())
    {
        const uint32_t drawCount = uint32_t(frame.draws.size());
        const uint64_t* poolResults[DRAW_PROFILER_MAX_POOLS]{};
        for (uint32_t pool = 0; pool * DRAW_PROFILER_POOL_SIZE < drawCount; pool++)
        {
            frame.drawQueries[pool]->queryResults();
            poolResults[pool] = frame.drawQueries[pool]->getResults();
        }

        auto drawTimestamp = [&](uint32_t index)
            {
                return poolResults[index / DRAW_PROFILER_POOL_SIZE][index % DRAW_PROFILER_POOL_SIZE];
            };

        for (uint32_t i = 0; i < drawCount; i++)
        {
            const auto& draw = frame.draws[i];
            const uint64_t previous = (i > 0 && frame.draws[i - 1].pass == draw.pass) ? drawTimestamp(i - 1) : timestamps[draw.pass];
            const double ms = GpuTimestampMs(int64_t(drawTimestamp(i) - previous));

            const auto& pass = frame.passes[draw.pass];
            DrawProfilerKey key{};
            key.width = pass.width;
            key.height = pass.height;
            key.colorFormat = pass.colorFormat;
            key.depthFormat = pass.depthFormat;
            key.samples = pass.samples;
            key.ordinal = ordinals[draw.pass];
            key.vertexShader = draw.vertexShader;
            key.pixelShader = draw.pixelShader;
            key.state = draw.state;

            auto& total = g_drawProfilerTotals[key];
            total.gpuMs += ms;
            total.draws++;
            total.count += draw.count;

            if (slowFrame)
            {
                auto& slowTotal = g_drawProfilerSlowTotals[key];
                slowTotal.gpuMs += ms;
                slowTotal.draws++;
                slowTotal.count += draw.count;
            }
        }
    }

    g_passProfilerFrameMs += gpuFrameMs;

    if (++g_passProfilerFrameCount < PASS_PROFILER_REPORT_FRAMES)
        return;

    PassProfilerReport();
}
#endif

static void ProcExecuteCommandList(const RenderCommand& cmd)
{
    if (g_swapChainValid)
    {
        auto swapChainTexture = g_swapChain->getTexture(g_backBufferIndex);
        if (g_backBuffer->texture == g_intermediaryBackBufferTexture.get())
        {
            struct
            {
                float gamma;
                uint32_t textureDescriptorIndex;

                int32_t viewportOffsetX;
                int32_t viewportOffsetY;
                int32_t viewportWidth;
                int32_t viewportHeight;
            } constants;

            constants.gamma = 0.85f;

            float brightness = Config::Brightness;
            uint32_t viewportWidth = Video::s_viewportWidth;
            uint32_t viewportHeight = Video::s_viewportHeight;
#if defined(__SWITCH__)
            // SwitchPresentWithoutRecordWait: the values Present took (see RenderCommand::executeCommandList).
            if (cmd.executeCommandList.captured)
            {
                brightness = cmd.executeCommandList.brightness;
                viewportWidth = cmd.executeCommandList.viewportWidth;
                viewportHeight = cmd.executeCommandList.viewportHeight;
            }
#endif

            float offset = (brightness - 0.5f) * 1.2f;

            constants.gamma = 1.0f / std::clamp(constants.gamma + offset, 0.1f, 4.0f);
            constants.textureDescriptorIndex = g_intermediaryBackBufferTextureDescriptorIndex;

            constants.viewportOffsetX = (int32_t(g_swapChain->getWidth()) - int32_t(viewportWidth)) / 2;
            constants.viewportOffsetY = (int32_t(g_swapChain->getHeight()) - int32_t(viewportHeight)) / 2;
            constants.viewportWidth = viewportWidth;
            constants.viewportHeight = viewportHeight;

            auto &framebuffer = g_backBuffer->framebuffers[swapChainTexture];
            if (!framebuffer)
            {
                RenderFramebufferDesc desc;
                desc.colorAttachments = const_cast<const RenderTexture **>(&swapChainTexture);
                desc.colorAttachmentsCount = 1;
                framebuffer = g_device->createFramebuffer(desc);
            }

            RenderTextureBarrier srcBarriers[] =
            {
                RenderTextureBarrier(g_intermediaryBackBufferTexture.get(), RenderTextureLayout::SHADER_READ),
                RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COLOR_WRITE)
            };

            auto &commandList = g_commandLists[g_frame];
#if defined(__SWITCH__)
            SurveyBarrierScope surveyBarrierScope;
#endif
            commandList->barriers(RenderBarrierStage::GRAPHICS, srcBarriers, std::size(srcBarriers));
            commandList->setGraphicsPipelineLayout(g_pipelineLayout.get());
            commandList->setPipeline(g_gammaCorrectionPipeline.get());
            commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
#if defined(__SWITCH__)
            if (g_gammaPushConstants)
            {
                // [Switch] SwitchGammaPushConstants. The 24 bytes of constants fill the 24-byte push constant
                // range (normally the three constant pointers), which the driver serves from its root constant
                // bank, instead of four loads through the shared constants pointer. The shader reads the same
                // values with the same layout. This is the command list's last draw, and the next one starts by
                // pushing all three pointers again (Present: DirtyStates(true)).
                static_assert(sizeof(constants) == 24);
                commandList->setGraphicsPushConstants(0, &constants, 0, sizeof(constants));
            }
            else
#endif
            {
                SetRootDescriptor(g_uploadAllocators[g_frame].allocate<false>(&constants, sizeof(constants), 0x100), 2);
            }
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
            InvalidatePushedRootAddresses();
#endif
#if defined(__SWITCH__)
            PassProfilerGamma(g_swapChain->getWidth(), g_swapChain->getHeight());
            FrameLogPass(nullptr, nullptr, "gamma pass: the back buffer into the swap chain image");
#endif
            commandList->setFramebuffer(framebuffer.get());
            commandList->setViewports(RenderViewport(0.0f, 0.0f, g_swapChain->getWidth(), g_swapChain->getHeight()));
            commandList->setScissors(RenderRect(0, 0, g_swapChain->getWidth(), g_swapChain->getHeight()));
            commandList->drawInstanced(CopyTriangleVertexCount(), 1, 0, 0);
            commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));
        }
        else
        {
            AddBarrier(g_backBuffer, RenderTextureLayout::PRESENT);
            FlushBarriers();
        }
    }

    auto &commandList = g_commandLists[g_frame];
#if defined(__SWITCH__)
    PassProfilerEndFrame();
    FrameLogEnd();
#endif
    commandList->writeTimestamp(g_queryPools[g_frame].get(), 1);
#if defined(__SWITCH__)
    g_commandListRecording = false;
#endif
    commandList->end();

#if defined(__SWITCH__)
    ReportResolveStats();

    // SwitchPresentOnRenderThread: every command of the frame is recorded; the D3D thread may go on.
    const bool pipelined = cmd.executeCommandList.pipelined;
    if (pipelined)
    {
        g_recordedCommandList.store(true, std::memory_order_release);
        g_recordedCommandList.notify_one();
    }
#endif

    if (g_swapChainValid)
    {
        const RenderCommandList *commandLists[] = { commandList.get() };
        RenderCommandSemaphore *waitSemaphores[] = { g_acquireSemaphores[g_frame].get() };
        RenderCommandSemaphore *signalSemaphores[] = { g_renderSemaphores[g_frame].get() };

        g_queue->executeCommandLists(
            commandLists, std::size(commandLists),
            waitSemaphores, std::size(waitSemaphores),
            signalSemaphores, std::size(signalSemaphores),
            g_commandFences[g_frame].get());
    }
    else
    {
        g_queue->executeCommandLists(commandList.get(), g_commandFences[g_frame].get());
    }

    g_commandListStates[g_frame] = true;

#if defined(__SWITCH__)
    if (pipelined)
    {
        PresentOnRenderThread();
        return;
    }
#endif

    g_executedCommandList = true;
    g_executedCommandList.notify_one();
}

static void ProcBeginCommandList(const RenderCommand& cmd)
{
#if defined(__SWITCH__)
    // SwitchGpuPassProfiler: the timestamps of the frame this slot held, before BeginCommandList resets them.
    PassProfilerCollect();
#endif
    DestructTempResources();
    BeginCommandList();
#if defined(__SWITCH__)
    FrameLogBegin();
#endif
}

static GuestSurface* GetBackBuffer() 
{
    g_backBuffer->AddRef();
    return g_backBuffer;
}

static GuestSurface* GetDepthStencil() 
{
#if defined(__SWITCH__)
    // [Switch] g_depthStencil is the render thread's (the surface of the last SetDepthStencilSurface it processed):
    // see CatchUpWithD3DThread. The game calls this once, while it sets its device up.
    CatchUpWithD3DThread();
#endif
    g_depthStencil->AddRef();
    return g_depthStencil;
}

void Video::ComputeViewportDimensions()
{
    uint32_t width = g_swapChain->getWidth();
    uint32_t height = g_swapChain->getHeight();
    float aspectRatio = float(width) / float(height);

    switch (Config::AspectRatio)
    {
        case EAspectRatio::Original:
        {
            if (aspectRatio > WIDE_ASPECT_RATIO)
            {
                s_viewportWidth = height * 16 / 9;
                s_viewportHeight = height;
            }
            else
            {
                s_viewportWidth = width;
                s_viewportHeight = width * 9 / 16;
            }

            break;
        }

        default:
            s_viewportWidth = width;
            s_viewportHeight = height;
            break;
    }

    AspectRatioPatches::ComputeOffsets();
}

static RenderFormat ConvertFormat(uint32_t format)
{
    switch (format)
    {
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_A16B16G16R16F_2:
    case D3DFMT_A16B16G16R16F_EXPAND:
        return RenderFormat::R16G16B16A16_FLOAT;
    case D3DFMT_LIN_A8R8G8B8:
        return RenderFormat::B8G8R8A8_UNORM;
    case D3DFMT_A8B8G8R8:
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_LE_X8R8G8B8:
        return RenderFormat::R8G8B8A8_UNORM;
    case D3DFMT_R32F:
        return RenderFormat::R32_FLOAT;
    case D3DFMT_D24FS8:
    case D3DFMT_D24S8:
        return RenderFormat::D32_FLOAT_S8_UINT;
    case D3DFMT_G16R16F:
    case D3DFMT_G16R16F_2:
        return RenderFormat::R16G16_FLOAT;
    case D3DFMT_INDEX16:
        return RenderFormat::R16_UINT;
    case D3DFMT_INDEX32:
        return RenderFormat::R32_UINT;
    case D3DFMT_A8:
    case D3DFMT_L8:
    case D3DFMT_L8_2:
        return RenderFormat::R8_UNORM;
    case D3DFMT_DXT1:
        return RenderFormat::BC1_UNORM;
    case D3DFMT_DXT4:
        return RenderFormat::BC3_UNORM;
    default:
        LOGF_WARNING("{:x}\n", format);
        assert(false && "Unknown format");
        return RenderFormat::R16G16B16A16_FLOAT;
    }
}

static void DiscardTexture(GuestBaseTexture* texture, RenderTextureLayout layout)
{
    if (g_backend == Backend::D3D12)
    {
        std::lock_guard lock(g_discardMutex);

        g_discardCommandList->begin();
        if (texture->layout != layout)
        {
            g_discardCommandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture->texture, layout));
            texture->layout = layout;
        }

        g_discardCommandList->discardTexture(texture->texture);
        g_discardCommandList->end();

        g_queue->executeCommandLists(g_discardCommandList.get(), g_discardCommandFence.get());
        g_queue->waitForCommandFence(g_discardCommandFence.get());
    }
}

static GuestTexture* CreateTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, uint32_t usage, uint32_t format, uint32_t pool, uint32_t type) 
{
    ResourceType resourceType;

    switch (type)
    {
    case 17:
        resourceType = ResourceType::VolumeTexture;
        break;
    case 19:
        resourceType = ResourceType::ArrayTexture;
        break;
    default:
        resourceType = ResourceType::Texture;
        break;
    }

    const auto texture = g_userHeap.AllocPhysical<GuestTexture>(resourceType);

    RenderTextureDesc desc;
    desc.dimension = texture->type == ResourceType::VolumeTexture ? RenderTextureDimension::TEXTURE_3D : RenderTextureDimension::TEXTURE_2D;
    desc.width = width;
    desc.height = height;
    desc.mipLevels = levels;
    desc.format = ConvertFormat(format);

#if defined(__SWITCH__)
    // [Switch] The game's depth array textures (the cascaded shadow maps, "csm") only ever receive copy_depth resolves
    // (depth test ALWAYS and written, stencil off) and are only sampled through depth-aspect views (RRR1 below).
    // SwitchDepthArrayTexturesD32: as D32_FLOAT instead of D32_FLOAT_S8_UINT (8 bytes a texel on NVK) the copies write,
    // and every shadow tap fetches, half the bytes. The copy writes the same float32 depth, clamped to [0, 1] alike for
    // both floating-point depth formats, and a depth-aspect view returns the stored float for both, with the same
    // swizzle; the stencil of these textures is never written nor read (a GuestTexture is never a depth-stencil
    // surface). NO_ZCULL: with SwitchZcull their copy passes (test ALWAYS, which never culls) would only pay the ZCULL
    // plane's loads and stores. A CPU update of such a texture is logged (ProcUnlockTextureRect): the texel size differs.
    const bool depthArray = resourceType == ResourceType::ArrayTexture && desc.format == RenderFormat::D32_FLOAT_S8_UINT;
    if (depthArray && g_switchRenderer.depthArrayTexturesD32)
        desc.format = RenderFormat::D32_FLOAT;

    // SwitchDepthTexturesD32: the 2D depth textures, by the same argument (they too only receive copy_depth and are
    // only sampled through depth-aspect views). Not with MSAA, whose depth resolves are made for D32_FLOAT_S8_UINT
    // targets (the Switch build keeps MSAA off; ExecutePendingCopy reports the case if it ever comes).
    const bool depth2D = resourceType == ResourceType::Texture && desc.format == RenderFormat::D32_FLOAT_S8_UINT;
    if (depth2D && g_switchRenderer.depthTexturesD32 && Config::AntiAliasing == EAntiAliasing::Off)
        desc.format = RenderFormat::D32_FLOAT;
#endif

    if (texture->type == ResourceType::ArrayTexture) {
        desc.arraySize = depth;
        desc.depth = 1;
    } else {
        desc.depth = depth;
        desc.arraySize = 1;
    }

    if (RenderFormatIsDepth(desc.format))
        desc.flags = RenderTextureFlag::DEPTH_TARGET;
    else if (usage != 0)
        desc.flags = RenderTextureFlag::RENDER_TARGET;
    else
        desc.flags = RenderTextureFlag::NONE;

#if defined(__SWITCH__)
    if (depthArray || (depth2D && desc.format == RenderFormat::D32_FLOAT))
        desc.flags |= RenderTextureFlag::NO_ZCULL;
#endif

    RenderTextureDesc achieved = desc;
    texture->textureHolder = CreateTextureChecked(desc, "guest-texture", &achieved);
    texture->texture = texture->textureHolder.get();

    RenderTextureViewDesc viewDesc;
    viewDesc.format = desc.format;
    viewDesc.dimension = texture->type == ResourceType::VolumeTexture ? RenderTextureViewDimension::TEXTURE_3D : RenderTextureViewDimension::TEXTURE_2D;
    viewDesc.mipLevels = achieved.mipLevels;

    switch (format)
    {
    case D3DFMT_D24FS8:
    case D3DFMT_D24S8:
    case D3DFMT_L8:
    case D3DFMT_L8_2:
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::R, RenderSwizzle::R, RenderSwizzle::R, RenderSwizzle::ONE);
        break;

    case D3DFMT_X8R8G8B8:
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::G, RenderSwizzle::B, RenderSwizzle::A, RenderSwizzle::ONE);
        break;
    }

    texture->textureView = texture->texture->createTextureView(viewDesc);

#if defined(__SWITCH__)
    // [Switch] SwitchResolveHandOver / SwitchCoverageHandOver: a surface's image can become this texture's image
    // (HandOverSurfaceImage) only when the two were created alike: a 2D texture of one level, one sample (textures have
    // no MSAA), made as a render or depth target like the surfaces; format and size are compared with the surface's.
    texture->viewDesc = viewDesc;
    texture->handOverCapable = texture->type == ResourceType::Texture && achieved.mipLevels == 1 && achieved.depth <= 1 &&
        achieved.arraySize == 1 && desc.flags == (RenderFormatIsDepth(desc.format) ? RenderTextureFlag::DEPTH_TARGET : RenderTextureFlag::RENDER_TARGET);
#endif

    // Store the ACHIEVED dimensions (may be reduced by the fallback), never the
    // requested ones — framebuffers/viewports built from these must match the
    // actual image or the GPU hangs.
    texture->width = achieved.width;
    texture->height = achieved.height;
    texture->depth = texture->type == ResourceType::ArrayTexture ? achieved.arraySize : achieved.depth;
    texture->format = desc.format;
    texture->mipLevels = viewDesc.mipLevels;
    texture->viewDimension = viewDesc.dimension;
    texture->descriptorIndex = g_textureDescriptorAllocator.allocate();

    SetTextureDescriptor(texture->descriptorIndex, texture->texture, texture->width, texture->height, RenderTextureLayout::SHADER_READ,
        texture->textureView.get(), texture->mipLevels, texture->type == ResourceType::ArrayTexture ? texture->depth : 1);

#ifdef _DEBUG 
    texture->texture->setName(fmt::format("Texture {:X}", g_memory.MapVirtual(texture)));
#endif

    if (desc.flags != RenderTextureFlag::NONE)
    {
        DiscardTexture(texture, desc.flags == RenderTextureFlag::RENDER_TARGET ?
            RenderTextureLayout::COLOR_WRITE : RenderTextureLayout::DEPTH_WRITE);
    }

    // printf("CreateTexture: w: %d, h: %d, depth: %d, levels: %d, usage: %d, format: %d, pool: %d, type: %d - %x\n", width, height, depth, levels, usage, format, pool, type, texture);
    return texture;
}

static RenderHeapType GetBufferHeapType()
{
    return g_capabilities.gpuUploadHeap ? RenderHeapType::GPU_UPLOAD : RenderHeapType::DEFAULT;
}

static GuestBuffer* CreateVertexBuffer(uint32_t length) 
{
    auto buffer = g_userHeap.AllocPhysical<GuestBuffer>(ResourceType::VertexBuffer);
    buffer->buffer = g_device->createBuffer(RenderBufferDesc::VertexBuffer(length, GetBufferHeapType(), RenderBufferFlag::INDEX));
    buffer->dataSize = length;
#ifdef _DEBUG 
    buffer->buffer->setName(fmt::format("Vertex Buffer {:X}", g_memory.MapVirtual(buffer)));
#endif
    return buffer;
}

static GuestBuffer* CreateIndexBuffer(uint32_t length, uint32_t, uint32_t format)
{
    auto buffer = g_userHeap.AllocPhysical<GuestBuffer>(ResourceType::IndexBuffer);
    buffer->buffer = g_device->createBuffer(RenderBufferDesc::IndexBuffer(length, GetBufferHeapType()));
    buffer->dataSize = length;
    buffer->format = ConvertFormat(format);
    buffer->guestFormat = format;
#ifdef _DEBUG 
    buffer->buffer->setName(fmt::format("Index Buffer {:X}", g_memory.MapVirtual(buffer)));
#endif
    return buffer;
}

static std::vector<std::pair<GuestSurface*, uint32_t>> g_surfaceCache;

// TODO: Singleplayer (possibly) uses the same memory location in EDRAM for HDR and FB0 surfaces,
// so we just remember who was created first and use that instead of creating a new one.
static GuestSurface* CreateSurface(uint32_t width, uint32_t height, uint32_t format, uint32_t multiSample, GuestSurfaceCreateParams* params) 
{
    GuestSurface* surface = nullptr;
    uint32_t baseValue = params ? params->base.get() : -1;
    if (params) {
        for (auto& entry : g_surfaceCache) {
            GuestSurface* cachedSurface = entry.first;
            uint32_t cachedBase = entry.second;
            if (cachedSurface &&
                cachedSurface->width == width &&
                cachedSurface->height == height &&
                cachedSurface->guestFormat == format &&
                cachedBase == baseValue) {
                surface = cachedSurface;
                break;
            }
        }
    }
    if (!surface) {
        // printf("CreateSurface: w: %d, h: %d, f: %d, ms: %d\n", width, height, format, multiSample);
        RenderTextureDesc desc;
        desc.dimension = RenderTextureDimension::TEXTURE_2D;
        desc.width = width;
        desc.height = height;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arraySize = 1;
        // A guest multisample request only produces an MSAA surface when the
        // Anti-Aliasing setting allows it (forcing it unconditionally drove the
        // MSAA path that hangs NVK on Switch).
        desc.multisampling.sampleCount = multiSample != 0 && Config::AntiAliasing != EAntiAliasing::Off ? int32_t(Config::AntiAliasing.Value) : RenderSampleCount::COUNT_1;
        desc.format = ConvertFormat(format);
        desc.flags = RenderFormatIsDepth(desc.format) ? RenderTextureFlag::DEPTH_TARGET : RenderTextureFlag::RENDER_TARGET;

        surface = g_userHeap.AllocPhysical<GuestSurface>(RenderFormatIsDepth(desc.format) ?
            ResourceType::DepthStencil : ResourceType::RenderTarget);

        RenderTextureDesc achieved = desc;
        surface->textureHolder = CreateTextureChecked(desc, "guest-surface", &achieved);
        surface->texture = surface->textureHolder.get();
        // Achieved dimensions, not requested — see CreateTextureChecked.
        surface->width = achieved.width;
        surface->height = achieved.height;
        surface->format = desc.format;
        surface->guestFormat = format;
        surface->sampleCount = desc.multisampling.sampleCount;

        RenderTextureViewDesc viewDesc;
        viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
        viewDesc.format = desc.format;
        viewDesc.mipLevels = 1;
        surface->textureView = surface->textureHolder->createTextureView(viewDesc);
        surface->descriptorIndex = g_textureDescriptorAllocator.allocate();
        SetTextureDescriptor(surface->descriptorIndex, surface->textureHolder.get(), surface->width, surface->height, RenderTextureLayout::SHADER_READ,
            surface->textureView.get(), viewDesc.mipLevels);

    #ifdef _DEBUG 
        surface->texture->setName(fmt::format("{} {:X}", desc.flags & RenderTextureFlag::RENDER_TARGET ? "Render Target" : "Depth Stencil", g_memory.MapVirtual(surface)));
    #endif

        DiscardTexture(surface, desc.flags == RenderTextureFlag::RENDER_TARGET ?
            RenderTextureLayout::COLOR_WRITE : RenderTextureLayout::DEPTH_WRITE);

        if (params) {
            surface->wasCached = true;
            g_surfaceCache.emplace_back(surface, baseValue);
        }
    }

    return surface;
}

#if defined(__SWITCH__)
// The viewport and scissor rectangle FlushViewport gives the GPU for the current state, and the scale it applied to them
// (1 when none). The coverage proofs (GetFullScreenCoverage) use the same values.
static float ComputeFlushViewport(RenderViewport& viewport, RenderRect& scissorRect)
{
    // Capped render targets (CreateTextureChecked can shrink e.g. the 2048
    // shadowmap to 1024) leave the guest still setting the original viewport,
    // and rendering beyond the attachment wedges the GPU channel on NVK — a
    // GPU hang under title takeover freezes the entire console. SCALE the
    // viewport/scissor down to the bound target instead of clamping: the pass
    // then covers the whole (smaller) target, so normalized-UV sampling stays
    // correct (a plain clamp rendered shadows into one quadrant, visibly
    // misplacing them).
    float maxW = FLT_MAX, maxH = FLT_MAX;
    if (g_renderTarget != nullptr)
    {
        maxW = float(g_renderTarget->width);
        maxH = float(g_renderTarget->height);
    }
    if (g_depthStencil != nullptr)
    {
        maxW = std::min(maxW, float(g_depthStencil->width));
        maxH = std::min(maxH, float(g_depthStencil->height));
    }

    float rtScale = 1.0f;
    if (maxW != FLT_MAX && g_viewport.width > 0.0f && g_viewport.height > 0.0f)
    {
        const float extentW = g_viewport.x + g_viewport.width;
        const float extentH = g_viewport.y + g_viewport.height;
        if (extentW > maxW || extentH > maxH)
            rtScale = std::min(maxW / extentW, maxH / extentH);
    }

    viewport = g_viewport;

    // if (viewport.minDepth > viewport.maxDepth)
    //     std::swap(viewport.minDepth, viewport.maxDepth);

    if (rtScale != 1.0f)
    {
        viewport.x *= rtScale;
        viewport.y *= rtScale;
        viewport.width *= rtScale;
        viewport.height *= rtScale;
    }

    scissorRect = g_scissorTestEnable ? g_scissorRect : RenderRect(
        g_viewport.x,
        g_viewport.y,
        g_viewport.x + g_viewport.width,
        g_viewport.y + g_viewport.height);

    if (rtScale != 1.0f)
    {
        scissorRect.left = int32_t(float(scissorRect.left) * rtScale);
        scissorRect.top = int32_t(float(scissorRect.top) * rtScale);
        scissorRect.right = int32_t(float(scissorRect.right) * rtScale);
        scissorRect.bottom = int32_t(float(scissorRect.bottom) * rtScale);
    }
    if (maxW != FLT_MAX)
    {
        // Safety clamp against residual overshoot from rounding.
        scissorRect.right = std::min<int32_t>(scissorRect.right, int32_t(maxW));
        scissorRect.bottom = std::min<int32_t>(scissorRect.bottom, int32_t(maxH));
        scissorRect.left = std::min(scissorRect.left, scissorRect.right);
        scissorRect.top = std::min(scissorRect.top, scissorRect.bottom);
    }

    return rtScale;
}

static void FlushViewport()
{
    auto& commandList = g_commandLists[g_frame];

    if (!g_dirtyStates.viewport && !g_dirtyStates.scissorRect)
        return;

    RenderViewport viewport;
    RenderRect scissorRect;
    ComputeFlushViewport(viewport, scissorRect);

    if (g_dirtyStates.viewport)
    {
        commandList->setViewports(viewport);
        g_appliedViewport = viewport;
        g_appliedViewportValid = true;

        g_dirtyStates.viewport = false;
    }

    if (g_dirtyStates.scissorRect)
    {
        commandList->setScissors(scissorRect);
        g_appliedScissor = scissorRect;
        g_appliedScissorValid = true;

        g_dirtyStates.scissorRect = false;
    }
}
#else
static void FlushViewport()
{
    auto& commandList = g_commandLists[g_frame];

    if (g_dirtyStates.viewport)
    {
        auto viewport = g_viewport;

        // if (viewport.minDepth > viewport.maxDepth)
        //     std::swap(viewport.minDepth, viewport.maxDepth);

        commandList->setViewports(viewport);

        g_dirtyStates.viewport = false;
    }

    if (g_dirtyStates.scissorRect)
    {
        auto scissorRect = g_scissorTestEnable ? g_scissorRect : RenderRect(
            g_viewport.x,
            g_viewport.y,
            g_viewport.x + g_viewport.width,
            g_viewport.y + g_viewport.height);

        commandList->setScissors(scissorRect);

        g_dirtyStates.scissorRect = false;
    }
}
#endif

static void StretchRect(GuestDevice* device, uint32_t flags, uint32_t, GuestTexture* texture, uint32_t, uint32_t, uint32_t destSliceOrFace)
{
    // printf("StretchRect %x\n", texture);
    RenderCommand cmd;
    cmd.type = RenderCommandType::StretchRect;
    cmd.stretchRect.flags = flags;
    cmd.stretchRect.texture = texture;
    cmd.stretchRect.destSliceOrFace = destSliceOrFace;
    EnqueueRenderCommand(cmd);
}

static void SetTextureInRenderThread(uint32_t index, GuestTexture* texture);
static void SetSurface(uint32_t index, GuestSurface* surface);

#if defined(__SWITCH__)
static bool HasOwedCopies(const GuestSurface* surface);
static void MakeOwedCopies(GuestSurface* surface, uint32_t trigger);
static void MakeCopyThePortMade(GuestTexture* texture, uint32_t trigger);
static void SetSurfaceAsTexture(uint32_t index, GuestSurface* surface);
static bool ForgetPendingResolves(GuestResource* resource);
static bool UsesLongerPendingCopies();
#endif

static void ProcStretchRect(const RenderCommand& cmd)
{
    const auto& args = cmd.stretchRect;

    const bool isDepthStencil = (args.flags & 0x4) != 0;
    const auto surface = isDepthStencil ? g_depthStencil : g_renderTarget;

#if defined(__SWITCH__)
    FrameLogResolve(surface, args.texture);

    // Copies the port had made before this resolve (see g_owedCopies and SwitchKeepResolvesPending) are made first: those
    // of the surface, which then holds only this resolve's (its copies stay all owed or none), and the texture's own,
    // which the port made before the texture was resolved into again.
    if (HasOwedCopies(surface))
        MakeOwedCopies(surface, RESOLVE_COPY_OTHER);

    if (args.texture->copyOwed || args.texture->pendingCarried)
        MakeCopyThePortMade(args.texture, RESOLVE_COPY_OTHER);

    // SwitchCascadeAdoption: a cascade's resolve into its slice is held in the cascade image instead of copied.
    if (CascadeOnResolve(surface, args.texture, args.destSliceOrFace))
        return;
#endif

    // Erase previous pending command so it doesn't cause the texture to be overriden.
    if (args.texture->sourceSurface != nullptr)
        args.texture->sourceSurface->destinationTextures.erase(args.texture);

    args.texture->sourceSurface = surface;
    // printf("ProcStretchRect: surface - %x %x ? (%x : %x)\n", surface, isDepthStencil, g_depthStencil, g_renderTarget);
    surface->destinationTextures.emplace(args.texture, args.destSliceOrFace);

    // If the texture is assigned to any slots, set it again. This'll also push the barrier.
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (g_textures[i] == args.texture)
        {
            // TODO: Render depth directly to slice and avoid copy
            // Set the original texture for MSAA and surface-to-array textures as they always get resolved.
            if (surface->sampleCount != RenderSampleCount::COUNT_1 ||
                args.texture->type == ResourceType::ArrayTexture)
            {
                SetTextureInRenderThread(i, args.texture);
                g_pendingResolves.emplace(surface);
            }
            else
            {
                SetSurface(i, surface);
            }
        }
    }

    // Remember to clear later.
    g_pendingSurfaceCopies.emplace(surface);
}

static void SetDefaultViewport(GuestDevice* device, GuestSurface* surface)
{
    if (surface != nullptr)
    {
        RenderCommand cmd;
        cmd.type = RenderCommandType::SetViewport;
        cmd.setViewport.x = 0.0f;
        cmd.setViewport.y = 0.0f;
        cmd.setViewport.width = float(surface->width);
        cmd.setViewport.height = float(surface->height);
        cmd.setViewport.minDepth = 0.0f;
        cmd.setViewport.maxDepth = 1.0f;
        EnqueueRenderCommand(cmd);

        device->viewport.x = 0.0f;
        device->viewport.y = 0.0f;
        device->viewport.width = float(surface->width);
        device->viewport.height = float(surface->height);
        device->viewport.minZ = 0.0f;
        device->viewport.maxZ = 1.0f;
    }
}

static void SetRenderTarget(GuestDevice* device, uint32_t index, GuestSurface* renderTarget) 
{
    if (index == 0)
    {
        RenderCommand cmd;
        cmd.type = RenderCommandType::SetRenderTarget;
        cmd.setRenderTarget.renderTarget = renderTarget;
        EnqueueRenderCommand(cmd);

        SetDefaultViewport(device, renderTarget);
    }
    else
    {
        // Multiple targets are not currently handled. Make sure any attempt to set them is nullptr.
        assert(renderTarget == nullptr);
    }
}

static void ProcSetRenderTarget(const RenderCommand& cmd)
{
    const auto& args = cmd.setRenderTarget;
    SetDirtyValue(g_dirtyStates.renderTargetAndDepthStencil, g_renderTarget, args.renderTarget);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.renderTargetFormat, args.renderTarget != nullptr ? args.renderTarget->format : RenderFormat::UNKNOWN);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.sampleCount, args.renderTarget != nullptr ? args.renderTarget->sampleCount : RenderSampleCount::COUNT_1);

    // When alpha to coverage is enabled, update the alpha test mode as it's dependent on sample count.
    SetAlphaTestMode((g_pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) != 0);
}

static void SetDepthStencilSurface(GuestDevice* device, GuestSurface* depthStencil) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetDepthStencilSurface;
    cmd.setDepthStencilSurface.depthStencil = depthStencil;
    EnqueueRenderCommand(cmd);

    SetDefaultViewport(device, depthStencil);
}

static void ProcSetDepthStencilSurface(const RenderCommand& cmd)
{
    const auto& args = cmd.setDepthStencilSurface;

    SetDirtyValue(g_dirtyStates.renderTargetAndDepthStencil, g_depthStencil, args.depthStencil);
#if defined(__SWITCH__)
    // SwitchCascadeAdoption: the cascade image's format while the shadow surface draws into it.
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthStencilFormat, args.depthStencil != nullptr ? CascadeDepthFormat(args.depthStencil) : RenderFormat::UNKNOWN);
#else
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthStencilFormat, args.depthStencil != nullptr ? args.depthStencil->format : RenderFormat::UNKNOWN);
#endif
}

static bool PopulateBarriersForStretchRect(GuestSurface* renderTarget, GuestSurface* depthStencil)
{
    bool addedAny = false;

    for (const auto surface : { renderTarget, depthStencil })
    {
        if (surface != nullptr && !surface->destinationTextures.empty())
        {
            const bool multiSampling = surface->sampleCount != RenderSampleCount::COUNT_1;

            RenderTextureLayout srcLayout;
            RenderTextureLayout dstLayout;
            bool shaderResolve = true;

            if (multiSampling)
            {
                if (!RenderFormatIsDepth(surface->format) || g_capabilities.resolveModes)
                {
                    srcLayout = RenderTextureLayout::RESOLVE_SOURCE;
                    dstLayout = RenderTextureLayout::RESOLVE_DEST;
                    shaderResolve = false;
                }
            }

            if (shaderResolve)
            {
                srcLayout = RenderTextureLayout::SHADER_READ;
                dstLayout = (RenderFormatIsDepth(surface->format) ? RenderTextureLayout::DEPTH_WRITE : RenderTextureLayout::COLOR_WRITE);
            }

            AddBarrier(surface, srcLayout);

            for (const auto [texture, _] : surface->destinationTextures)
                AddBarrier(texture, dstLayout);

            addedAny = true;
        }
    }

    return addedAny;
}

#if defined(__SWITCH__)
// [Switch] The render thread's framebuffer changes (every commandList->setFramebuffer of the game's passes), for
// SwitchStableFramebuffers: g_boundTargets describes g_framebuffer only while no other framebuffer was bound since.
static uint32_t g_framebufferBindCount = 0;

struct BoundTargets
{
    RenderFramebuffer* framebuffer = nullptr;
    GuestSurface* renderTarget = nullptr;
    GuestSurface* depthStencil = nullptr;
    uint32_t bindCount = 0;
};

static BoundTargets g_boundTargets;
#endif

// One pending copy: `texture` (slice `slice` of it) gets the contents of `surface`, and the slots that hold it read it
// again. The caller made the barriers (PopulateBarriersForStretchRect) and takes it out of destinationTextures.
static void ExecutePendingCopy(GuestSurface* surface, GuestTexture* texture, uint32_t slice)
{
    auto& commandList = g_commandLists[g_frame];
    const bool multiSampling = surface->sampleCount != RenderSampleCount::COUNT_1;
    const bool isDepthStencil = RenderFormatIsDepth(surface->format);

#if defined(__SWITCH__)
    PassProfilerCopy(surface, texture);
    g_resolveStats.copies[g_resolveCopyTrigger]++;
    g_resolveStats.copyPixels += uint64_t(texture->width) * texture->height;
    if (texture->type == ResourceType::ArrayTexture)
        g_resolveStats.arrayCopies++;

    texture->copyOwed = false;
    texture->pendingCarried = false;
#endif

    bool shaderResolve = true;

    if (multiSampling)
    {
        if (!isDepthStencil || g_capabilities.resolveModes)
        {
            if (isDepthStencil)
                commandList->resolveTextureRegion(texture->texture, 0, 0, surface->texture, nullptr, RenderResolveMode::MIN);
            else
                commandList->resolveTexture(texture->texture, surface->texture);

            shaderResolve = false;
        }
    }

    if (shaderResolve)
    {
        RenderPipeline* pipeline = nullptr;

        if (multiSampling)
        {
            uint32_t pipelineIndex = 0;

            switch (surface->sampleCount)
            {
            case RenderSampleCount::COUNT_2:
                pipelineIndex = 0;
                break;
            case RenderSampleCount::COUNT_4:
                pipelineIndex = 1;
                break;
            case RenderSampleCount::COUNT_8:
                pipelineIndex = 2;
                break;
            default:
                assert(false && "Unsupported MSAA sample count");
                break;
            }

            if (isDepthStencil)
            {
                pipeline = g_resolveMsaaDepthPipelines[pipelineIndex].get();
#if defined(__SWITCH__)
                // SwitchDepthTexturesD32 keeps 2D depth textures D32_FLOAT_S8_UINT with MSAA (CreateTexture); an array
                // texture (SwitchDepthArrayTexturesD32) never receives an MSAA resolve in this game.
                if (texture->format == RenderFormat::D32_FLOAT)
                {
                    static bool s_logged = false;
                    if (!s_logged)
                    {
                        s_logged = true;
                        fprintf(stderr, "Switch renderer: an MSAA depth resolve into a D32_FLOAT texture; turn "
                            "SwitchDepthTexturesD32 and SwitchDepthArrayTexturesD32 off.\n");
                    }
                }
#endif
            }
            else
            {
                auto& resolveMsaaColorPipeline = g_resolveMsaaColorPipelines[surface->format][pipelineIndex];
                if (resolveMsaaColorPipeline == nullptr)
                {
                    RenderGraphicsPipelineDesc desc;
                    desc.pipelineLayout = g_pipelineLayout.get();
                    desc.vertexShader = g_copyShader.get();
                    desc.pixelShader = g_resolveMsaaColorShaders[pipelineIndex].get();
                    desc.renderTargetFormat[0] = texture->format;
                    desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
                    desc.renderTargetCount = 1;
                    resolveMsaaColorPipeline = g_device->createGraphicsPipeline(desc);
                }

                pipeline = resolveMsaaColorPipeline.get();
            }
        }
        else
        {
            if (isDepthStencil)
            {
                pipeline = g_copyDepthPipeline.get();
#if defined(__SWITCH__)
                // SwitchDepthArrayTexturesD32: the depth array textures are D32_FLOAT (CreateTexture).
                if (texture->format == RenderFormat::D32_FLOAT)
                    pipeline = g_copyDepthPipelineD32.get();
#endif
            }
            else
            {
                auto& copyColorPipeline = g_copyColorPipelines[texture->format];
                if (copyColorPipeline == nullptr)
                {
                    RenderGraphicsPipelineDesc desc;
                    desc.pipelineLayout = g_pipelineLayout.get();
                    desc.vertexShader = g_copyShader.get();
                    desc.pixelShader = g_copyColorShader.get();
                    desc.renderTargetFormat[0] = texture->format;
                    desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
                    desc.renderTargetCount = 1;
                    copyColorPipeline = g_device->createGraphicsPipeline(desc);
                }

                pipeline = copyColorPipeline.get();
            }
        }

        auto& framebuffer = texture->framebuffers[slice];
        if (framebuffer == nullptr)
        {
            if (isDepthStencil)
            {
                RenderTextureViewDesc viewDesc;
                viewDesc.format = texture->format;
                viewDesc.dimension = texture->viewDimension;
                viewDesc.mipLevels = texture->mipLevels;
                viewDesc.arrayIndex = slice;
                viewDesc.arraySize = 1;
                auto& view = texture->framebufferViews.emplace_back(texture->texture->createTextureView(viewDesc));

                RenderFramebufferDesc desc;
                desc.depthAttachmentView = view.get();
                framebuffer = g_device->createFramebuffer(desc);
            }
            else
            {
                RenderFramebufferDesc desc;
                desc.colorAttachments = const_cast<const RenderTexture**>(&texture->texture);
                desc.colorAttachmentsCount = 1;
                framebuffer = g_device->createFramebuffer(desc);
            }
        }

        if (g_framebuffer != framebuffer.get())
        {
            commandList->setFramebuffer(framebuffer.get());
            g_framebuffer = framebuffer.get();
#if defined(__SWITCH__)
            g_framebufferBindCount++;
            g_resolveStats.framebufferChanges++;
#endif
        }

        commandList->setPipeline(pipeline);
        commandList->setViewports(RenderViewport(0.0f, 0.0f, float(texture->width), float(texture->height), 0.0f, 1.0f));
        commandList->setScissors(RenderRect(0, 0, texture->width, texture->height));
        commandList->setGraphicsPushConstants(0, &surface->descriptorIndex, 0, sizeof(uint32_t));
        commandList->drawInstanced(CopyTriangleVertexCount(), 1, 0, 0);
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
        InvalidatePushedRootAddresses();
#endif

        g_dirtyStates.renderTargetAndDepthStencil = true;
        g_dirtyStates.viewport = true;
        g_dirtyStates.pipelineState = true;
        g_dirtyStates.scissorRect = true;

        if (g_backend != Backend::D3D12)
        {
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
            // [Switch] SwitchCopyKeepsVertexConstants: the push above only overwrote the vertex constant
            // pointer. With the uniform buffers and lazy pushes, the block uploaded last is still bound
            // in set 5 (the copy pipeline has the same layout and binds no set), still intact in this
            // frame's upload ring, and its pointer is pushed again (from g_pendingRootAddresses) before
            // the next draw whose shaders read pointers: uploading it again changed nothing.
            if (!(g_constantsUbo && g_switchRenderer.copyKeepsVertexConstants))
#endif
            g_dirtyStates.vertexShaderConstants = true; // The push constant call invalidates vertex shader constants.
            g_dirtyStates.depthBias = true; // Static depth bias in copy pipeline invalidates dynamic depth bias.
        }
    }

    texture->sourceSurface = nullptr;

    // Check if any texture slots had this texture assigned, and make it point back at the original texture.
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (g_textures[i] == texture)
            SetTextureInRenderThread(i, texture);
    }
}

static void ExecutePendingStretchRectCommands(GuestSurface* renderTarget, GuestSurface* depthStencil)
{
    for (const auto surface : { renderTarget, depthStencil })
    {
        if (surface != nullptr && !surface->destinationTextures.empty())
        {
            for (const auto [texture, slice] : surface->destinationTextures)
                ExecutePendingCopy(surface, texture, slice);

            surface->destinationTextures.clear();
        }
    }
}

#if defined(__SWITCH__)
static void IssueDeferredClear();

// [Switch] Copies the port had already made (SwitchLazyResolves, SwitchSkipNoOpDraws, SwitchReadOnlyDepthSampling,
// SwitchSkipRestoreDraws). These leave pending a copy the port made at that point: at a draw that binds the surface
// without changing what the copy reads (no depth written into a depth surface: the copy reads the depth aspect alone,
// so stencil writes do not count; or a draw that is not sent at all), or at a clear that leaves the surface alone. The
// texture is "owed" its copy. The surface does not change before the copy is made (every change of it makes its
// pending copies first, as before), so the copy made later is the same; and it is made before anything could see the
// difference: before a slot is given the texture (SetTexture; the port's slot then read the texture, through its own
// view, which may swizzle), before a CPU update of the texture, before another resolve into it or from its surface,
// and at Present (the port dropped pending depth copies there, not ones it had made). No slot holds the texture when
// its copy becomes owed (CanDeferCopies), but with SwitchReadOnlyDepthSampling and SwitchSkipRestoreDraws, whose slots
// are pointed at views of the surface that sample exactly what the texture holds after the copy. A surface's pending
// copies become owed together and are made together: they are all owed or none.
static std::vector<GuestTexture*> g_owedCopies; // Marked this frame (entries whose copy was made since are skipped).

// Surfaces the game released while textures still had copies pending from them (SwitchKeepResolvesPending & co, see
// ForgetPendingResolves): destroyed once the next Present has dealt with those copies.
static std::vector<GuestSurface*> g_releasedSurfaces;

static bool IsDepthSurface(const GuestSurface* surface)
{
    return RenderFormatIsDepth(surface->format);
}

static bool HasOwedCopies(const GuestSurface* surface)
{
    for (const auto [texture, slice] : surface->destinationTextures)
    {
        if (texture->copyOwed)
            return true;
    }

    return false;
}

static void MarkCopiesOwed(GuestSurface* surface)
{
    for (const auto [texture, slice] : surface->destinationTextures)
    {
        if (!texture->copyOwed)
        {
            texture->copyOwed = true;
            g_owedCopies.push_back(texture);
            g_resolveStats.owed++;
        }
    }
}

// Whether the pending copies of `surface` may wait: nothing samples them now (no slot holds one of its textures, and
// it is not among the surfaces whose array or MSAA copies the next draw makes, g_pendingResolves), so no slot needs
// pointing at the texture as the copy would have done.
static bool CanDeferCopies(const GuestSurface* surface)
{
    if (surface->sampleCount != RenderSampleCount::COUNT_1 || g_pendingResolves.find(const_cast<GuestSurface*>(surface)) != g_pendingResolves.end())
        return false;

    for (const auto [texture, slice] : surface->destinationTextures)
    {
        for (uint32_t i = 0; i < std::size(g_textures); i++)
        {
            if (g_textures[i] == texture)
                return false;
        }
    }

    return true;
}

// The given pending copies of `surface`, in one barrier batch, as ExecutePendingStretchRectCommands makes them all.
static void ExecutePendingCopies(GuestSurface* surface, const std::vector<GuestTexture*>& textures)
{
    if (textures.empty())
        return;

    const bool multiSampling = surface->sampleCount != RenderSampleCount::COUNT_1;
    const bool shaderResolve = !multiSampling || (IsDepthSurface(surface) && !g_capabilities.resolveModes);
    AddBarrier(surface, shaderResolve ? RenderTextureLayout::SHADER_READ : RenderTextureLayout::RESOLVE_SOURCE);
    for (GuestTexture* texture : textures)
    {
        AddBarrier(texture, !shaderResolve ? RenderTextureLayout::RESOLVE_DEST :
            IsDepthSurface(surface) ? RenderTextureLayout::DEPTH_WRITE : RenderTextureLayout::COLOR_WRITE);
    }

    FlushBarriers();

    for (GuestTexture* texture : textures)
    {
        auto findResult = surface->destinationTextures.find(texture);
        if (findResult == surface->destinationTextures.end())
            continue;

        ExecutePendingCopy(surface, texture, findResult->second);
        surface->destinationTextures.erase(findResult);
    }
}

// Makes the owed copies of `surface`, all of them (see g_owedCopies). The caller may run while a colour clear waits
// (SwitchSkipOverwrittenClears): that clear is made first, as it was made before these copies' own time had come.
static void MakeOwedCopies(GuestSurface* surface, uint32_t trigger)
{
    static std::vector<GuestTexture*> s_textures;
    s_textures.clear();

    for (const auto [texture, slice] : surface->destinationTextures)
    {
        if (texture->copyOwed)
            s_textures.push_back(texture);
    }

    if (s_textures.empty())
        return;

    IssueDeferredClear();
    g_resolveCopyTrigger = trigger;
    ExecutePendingCopies(surface, s_textures);
}

// A copy of `texture` the port had already made (owed, or kept over a Present), made now: before its slot is given it,
// before a CPU update of it or a new resolve into it. Its surface has not changed since.
static void MakeCopyThePortMade(GuestTexture* texture, uint32_t trigger)
{
    GuestSurface* surface = texture->sourceSurface;
    if (surface == nullptr)
        return;

    if (texture->copyOwed)
    {
        MakeOwedCopies(surface, trigger);
    }
    else if (texture->pendingCarried)
    {
        static std::vector<GuestTexture*> s_textures;
        s_textures.assign(1, texture);

        IssueDeferredClear();
        g_resolveCopyTrigger = trigger;
        ExecutePendingCopies(surface, s_textures);
    }
}

// Whether `texture`, pending on `surface`, reads through the surface's own descriptor exactly as it would read its own
// after the copy: the same image kind (a 2D texture of one level, handOverCapable), format and size, a view of the
// default component mapping (the port's L8, X8R8G8B8 and depth textures swizzle), no replacement for the PS3 button
// icons, and a copy that fills the whole texture (slice 0).
static bool SurfaceSamplesLikeTexture(const GuestSurface* surface, const GuestTexture* texture, uint32_t slice)
{
    const auto& mapping = texture->viewDesc.componentMapping;
    const bool identityMapping = mapping.r == RenderSwizzle::IDENTITY && mapping.g == RenderSwizzle::IDENTITY &&
        mapping.b == RenderSwizzle::IDENTITY && mapping.a == RenderSwizzle::IDENTITY;

    return texture->handOverCapable && slice == 0 && identityMapping && texture->patchedTexture == nullptr &&
        texture->format == surface->format && texture->width == surface->width && texture->height == surface->height;
}

// [Switch] SwitchKeepResolvesPending. A colour resolve still pending at the end of the frame stays pending instead of
// being copied then. The texture keeps reading the surface's image, which holds what the copy would have written until
// the surface changes (a draw into it, a clear, which may hand the image over instead, or its release), and those make
// the copy first; so do a CPU update of the texture and another resolve into it. Only when every pending texture of the
// surface reads the surface's descriptor exactly as its own (SurfaceSamplesLikeTexture: after Present the port's slots
// read the copy through the texture's descriptor); not for the back buffer (its image changes every frame), released,
// multisampled or depth surfaces (the port drops the depth copies still pending at Present, it does not make them).
static bool CanKeepResolvePending(const GuestSurface* surface)
{
    if (!g_switchRenderer.keepResolvesPending || surface == g_backBuffer || surface->textureHolder == nullptr ||
        surface->sampleCount != RenderSampleCount::COUNT_1 || IsDepthSurface(surface) || surface->released)
    {
        if (g_switchRenderer.keepResolvesPending)
        {
            uint32_t reason = KEEP_REFUSED_RELEASED;
            if (surface == g_backBuffer || surface->textureHolder == nullptr)
                reason = KEEP_REFUSED_BACK_BUFFER;
            else if (surface->sampleCount != RenderSampleCount::COUNT_1)
                reason = KEEP_REFUSED_MSAA;
            else if (IsDepthSurface(surface))
                reason = KEEP_REFUSED_DEPTH;
            g_resolveStats.keepRefused[reason]++;
        }
        return false;
    }

    for (const auto [texture, slice] : surface->destinationTextures)
    {
        if (!SurfaceSamplesLikeTexture(surface, texture, slice))
        {
            const auto& mapping = texture->viewDesc.componentMapping;
            uint32_t reason = KEEP_REFUSED_SIZE;
            if (!texture->handOverCapable || slice != 0 || texture->patchedTexture != nullptr)
                reason = KEEP_REFUSED_TEXTURE;
            else if (mapping.r != RenderSwizzle::IDENTITY || mapping.g != RenderSwizzle::IDENTITY ||
                mapping.b != RenderSwizzle::IDENTITY || mapping.a != RenderSwizzle::IDENTITY)
                reason = KEEP_REFUSED_SWIZZLE;
            else if (texture->format != surface->format)
                reason = KEEP_REFUSED_FORMAT;
            g_resolveStats.keepRefused[reason]++;
            return false;
        }
    }

    return true;
}

// A slot given a texture whose copy was kept pending over Present (or one of a skipped restore): what
// SetTextureInRenderThread sets for the texture after its copy (it has a 2D view), with the surface's descriptor, which
// reads the same texels the same way (SurfaceSamplesLikeTexture).
static void SetSurfaceAsTexture(uint32_t index, GuestSurface* surface)
{
    AddBarrier(surface, RenderTextureLayout::SHADER_READ);

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DIndices[index], surface->descriptorIndex);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DArrayIndices[index], surface->descriptorIndex);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.textureCubeIndices[index], uint32_t(TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE));

#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
    UpdateTextureSlotTables(index);
#endif
}

// The options that leave copies pending past the point where the port made or dropped them. With none of them on,
// the pending copies are handled exactly as before (ProcExecutePendingStretchRectCommands, ProcDestructResource).
static bool UsesLongerPendingCopies()
{
    return g_switchRenderer.keepResolvesPending || g_switchRenderer.lazyResolves || g_switchRenderer.skipNoOpDraws ||
        g_switchRenderer.readOnlyDepthSampling || g_switchRenderer.skipRestoreDraws;
}

// Present with UsesLongerPendingCopies: the same outcome as ProcExecutePendingStretchRectCommands. The port copied every
// colour resolve still pending and, when there was one, dropped the pending depth ones of this frame's resolves;
// copies it had made already (owed ones, and the ones kept pending over an earlier Present) were not pending then.
// Colour copies stay pending where CanKeepResolvePending allows, and are made otherwise; owed depth copies are made.
static void ExecutePendingCopiesAtPresent()
{
    g_resolveCopyTrigger = RESOLVE_COPY_AT_PRESENT;

    bool foundAny = false;
    for (const auto surface : g_pendingSurfaceCopies)
    {
        if (!IsDepthSurface(surface))
        {
            for (const auto [texture, slice] : surface->destinationTextures)
            {
                if (!texture->copyOwed && !texture->pendingCarried)
                {
                    foundAny = true;
                    break;
                }
            }
        }
    }

    // This frame's resolves, the surfaces with owed copies and the released ones.
    static ankerl::unordered_dense::set<GuestSurface*> s_surfaces;
    s_surfaces.clear();

    for (const auto surface : g_pendingSurfaceCopies)
        s_surfaces.emplace(surface);

    for (GuestTexture* texture : g_owedCopies)
    {
        if (texture->copyOwed && texture->sourceSurface != nullptr)
            s_surfaces.emplace(texture->sourceSurface);
    }

    for (GuestSurface* surface : g_releasedSurfaces)
        s_surfaces.emplace(surface);

    // Released textures (ForgetPendingResolves), counted above as the port counted them: nothing reads them any more
    // (the slots are cleared for the next frame), so their copies are dropped rather than made or kept.
    for (const auto surface : s_surfaces)
    {
        for (auto it = surface->destinationTextures.begin(); it != surface->destinationTextures.end();)
        {
            GuestTexture* texture = it->first;
            if (texture->released)
            {
                texture->sourceSurface = nullptr;
                texture->copyOwed = false;
                texture->pendingCarried = false;
                it = surface->destinationTextures.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // The copies to make now: every one of a colour surface that cannot keep them, the owed ones of depth surfaces.
    static std::vector<std::pair<GuestSurface*, std::vector<GuestTexture*>>> s_copies;
    s_copies.clear();

    for (const auto surface : s_surfaces)
    {
        if (surface->destinationTextures.empty())
            continue;

        if (CanKeepResolvePending(surface))
        {
            for (const auto [texture, slice] : surface->destinationTextures)
            {
                texture->copyOwed = false;
                texture->pendingCarried = true;
                g_resolveStats.kept++;
            }

            continue;
        }

        std::vector<GuestTexture*> textures;
        for (const auto [texture, slice] : surface->destinationTextures)
        {
            if (!IsDepthSurface(surface) || texture->copyOwed)
                textures.push_back(texture);
        }

        if (!textures.empty())
            s_copies.emplace_back(surface, std::move(textures));
    }

    for (auto& [surface, textures] : s_copies)
    {
        const bool multiSampling = surface->sampleCount != RenderSampleCount::COUNT_1;
        const bool shaderResolve = !multiSampling || (IsDepthSurface(surface) && !g_capabilities.resolveModes);
        AddBarrier(surface, shaderResolve ? RenderTextureLayout::SHADER_READ : RenderTextureLayout::RESOLVE_SOURCE);
        for (GuestTexture* texture : textures)
        {
            AddBarrier(texture, !shaderResolve ? RenderTextureLayout::RESOLVE_DEST :
                IsDepthSurface(surface) ? RenderTextureLayout::DEPTH_WRITE : RenderTextureLayout::COLOR_WRITE);
        }
    }

    FlushBarriers();

    for (auto& [surface, textures] : s_copies)
    {
        for (GuestTexture* texture : textures)
        {
            auto findResult = surface->destinationTextures.find(texture);
            if (findResult != surface->destinationTextures.end())
            {
                ExecutePendingCopy(surface, texture, findResult->second);
                surface->destinationTextures.erase(findResult);
            }
        }
    }

    // The port's rule for the depth copies of this frame's resolves still pending: dropped when a colour one was
    // pending, kept otherwise.
    if (foundAny)
    {
        for (const auto surface : g_pendingSurfaceCopies)
        {
            if (IsDepthSurface(surface))
            {
                g_resolveStats.depthDropped += surface->destinationTextures.size();

                for (const auto [texture, slice] : surface->destinationTextures)
                    texture->sourceSurface = nullptr;

                surface->destinationTextures.clear();
            }
        }
    }

    // Released surfaces are destroyed when this frame slot comes around again, as the port destroyed them. Depth copies
    // the port kept pending stay so until then (DestructTempResources).
    for (GuestSurface* surface : g_releasedSurfaces)
        g_tempResources[g_frame].push_back(surface);

    g_releasedSurfaces.clear();
    g_owedCopies.clear();
    g_pendingSurfaceCopies.clear();
    g_pendingResolves.clear();
}

// Render thread, when the game releases a resource (ProcDestructResource), with UsesLongerPendingCopies: no texture
// stays linked to a surface past its destruction. A released texture leaves its surface's pending copies (the game
// never reads it again, and nothing else does), at the next Present if the port counted it there (see below). A
// released surface whose textures still have copies pending from it
// stays alive until the next Present has made or dropped them as the port would have (the port made or dropped every
// colour one there, before or after the release; CanKeepResolvePending excludes released surfaces), and is destroyed
// with the resources released in that frame. Returns true when it keeps the resource.
static bool ForgetPendingResolves(GuestResource* resource)
{
    if (!UsesLongerPendingCopies())
        return false;

    switch (resource->type)
    {
    case ResourceType::Texture:
    case ResourceType::VolumeTexture:
    case ResourceType::ArrayTexture:
    {
        auto texture = reinterpret_cast<GuestTexture*>(resource);
        texture->released = true;

        // A copy the port had already made (owed, or kept over a Present) of a texture a slot still holds: the slot
        // reads the surface (SetSurfaceAsTexture), and once the texture leaves the surface's list nothing points it
        // back at the texture before the surface changes. The port's slot read the texture's own copy until the frame
        // ended, so the copy is made now, which points the slot at the texture (ExecutePendingCopy). Only while the
        // frame is recorded: after it, nothing samples the slots before they are reset.
        if (texture->sourceSurface != nullptr && (texture->copyOwed || texture->pendingCarried) && g_commandListRecording)
        {
            for (uint32_t i = 0; i < std::size(g_textures); i++)
            {
                if (g_textures[i] == texture)
                {
                    MakeCopyThePortMade(texture, RESOLVE_COPY_OTHER);
                    break;
                }
            }
        }

        // A colour copy the port still had pending (not owed, not kept over a Present) stays listed until the next
        // Present: the port dropped the depth copies there only if a colour one was pending, and this one was. It is
        // made (as by the port) or dropped there; the texture is destroyed later (the frame slot's next turn).
        if (texture->sourceSurface != nullptr && !IsDepthSurface(texture->sourceSurface) && !texture->copyOwed &&
            !texture->pendingCarried)
        {
            return false;
        }

        if (texture->sourceSurface != nullptr)
        {
            texture->sourceSurface->destinationTextures.erase(texture);
            texture->sourceSurface = nullptr;
        }

        texture->copyOwed = false;
        texture->pendingCarried = false;
        return false;
    }

    case ResourceType::RenderTarget:
    case ResourceType::DepthStencil:
    {
        auto surface = reinterpret_cast<GuestSurface*>(resource);
        if (surface->released)
            return true; // Already waiting for the next Present.

        if (surface->destinationTextures.empty())
            return false;

        surface->released = true;
        g_releasedSurfaces.push_back(surface);
        return true;
    }

    default:
        return false;
    }
}
#endif

static void ProcExecutePendingStretchRectCommands(const RenderCommand& cmd)
{
#if defined(__SWITCH__)
    // SwitchCascadeAdoption: the cascade sequence ends with the frame, before the port's rules for pending copies run.
    CascadeAtPresent();

    if (UsesLongerPendingCopies())
    {
        ExecutePendingCopiesAtPresent();
        return;
    }

    g_resolveCopyTrigger = RESOLVE_COPY_AT_PRESENT;
#endif

    bool foundAny = false;

    for (const auto surface : g_pendingSurfaceCopies)
    {
        // Depth stencil textures in this game are guaranteed to be transient.
        if (!RenderFormatIsDepth(surface->format))
            foundAny |= PopulateBarriersForStretchRect(surface, nullptr);
    }

    if (foundAny)
    {
        FlushBarriers();

        for (const auto surface : g_pendingSurfaceCopies)
        {
            if (!RenderFormatIsDepth(surface->format))
                ExecutePendingStretchRectCommands(surface, nullptr);
#if defined(__SWITCH__)
            else
                g_resolveStats.depthDropped += surface->destinationTextures.size();
#endif

            for (const auto [texture, _] : surface->destinationTextures)
                texture->sourceSurface = nullptr;

            surface->destinationTextures.clear();
        }
    }

    g_pendingSurfaceCopies.clear();
    g_pendingResolves.clear();
}

#if defined(__SWITCH__)
// SwitchReadOnlyDepthSampling: the depth surface of the framebuffer SetFramebuffer binds for a draw is attached
// read-only (set by FlushRenderStateForRenderThread for that call only), and whether the bound one is.
static bool g_framebufferDepthReadOnly = false;
static bool g_boundFramebufferDepthReadOnly = false;
#endif

static void SetFramebuffer(GuestSurface* renderTarget, GuestSurface* depthStencil, bool settingForClear)
{
#if defined(__SWITCH__)
    // SwitchCascadeAdoption: the shadow surface draws into the cascade image's layer (never with a colour target: it
    // takes its own image back first, CascadeBeforeDraw).
    if (renderTarget == nullptr && CascadeActive(depthStencil))
    {
        if (settingForClear || g_dirtyStates.renderTargetAndDepthStencil || g_boundFramebufferDepthReadOnly)
        {
            CascadeBindLayer();
            g_dirtyStates.renderTargetAndDepthStencil = settingForClear;
        }

        return;
    }

    const bool depthReadOnly = g_framebufferDepthReadOnly && depthStencil != nullptr && !settingForClear;
    if (settingForClear || g_dirtyStates.renderTargetAndDepthStencil || depthReadOnly != g_boundFramebufferDepthReadOnly)
#else
    if (settingForClear || g_dirtyStates.renderTargetAndDepthStencil)
#endif
    {
        // printf("SetFramebuffer %x %x\n", renderTarget, depthStencil);
        GuestSurface* framebufferContainer = nullptr;
        RenderTexture* framebufferKey = nullptr;

        if (renderTarget != nullptr && depthStencil != nullptr)
        {
            framebufferContainer = depthStencil; // Backbuffer texture changes per frame so we can't use the depth stencil as the key.
            framebufferKey = renderTarget->texture;
        }
        else if (renderTarget != nullptr && depthStencil == nullptr)
        {
            framebufferContainer = renderTarget;
            framebufferKey = renderTarget->texture; // Backbuffer texture changes per frame so we can't assume nullptr for it.
        }
        else if (renderTarget == nullptr && depthStencil != nullptr)
        {
            framebufferContainer = depthStencil;
            framebufferKey = nullptr;
        }

        auto& commandList = g_commandLists[g_frame];

        if (framebufferContainer != nullptr)
        {
#if defined(__SWITCH__)
            auto& framebuffers = depthReadOnly ? framebufferContainer->readOnlyFramebuffers : framebufferContainer->framebuffers;
            auto& framebuffer = framebuffers[framebufferKey];
#else
            auto& framebuffer = framebufferContainer->framebuffers[framebufferKey];
#endif

            if (framebuffer == nullptr)
            {
                RenderFramebufferDesc desc;

                if (renderTarget != nullptr)
                {
                    desc.colorAttachments = const_cast<const RenderTexture**>(&renderTarget->texture);
                    desc.colorAttachmentsCount = 1;
                }

                if (depthStencil != nullptr)
                    desc.depthAttachment = depthStencil->texture;

#if defined(__SWITCH__)
                // Loaded and stored (not plume's NONE operations): NVK keeps hierarchical Z for the pass (SwitchZcull),
                // and nothing writes the attachment, so the store writes back what was loaded.
                desc.depthAttachmentReadOnly = depthReadOnly;
                desc.depthAttachmentReadOnlyLoadStore = depthReadOnly;

                // Its caches are keyed by colour image: ForgetFramebuffersOf looks here when an image is destroyed.
                g_liveSurfaces.emplace(framebufferContainer);
#endif

                framebuffer = g_device->createFramebuffer(desc);
            }

            if (g_framebuffer != framebuffer.get())
            {
#if defined(__SWITCH__)
                PassProfilerFramebuffer(renderTarget, depthStencil);
                FrameLogPass(renderTarget, depthStencil, depthReadOnly ? "depth read-only" : nullptr);
#endif
                commandList->setFramebuffer(framebuffer.get());
                g_framebuffer = framebuffer.get();
#if defined(__SWITCH__)
                g_framebufferBindCount++;
                g_resolveStats.framebufferChanges++;
#endif
            }
        }
        else if (g_framebuffer != nullptr)
        {
            commandList->setFramebuffer(nullptr);
            g_framebuffer = nullptr;
#if defined(__SWITCH__)
            g_framebufferBindCount++;
#endif
        }

        if (g_framebuffer != nullptr)
        {
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetX, 1.0f / float(g_framebuffer->getWidth()));
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetY, -1.0f / float(g_framebuffer->getHeight()));
        }

        g_dirtyStates.renderTargetAndDepthStencil = settingForClear;
#if defined(__SWITCH__)
        g_boundFramebufferDepthReadOnly = depthReadOnly;
        g_boundTargets.framebuffer = g_framebuffer;
        g_boundTargets.renderTarget = renderTarget;
        g_boundTargets.depthStencil = depthStencil;
        g_boundTargets.bindCount = g_framebufferBindCount;
#endif
    }
}

static void Clear(GuestDevice* device, uint32_t flags, uint32_t, be<float>* color, double z, uint32_t stencil)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::Clear;
    cmd.clear.flags = flags;
    cmd.clear.color[0] = color[0];
    cmd.clear.color[1] = color[1];
    cmd.clear.color[2] = color[2];
    cmd.clear.color[3] = color[3];
    cmd.clear.z = float(z);
    cmd.clear.stencil = stencil;
    EnqueueRenderCommand(cmd);
}

#if defined(__SWITCH__)
// [Switch] SwitchResolveHandOver. A surface resolved into exactly one texture is about to be cleared completely: instead
// of copying the surface into the texture and clearing the surface, the texture takes the surface's image (which holds
// exactly what the copy would have written) and the surface takes the texture's old image, which the clear then
// overwrites entirely: its colour, or its depth and its stencil (D32_FLOAT_S8_UINT: a depth surface is only handed
// over by a clear of both, or one whose stencil SwitchUniformStencilClears proves unchanged, which then clears it to
// that value). Only when the images were created alike (same format, size, one level, same kind of target, one
// sample): every later read and write sees the same texels as with the copy. The texture is sampled through a view made
// from its own view description (component mapping included), as after the copy; its stencil is never sampled (depth
// views are depth-aspect). Both get new descriptors; the old views, descriptors and framebuffers stay alive until the
// commands of the frame, which may use them, are done.
static bool CanHandOver(const GuestSurface* surface, const GuestTexture* texture, uint32_t slice)
{
    return surface != g_backBuffer && surface->textureHolder != nullptr && surface->sampleCount == RenderSampleCount::COUNT_1 &&
        texture->handOverCapable && texture->textureHolder != nullptr && texture->patchedTexture == nullptr && slice == 0 &&
        texture->width == surface->width && texture->height == surface->height && texture->format == surface->format;
}

static void RetireFramebuffer(std::unique_ptr<RenderFramebuffer>& framebuffer)
{
    if (framebuffer == nullptr)
        return;

    if (g_framebuffer == framebuffer.get())
        g_framebuffer = nullptr;

    g_handOverFramebuffers[g_frame].push_back(std::move(framebuffer));
}

static void SanitizePipelineState(PipelineState& pipelineState);
static void RequestPipelineVariant(const PipelineState& sanitizedState, XXH64_hash_t hash);

// The swap itself. Returns the surface's previous descriptor (a plain view of the image the texture now holds, valid
// until the frame's commands are done), or 0 when the descriptor heap is full (nothing changed then).
static uint32_t HandOverSurfaceImage(GuestSurface* surface, GuestTexture* texture)
{
    const uint32_t textureDescriptor = g_textureDescriptorAllocator.allocate();
    const uint32_t surfaceDescriptor = g_textureDescriptorAllocator.allocate();
    if (textureDescriptor < TEXTURE_DESCRIPTOR_NULL_COUNT || surfaceDescriptor < TEXTURE_DESCRIPTOR_NULL_COUNT)
    {
        g_textureDescriptorAllocator.free(textureDescriptor);
        g_textureDescriptorAllocator.free(surfaceDescriptor);
        return 0;
    }

    // Barriers still pending are keyed by image and stay right across the swap (each object's layout moves with its
    // image), so they go out with the caller's batch.
    const uint32_t oldSurfaceDescriptor = surface->descriptorIndex;
    const uint32_t frame = g_frame;

    g_handOverViews[frame].push_back(std::move(surface->textureView));
    g_handOverViews[frame].push_back(std::move(texture->textureView));
    g_handOverDescriptors[frame].push_back(surface->descriptorIndex);
    g_handOverDescriptors[frame].push_back(texture->descriptorIndex);

    // The texture's copy framebuffers (and their depth views) name its old image.
    for (auto& [slice, framebuffer] : texture->framebuffers)
        RetireFramebuffer(framebuffer);

    texture->framebuffers.clear();

    for (auto& view : texture->framebufferViews)
        g_handOverViews[frame].push_back(std::move(view));

    texture->framebufferViews.clear();

    // A depth surface's cached framebuffers all name its image as their depth attachment, and its views for read-only
    // sampling are of that image. (A colour surface's are keyed by its image, so they are not found for the new one.)
    if (IsDepthSurface(surface))
    {
        for (auto* cache : { &surface->framebuffers, &surface->readOnlyFramebuffers })
        {
            for (auto& [key, framebuffer] : *cache)
                RetireFramebuffer(framebuffer);

            cache->clear();
        }

        for (auto& depthReadView : surface->depthReadViews)
        {
            g_handOverViews[frame].push_back(std::move(depthReadView.view));
            g_handOverDescriptors[frame].push_back(depthReadView.descriptorIndex);
        }

        surface->depthReadViews.clear();
    }

    std::swap(surface->textureHolder, texture->textureHolder);
    std::swap(surface->texture, texture->texture);
    std::swap(surface->layout, texture->layout);
    texture->hadSurfaceImage = true;
    surface->stencilKnown = false;

    // A colour surface's framebuffer bound now names the image the texture holds from here on: never keep it
    // (SwitchStableFramebuffers). The next draw binds the surface's own again (renderTargetAndDepthStencil below).
    g_framebufferBindCount++;

    texture->textureView = texture->texture->createTextureView(texture->viewDesc);
    texture->descriptorIndex = textureDescriptor;
    SetTextureDescriptor(texture->descriptorIndex, texture->texture, texture->width, texture->height, RenderTextureLayout::SHADER_READ,
        texture->textureView.get(), texture->mipLevels);

    RenderTextureViewDesc surfaceViewDesc;
    surfaceViewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    surfaceViewDesc.format = surface->format;
    surfaceViewDesc.mipLevels = 1;
    surface->textureView = surface->texture->createTextureView(surfaceViewDesc);
    surface->descriptorIndex = surfaceDescriptor;
    SetTextureDescriptor(surface->descriptorIndex, surface->texture, surface->width, surface->height, RenderTextureLayout::SHADER_READ,
        surface->textureView.get(), 1);

    texture->sourceSurface = nullptr;
    texture->copyOwed = false;
    texture->pendingCarried = false;
    surface->destinationTextures.erase(texture);
    FrameLogHandOver(surface, texture, g_resolveCopyTrigger);

    // Slots that read the texture through the surface now read the texture itself.
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (g_textures[i] == texture)
            SetTextureInRenderThread(i, texture);
    }

    g_dirtyStates.renderTargetAndDepthStencil = true;
    return oldSurfaceDescriptor;
}

static bool TryResolveHandOver(GuestSurface* surface)
{
    if (surface->destinationTextures.size() != 1)
        return false;

    const auto [texture, slice] = *surface->destinationTextures.begin();
    if (!CanHandOver(surface, texture, slice) || HandOverSurfaceImage(surface, texture) == 0)
        return false;

    g_resolveStats.handOversAtClear++;
    return true;
}

// [Switch] SwitchCascadeAdoption (perf7). The game draws its shadow cascades one after another into one depth surface S
// (1024x1024 after the cap) and resolves each into one slice of the cascaded shadow map, a depth array texture A: the
// port copied S into A's slice at the next clear of S, and the last one before the main pass's first draw: four
// full-screen depth copies a frame, 1.15 ms each in the perf6 GPU-limited segment (4.6 ms of its 25-31 ms GPU frame),
// each behind a wait for idle. Here S draws straight into a layer of a second image R made like A (same size, format
// and layers; no ZCULL, which NVK keeps per image, not per layer): the clear of S that starts a cascade clears R's next
// layer, the cascade's draws draw into it, and its resolve into the slice of that index only records that R's layer
// holds A's slice. When A is next needed (bound to a slot, updated or released, or a new sequence starts), A and R swap
// images as a hand-over does, and A holds exactly what the copies would have written (copy_depth is an identity on
// stored depth). Slices not drawn this time (three cascades at the CPU-limited spot) are first copied from A's old image
// into R, one copy instead of three; when fewer than half were drawn, the drawn layers are copied into A instead. Any
// other use of S takes its content back into its own image first (CascadeMaterializeSurface): a draw with a colour
// target or, when R has no stencil, with the stencil test or writes, a resolve of S anywhere else, a partial clear,
// Present with the cascade's resolve dropped as the port dropped it. S's own stencil then gets the value the cascade
// clears gave it (no draw changed it meanwhile). The cascades' draws run unchanged: the same state, viewport and clear
// values on a float32 depth either way (D32_FLOAT, or D32_FLOAT_S8_UINT without SwitchDepthArrayTexturesD32), so the
// same depth tests and values; their pipelines take R's depth format.
struct CascadeLayerTarget
{
    std::unique_ptr<RenderTextureView> view;
    std::unique_ptr<RenderFramebuffer> framebuffer;
};

struct CascadeAdoption
{
    GuestSurface* surface = nullptr;                        // S
    GuestTexture* array = nullptr;                          // A
    GuestBaseTexture image{ ResourceType::ArrayTexture };   // R: its holder, image, layout, size and format (A's)
    std::vector<CascadeLayerTarget> layers;                 // per layer of R: a depth attachment view and framebuffer
    uint32_t layerCount = 0;
    int32_t layer = -1;                                     // R's layer S draws into, -1 when S draws into its own image
    bool layerResolved = false;                             // that layer was resolved into A's slice of its index since
    uint32_t nextLayer = 0;                                 // the layer the next cascade starts in (0 at each Present)
    uint32_t held = 0;                                      // A's slices whose next content is R's layer of their index
    GuestBaseTexture* detachedHolder = nullptr;             // S's content is a layer of this image (R's or, after a swap,
    int32_t detachedLayer = -1;                             // A's), its own image stale until it is taken back
    uint32_t mismatches = 0;                                // cascades resolved into another slice than drawn for
    bool failed = false;                                    // R could not be made, or mismatches: no more cascades
    // perf8: R's layers known to hold what A's slice of their index holds (filled in, or A's previous image after a swap,
    // and written on neither side since), which an adoption does not fill in again.
    uint32_t valid = 0;
    // perf8: per layer L, the slice of A that S was resolved into after a cascade resolved from L kept drawing without a
    // clear (learned on the first such frame, -1 until then), and the layer such a continuation is being learned from or
    // continued from.
    int8_t continueTarget[32] = {};
    int32_t learnFrom = -1;
    int32_t continuedFrom = -1;
};

static CascadeAdoption g_cascade;

static bool CascadeActive(const GuestSurface* surface)
{
    return surface != nullptr && surface == g_cascade.surface && g_cascade.layer >= 0;
}

static bool CascadeImageHasStencil()
{
    return g_cascade.image.format == RenderFormat::D32_FLOAT_S8_UINT;
}

// The depth format the pipelines of draws on `surface` are made for: R's while S draws into it.
static RenderFormat CascadeDepthFormat(const GuestSurface* surface)
{
    return CascadeActive(surface) ? g_cascade.image.format : surface->format;
}

static void CascadeFormatChanged()
{
    if (g_depthStencil != nullptr && g_depthStencil == g_cascade.surface)
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthStencilFormat, CascadeDepthFormat(g_depthStencil));

    g_dirtyStates.renderTargetAndDepthStencil = true;
}

// A 2D view of one layer of an image for copy_depth to read, alive until the frame's commands are done. 0 when the
// descriptor heap is full.
static uint32_t CascadeLayerSource(const GuestBaseTexture* holder, uint32_t layer)
{
    const uint32_t descriptor = g_textureDescriptorAllocator.allocate();
    if (descriptor < TEXTURE_DESCRIPTOR_NULL_COUNT)
    {
        g_textureDescriptorAllocator.free(descriptor);
        return 0;
    }

    RenderTextureViewDesc viewDesc;
    viewDesc.format = holder->format;
    viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    viewDesc.mipLevels = 1;
    viewDesc.arrayIndex = layer;
    viewDesc.arraySize = 1;
    auto view = holder->texture->createTextureView(viewDesc);
    SetTextureDescriptor(descriptor, holder->texture, holder->width, holder->height, RenderTextureLayout::SHADER_READ, view.get(), 1);
    g_handOverViews[g_frame].push_back(std::move(view));
    g_handOverDescriptors[g_frame].push_back(descriptor);
    return descriptor;
}

// copy_depth of the view `source` into `framebuffer` (one depth attachment of the given format and size), with what
// ExecutePendingCopy's copy leaves to restore after it.
static void CascadeCopy(uint32_t source, RenderFramebuffer* framebuffer, RenderFormat format, uint32_t width, uint32_t height)
{
    auto& commandList = g_commandLists[g_frame];
    PassProfilerCopy(g_cascade.surface, g_cascade.array);
    if (g_framebuffer != framebuffer)
    {
        commandList->setFramebuffer(framebuffer);
        g_framebuffer = framebuffer;
        g_framebufferBindCount++;
        g_resolveStats.framebufferChanges++;
    }

    commandList->setPipeline(format == RenderFormat::D32_FLOAT ? g_copyDepthPipelineD32.get() : g_copyDepthPipeline.get());
    commandList->setViewports(RenderViewport(0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f));
    commandList->setScissors(RenderRect(0, 0, width, height));
    commandList->setGraphicsPushConstants(0, &source, 0, sizeof(uint32_t));
    commandList->drawInstanced(CopyTriangleVertexCount(), 1, 0, 0);
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    InvalidatePushedRootAddresses();
#endif

    g_dirtyStates.renderTargetAndDepthStencil = true;
    g_dirtyStates.viewport = true;
    g_dirtyStates.pipelineState = true;
    g_dirtyStates.scissorRect = true;
    if (g_backend != Backend::D3D12)
    {
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
        if (!(g_constantsUbo && g_switchRenderer.copyKeepsVertexConstants))
#endif
        g_dirtyStates.vertexShaderConstants = true;
        g_dirtyStates.depthBias = true;
    }
}

// perf8: one layer of an array image into a layer of another one (or of the same one, in the GENERAL layout) with the
// same format and size, as vkCmdCopyImage copies it (the same bytes), instead of a copy_depth draw. The layouts are the
// caller's; the copy ends the render pass, the next draw resumes the bound framebuffer.
static void CascadeTransferCopy(GuestBaseTexture* dst, uint32_t dstLayer, GuestBaseTexture* src, uint32_t srcLayer)
{
    PassProfilerCopy(g_cascade.surface, g_cascade.array);
    g_commandLists[g_frame]->copyTextureRegion(
        RenderTextureCopyLocation::Subresource(dst->texture, 0, dstLayer),
        RenderTextureCopyLocation::Subresource(src->texture, 0, srcLayer));
}

static RenderFramebuffer* CascadeLayerFramebuffer(uint32_t layer)
{
    auto& target = g_cascade.layers[layer];
    if (target.framebuffer == nullptr)
    {
        RenderTextureViewDesc viewDesc;
        viewDesc.format = g_cascade.image.format;
        viewDesc.dimension = g_cascade.array->viewDimension;
        viewDesc.mipLevels = 1;
        viewDesc.arrayIndex = layer;
        viewDesc.arraySize = 1;
        target.view = g_cascade.image.texture->createTextureView(viewDesc);

        RenderFramebufferDesc desc;
        desc.depthAttachmentView = target.view.get();
        target.framebuffer = g_device->createFramebuffer(desc);
    }

    return target.framebuffer.get();
}

static void CascadeRetireLayers()
{
    for (auto& target : g_cascade.layers)
    {
        RetireFramebuffer(target.framebuffer);
        if (target.view != nullptr)
            g_handOverViews[g_frame].push_back(std::move(target.view));
    }
}

// S's draws and clears bind R's layer: what SetFramebuffer sets for S's own depth-only framebuffer.
static void CascadeBindLayer()
{
    RenderFramebuffer* framebuffer = CascadeLayerFramebuffer(uint32_t(g_cascade.layer));
    if (g_framebuffer != framebuffer)
    {
        PassProfilerFramebuffer(nullptr, g_cascade.surface);
        FrameLogPass(nullptr, g_cascade.surface, "cascade adoption: into the cascade image's layer");
        g_commandLists[g_frame]->setFramebuffer(framebuffer);
        g_framebuffer = framebuffer;
        g_framebufferBindCount++;
        g_resolveStats.framebufferChanges++;
    }

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetX, 1.0f / float(g_framebuffer->getWidth()));
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetY, -1.0f / float(g_framebuffer->getHeight()));
    g_boundFramebufferDepthReadOnly = false;
    g_boundTargets.framebuffer = g_framebuffer;
    g_boundTargets.renderTarget = nullptr;
    g_boundTargets.depthStencil = g_cascade.surface;
    g_boundTargets.bindCount = g_framebufferBindCount;
}

// S's content (R's layer it draws into, or the layer it was left in) into S's own image, with its stencil set to the
// value the cascade clears gave it; S draws into its own image from here on.
static void CascadeMaterializeSurface()
{
    GuestSurface* surface = g_cascade.surface;
    GuestBaseTexture* holder = nullptr;
    uint32_t layer = 0;
    if (CascadeActive(surface))
    {
        holder = &g_cascade.image;
        layer = uint32_t(g_cascade.layer);
    }
    else if (g_cascade.detachedHolder != nullptr)
    {
        holder = g_cascade.detachedHolder;
        layer = uint32_t(g_cascade.detachedLayer);
    }
    else
    {
        return;
    }

    g_cascade.layer = -1;
    g_cascade.layerResolved = false;
    g_cascade.detachedHolder = nullptr;
    g_cascade.detachedLayer = -1;
    CascadeFormatChanged();

    const uint32_t source = CascadeLayerSource(holder, layer);
    AddBarrier(holder, RenderTextureLayout::SHADER_READ);
    AddBarrier(surface, RenderTextureLayout::DEPTH_WRITE);
    FlushBarriers();
    SetFramebuffer(nullptr, surface, true);
    if (source != 0)
    {
        CascadeCopy(source, g_framebuffer, surface->format, surface->width, surface->height);
    }
    else
    {
        static bool s_logged = false;
        if (!s_logged)
        {
            s_logged = true;
            fprintf(stderr, "Switch renderer: cascade adoption: no texture descriptor left to restore the shadow surface.\n");
        }
    }

    if (surface->stencilKnown && surface->format == RenderFormat::D32_FLOAT_S8_UINT)
        g_commandLists[g_frame]->clearDepthStencil(false, true, 0.0f, surface->stencilValue);

    g_resolveStats.cascadeMaterialized++;
}

// A and R swap images (as HandOverSurfaceImage does); A gets a new view and descriptor. False when the descriptor heap
// is full (nothing changed then).
static bool CascadeSwap()
{
    GuestTexture* texture = g_cascade.array;
    const uint32_t descriptor = g_textureDescriptorAllocator.allocate();
    if (descriptor < TEXTURE_DESCRIPTOR_NULL_COUNT)
    {
        g_textureDescriptorAllocator.free(descriptor);
        return false;
    }

    // Pending barriers are keyed by image and each object's layout moves with its image, as for a hand-over.
    const uint32_t frame = g_frame;
    g_handOverViews[frame].push_back(std::move(texture->textureView));
    g_handOverDescriptors[frame].push_back(texture->descriptorIndex);
    for (auto& [slice, framebuffer] : texture->framebuffers)
        RetireFramebuffer(framebuffer);

    texture->framebuffers.clear();
    for (auto& view : texture->framebufferViews)
        g_handOverViews[frame].push_back(std::move(view));

    texture->framebufferViews.clear();
    CascadeRetireLayers();

    std::swap(texture->textureHolder, g_cascade.image.textureHolder);
    std::swap(texture->texture, g_cascade.image.texture);
    std::swap(texture->layout, g_cascade.image.layout);
    if (g_cascade.detachedHolder == &g_cascade.image)
        g_cascade.detachedHolder = texture;
    else if (g_cascade.detachedHolder == texture)
        g_cascade.detachedHolder = &g_cascade.image;

    texture->textureView = texture->texture->createTextureView(texture->viewDesc);
    texture->descriptorIndex = descriptor;
    SetTextureDescriptor(texture->descriptorIndex, texture->texture, texture->width, texture->height, RenderTextureLayout::SHADER_READ,
        texture->textureView.get(), texture->mipLevels, texture->depth);

    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (g_textures[i] == texture)
            SetTextureInRenderThread(i, texture);
    }

    g_dirtyStates.renderTargetAndDepthStencil = true;
    g_resolveStats.cascadeAdoptions++;
    return true;
}

// A's slices held in R become A's content (see above).
static void CascadeAdopt()
{
    if (g_cascade.held == 0)
        return;

    GuestSurface* surface = g_cascade.surface;
    GuestTexture* texture = g_cascade.array;

    // S's content in a layer that is not held would be overwritten by a fill-in copy: it goes to S's own image first.
    if (CascadeActive(surface) && (g_cascade.held & (1u << g_cascade.layer)) == 0)
        CascadeMaterializeSurface();

    const uint32_t all = g_cascade.layerCount >= 32 ? ~0u : (1u << g_cascade.layerCount) - 1;
    const uint32_t held = g_cascade.held & all;
    const uint32_t missing = all & ~held;
    // perf8: a missing layer that already holds A's slice needs no fill-in (the slice the game does not draw every frame).
    const uint32_t fill = missing & ~g_cascade.valid;

    // perf8: the copies between R and A (same format and size) as transfer copies.
    bool swapped = false;
    if (std::popcount(fill) <= std::popcount(held))
    {
        if (fill != 0)
        {
            AddBarrier(texture, RenderTextureLayout::COPY_SOURCE);
            AddBarrier(&g_cascade.image, RenderTextureLayout::COPY_DEST);
            FlushBarriers();
            for (uint32_t slice = 0; slice < g_cascade.layerCount; slice++)
            {
                if ((fill & (1u << slice)) == 0)
                    continue;

                CascadeTransferCopy(&g_cascade.image, slice, texture, slice);
                g_resolveStats.cascadeFillIns++;
            }

            g_cascade.valid |= fill;
        }

        g_resolveStats.cascadeFillsSkipped += std::popcount(missing & ~fill);
        swapped = CascadeSwap();
        if (swapped)
        {
            // A's new image is R's: its missing slices equal R's new image (A's previous one), its held ones differ. A copy
            // still to be made into A (it writes A's new image) makes no layer known.
            g_cascade.valid = (texture->sourceSurface != nullptr || texture->copyOwed || texture->pendingCarried) ? 0 : (all & ~held);
        }
    }

    if (!swapped)
    {
        AddBarrier(&g_cascade.image, RenderTextureLayout::COPY_SOURCE);
        AddBarrier(texture, RenderTextureLayout::COPY_DEST);
        FlushBarriers();
        for (uint32_t slice = 0; slice < g_cascade.layerCount; slice++)
        {
            if ((held & (1u << slice)) == 0)
                continue;

            CascadeTransferCopy(texture, slice, &g_cascade.image, slice);
            g_resolveStats.cascadeHeldCopies++;
        }

        g_cascade.valid |= held;

        // The slots that hold A read it again, as after ExecutePendingCopy (the barrier back to sampling).
        for (uint32_t i = 0; i < std::size(g_textures); i++)
        {
            if (g_textures[i] == texture)
                SetTextureInRenderThread(i, texture);
        }
    }

    // S's content (its layer is held here) is now A's slice after a swap, or still R's layer: the next use of S takes it
    // back, its next full clear drops it. The next cascade continues with the next layer.
    if (CascadeActive(surface))
    {
        g_cascade.nextLayer = uint32_t(g_cascade.layer) + (g_cascade.layerResolved ? 1 : 0);
        g_cascade.detachedHolder = swapped ? static_cast<GuestBaseTexture*>(texture) : &g_cascade.image;
        g_cascade.detachedLayer = g_cascade.layer;
        g_cascade.layer = -1;
        g_cascade.layerResolved = false;
        CascadeFormatChanged();
    }

    g_cascade.held = 0;
}

// A is about to be read or written (bound to a slot, updated from the CPU): its held slices first.
static void CascadeAdoptIfHeld(const GuestTexture* texture)
{
    if (texture != nullptr && texture == g_cascade.array && g_cascade.held != 0)
        CascadeAdopt();
}

// A CPU update of a texture: A's held slices first, and R no longer holds a copy of any of A's slices (perf8).
static void CascadeTextureUpdated(const GuestTexture* texture)
{
    CascadeAdoptIfHeld(texture);
    if (texture != nullptr && texture == g_cascade.array)
        g_cascade.valid = 0;
}

// The learning: a single-sampled depth surface resolved into slice 0 of a depth array texture of its size with one level
// (the cascaded shadow map). R is made like A.
static void CascadeLearn(GuestSurface* surface, GuestTexture* texture, uint32_t slice)
{
    if (g_cascade.surface != nullptr || g_cascade.failed || slice != 0 || texture->type != ResourceType::ArrayTexture ||
        !IsDepthSurface(surface) || surface == g_backBuffer || surface->sampleCount != RenderSampleCount::COUNT_1 ||
        surface->textureHolder == nullptr || texture->textureHolder == nullptr || texture->patchedTexture != nullptr ||
        !RenderFormatIsDepth(texture->format) || texture->width != surface->width || texture->height != surface->height ||
        texture->mipLevels != 1 || texture->depth < 2 || texture->depth > 32)
    {
        return;
    }

    // R's copies use copy_depth for its format; the D32_FLOAT one exists with SwitchDepthArrayTexturesD32.
    if (texture->format == RenderFormat::D32_FLOAT ? g_copyDepthPipelineD32 == nullptr : texture->format != surface->format)
        return;

    RenderTextureDesc desc;
    desc.dimension = RenderTextureDimension::TEXTURE_2D;
    desc.width = texture->width;
    desc.height = texture->height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arraySize = texture->depth;
    desc.format = texture->format;
    desc.flags = RenderTextureFlag::DEPTH_TARGET | RenderTextureFlag::NO_ZCULL;

    RenderTextureDesc achieved = desc;
    auto image = CreateTextureChecked(desc, "cascade-image", &achieved);
    if (image == nullptr || achieved.width != desc.width || achieved.height != desc.height || achieved.arraySize != desc.arraySize)
    {
        g_cascade.failed = true;
        fprintf(stderr, "Switch renderer: cascade adoption: the cascade image could not be made; the cascades are copied.\n");
        return;
    }

    g_cascade.image.textureHolder = std::move(image);
    g_cascade.image.texture = g_cascade.image.textureHolder.get();
    g_cascade.image.width = texture->width;
    g_cascade.image.height = texture->height;
    g_cascade.image.format = texture->format;
    g_cascade.image.layout = RenderTextureLayout::UNKNOWN;
    g_cascade.layerCount = texture->depth;
    g_cascade.layers.clear();
    g_cascade.layers.resize(texture->depth);
    g_cascade.surface = surface;
    g_cascade.array = texture;
    g_cascade.layer = -1;
    g_cascade.nextLayer = 1; // this resolve was slice 0's (a pending copy as before): the next cascade is slice 1's
    g_cascade.held = 0;
    g_cascade.valid = 0;
    std::fill(std::begin(g_cascade.continueTarget), std::end(g_cascade.continueTarget), int8_t(-1));
    g_cascade.learnFrom = -1;
    g_cascade.continuedFrom = -1;

    fprintf(stderr, "Switch renderer: cascade adoption: shadow surface %ux%u, cascaded shadow map of %u slices (%s).\n",
        surface->width, surface->height, texture->depth, texture->format == RenderFormat::D32_FLOAT ? "D32_FLOAT" : "D32_FLOAT_S8_UINT");
}

// ProcStretchRect, after the copies the port had made: true when the resolve is held instead of becoming a pending copy.
static bool CascadeOnResolve(GuestSurface* surface, GuestTexture* texture, uint32_t slice)
{
    if (!g_switchRenderer.cascadeAdoption)
        return false;

    if (surface != g_cascade.surface)
    {
        // Another surface's resolve into a slice of A replaces it: no longer held (an adoption fills it from A, which
        // holds that copy's result by then, or gets it from the copy afterwards), and no longer known in R.
        if (texture == g_cascade.array && slice < 32)
        {
            g_cascade.held &= ~(1u << slice);
            g_cascade.valid &= ~(1u << slice);
        }

        CascadeLearn(surface, texture, slice);
        return false;
    }

    if (CascadeActive(surface) && texture == g_cascade.array && slice == uint32_t(g_cascade.layer))
    {
        // As the resolve replaced a copy still pending into the texture.
        if (texture->sourceSurface != nullptr)
        {
            texture->sourceSurface->destinationTextures.erase(texture);
            texture->sourceSurface = nullptr;
        }

        g_cascade.held |= 1u << slice;
        g_cascade.layerResolved = true;
        g_cascade.continuedFrom = -1;
        g_resolveStats.cascadeHeld++;
        FrameLogResolve(surface, texture);

        // perf8: A takes the held slices at the first draw that samples it (CascadeAdoptForDraw), not here: a slot that
        // merely holds A reads nothing until then.
        return true;
    }

    // An ordinary resolve of S: its content in its own image. A slice it replaces is no longer held (the copy now
    // pending writes it, and an adoption before that fills the layer from A).
    const bool mismatch = CascadeActive(surface);
    CascadeMaterializeSurface();
    if (texture == g_cascade.array && slice < 32)
    {
        g_cascade.held &= ~(1u << slice);
        g_cascade.valid &= ~(1u << slice);

        // perf8: where a cascade that kept drawing after its resolve went (learned in S's own image), or where it went
        // instead of the slice it was continued into.
        const int32_t from = g_cascade.learnFrom >= 0 ? g_cascade.learnFrom : (mismatch ? g_cascade.continuedFrom : -1);
        if (from >= 0 && from < 32)
            g_cascade.continueTarget[from] = int8_t(slice);
    }
    g_cascade.learnFrom = -1;
    g_cascade.continuedFrom = -1;

    // A cascade resolved into another slice than the one it was drawn for, again and again (the game draws them in
    // another order than its slices): adoption would only add copies, so it stops for the session.
    if (mismatch && ++g_cascade.mismatches >= 8)
    {
        g_cascade.failed = true;
        fprintf(stderr, "Switch renderer: cascade adoption: the cascades do not come in slice order; they are copied from now on.\n");
    }

    return false;
}

// ProcClear of S (g_depthStencil), after the pending copies of the targets were made: true when the clear was made here,
// into R's layer of the next cascade.
static bool CascadeOnClear(uint32_t flags, bool clearDepth, bool clearStencil, float z, uint32_t stencilValue)
{
    GuestSurface* surface = g_cascade.surface;
    const bool full = clearDepth && clearStencil && (flags & D3DCLEAR_TARGET) == 0 && g_renderTarget == nullptr &&
        surface->destinationTextures.empty();
    if (!full || g_cascade.failed)
    {
        CascadeMaterializeSurface();
        return false;
    }

    // A full clear: S's content left in a layer is no longer needed.
    g_cascade.detachedHolder = nullptr;
    g_cascade.detachedLayer = -1;
    g_cascade.learnFrom = -1;
    g_cascade.continuedFrom = -1;

    if (CascadeActive(surface))
    {
        if (g_cascade.layerResolved)
        {
            g_cascade.layer++;
            g_cascade.layerResolved = false;
        }
    }
    else
    {
        // Slices of an earlier sequence that A was not needed for since.
        if (g_cascade.held != 0)
            CascadeAdopt();

        g_cascade.layer = int32_t(g_cascade.nextLayer);
        g_cascade.layerResolved = false;
    }

    if (uint32_t(g_cascade.layer) >= g_cascade.layerCount)
    {
        // More cascades than slices: the rest are drawn into S's own image.
        g_cascade.layer = -1;
        CascadeFormatChanged();
        return false;
    }

    CascadeFormatChanged();
    g_cascade.valid &= ~(1u << g_cascade.layer);
    AddBarrier(&g_cascade.image, RenderTextureLayout::DEPTH_WRITE);
    FlushBarriers();
    CascadeBindLayer();
    g_commandLists[g_frame]->clearDepthStencil(true, CascadeImageHasStencil(), z, stencilValue);

    // Vulkan stores the low 8 bits of the value in the S8 part; S's own image is owed it (CascadeMaterializeSurface).
    surface->stencilKnown = true;
    surface->stencilValue = uint8_t(stencilValue);
    g_dirtyStates.renderTargetAndDepthStencil = true;
    return true;
}

// FlushRenderStateForRenderThread: a draw that tests S. It keeps drawing into R's layer only without a colour target and
// without the stencil test or stencil writes (S's stencil stays the uniform value its own image is owed, which is all a
// copy back has to restore); otherwise S takes its content back first.
// perf8: S keeps drawing (and writing depth) after its cascade was resolved into the slice of its layer, which is held:
// the layer must keep that content. When the previous frame showed where the next resolve goes (a layer neither held
// nor the same), S continues there: R's layer is copied into that layer (one transfer copy within R, in the GENERAL
// layout) and S draws on in it, so that resolve is held too. Otherwise S takes its content back into its own image, as
// before, and the slice of its next resolve is learned. False when S went back to its own image.
static bool CascadeContinue()
{
    const uint32_t from = uint32_t(g_cascade.layer);
    const int32_t target = from < 32 ? g_cascade.continueTarget[from] : -1;
    if (target < 0 || uint32_t(target) >= g_cascade.layerCount || uint32_t(target) == from || (g_cascade.held & (1u << target)) != 0)
    {
        CascadeMaterializeSurface();
        g_cascade.learnFrom = int32_t(from);
        return false;
    }

    AddBarrier(&g_cascade.image, RenderTextureLayout::GENERAL);
    FlushBarriers();
    CascadeTransferCopy(&g_cascade.image, uint32_t(target), &g_cascade.image, from);
    AddBarrier(&g_cascade.image, RenderTextureLayout::DEPTH_WRITE);
    FlushBarriers();

    g_cascade.layer = target;
    g_cascade.layerResolved = false;
    g_cascade.continuedFrom = int32_t(from);
    g_cascade.valid &= ~(1u << target);
    g_dirtyStates.renderTargetAndDepthStencil = true; // SetFramebuffer binds the new layer (CascadeBindLayer)
    g_resolveStats.cascadeContinued++;
    return true;
}

static void CascadeBeforeDraw(GuestSurface* renderTarget, GuestSurface* depthStencil)
{
    if (depthStencil == nullptr || depthStencil != g_cascade.surface)
        return;

    if (g_cascade.detachedHolder != nullptr)
    {
        CascadeMaterializeSurface();
    }
    else if (CascadeActive(depthStencil) && (renderTarget != nullptr || depthStencil->sampleCount != RenderSampleCount::COUNT_1 ||
        g_pipelineState.stencilEnable || StencilMayBeWritten(g_pipelineState)))
    {
        CascadeMaterializeSurface();
    }
    else if (CascadeActive(depthStencil) && g_cascade.layerResolved && g_pipelineState.zEnable && g_pipelineState.zWriteEnable)
    {
        // perf8: the held layer must not change (in perf7 this draw went into it).
        CascadeContinue();
    }
}

// Present (before its copies): the cascade sequence ends with the frame. The resolve of the last cascade was a copy still
// pending at the port's Present: dropped there when a colour copy was pending too (then A's slice keeps its content and S
// keeps the cascade, in its own image now), kept pending otherwise (A gets it when adopted). A cascade drawn and not
// resolved goes to S's own image.
static void CascadeAtPresent()
{
    if (g_cascade.surface == nullptr)
        return;

    g_cascade.nextLayer = 0;
    g_cascade.learnFrom = -1;
    g_cascade.continuedFrom = -1;
    GuestSurface* surface = g_cascade.surface;
    if (!CascadeActive(surface))
        return;

    if (!g_cascade.layerResolved)
    {
        CascadeMaterializeSurface();
        return;
    }

    bool colourCopyPending = false;
    for (const auto pending : g_pendingSurfaceCopies)
    {
        if (IsDepthSurface(pending))
            continue;

        for (const auto [texture, slice] : pending->destinationTextures)
        {
            if (!UsesLongerPendingCopies() || (!texture->copyOwed && !texture->pendingCarried))
            {
                colourCopyPending = true;
                break;
            }
        }

        if (colourCopyPending)
            break;
    }

    const uint32_t slice = uint32_t(g_cascade.layer);
    if (colourCopyPending)
    {
        CascadeMaterializeSurface();
        g_cascade.held &= ~(1u << slice);
        g_resolveStats.depthDropped++;
    }
    else
    {
        g_cascade.detachedHolder = &g_cascade.image;
        g_cascade.detachedLayer = int32_t(slice);
        g_cascade.layer = -1;
        g_cascade.layerResolved = false;
        CascadeFormatChanged();
    }
}

// ProcDestructResource: the pair is forgotten when either goes. A's slices held for it are adopted when S goes; when A
// goes, S takes its content back first (it may still be used).
static void CascadeForget(GuestResource* resource)
{
    if (g_cascade.surface == nullptr ||
        (resource != static_cast<GuestResource*>(g_cascade.surface) && resource != static_cast<GuestResource*>(g_cascade.array)))
    {
        return;
    }

    if (resource == static_cast<GuestResource*>(g_cascade.surface))
    {
        if (g_cascade.held != 0)
            CascadeAdopt();
    }
    else
    {
        CascadeMaterializeSurface();
    }

    g_cascade.layer = -1;
    g_cascade.layerResolved = false;
    g_cascade.detachedHolder = nullptr;
    g_cascade.detachedLayer = -1;
    g_cascade.held = 0;
    g_cascade.valid = 0;
    g_cascade.learnFrom = -1;
    g_cascade.continuedFrom = -1;
    CascadeFormatChanged();

    CascadeRetireLayers();
    if (g_cascade.image.textureHolder != nullptr)
        g_retiredCascadeImages[g_frame].push_back(std::move(g_cascade.image.textureHolder));

    g_cascade.image.texture = nullptr;
    g_cascade.image.layout = RenderTextureLayout::UNKNOWN;
    g_cascade.layers.clear();
    g_cascade.layerCount = 0;
    g_cascade.nextLayer = 0;
    g_cascade.surface = nullptr;
    g_cascade.array = nullptr;
}

// [Switch] SwitchCoverageHandOver. A surface resolved into one texture is about to be drawn into, so its pending copy is
// made first. When the draw neither blends with the target nor leaves a channel of it unwritten, only the pixels the
// draw does not write need the old contents: the texture takes the surface's image (the copy's exact result, as with a
// hand-over at a clear), the surface takes the texture's old image, and the draw also marks every pixel it writes with
// a stencil value in a stencil buffer of the surface's size (the draw has no depth buffer: its colour output and every
// test are unchanged; pixels the pixel shader discards, through a kill, the alpha test or the conditional rendering
// prologue, write no mark: the translated shaders use late tests). Right after the draw, the pixels not holding that
// value get the old contents with the same copy shader, from the image the texture now holds. Every pixel then holds
// exactly what the copy followed by the draw gave, and slots holding the texture read it as after the copy (the draw
// itself included, when it samples the texture). The stencil variant of the draw's pipeline is compiled by the
// compiler threads; until it exists the copy is made as before.
struct CoverageStencil
{
    std::unique_ptr<RenderTexture> texture;
    RenderTextureLayout layout = RenderTextureLayout::UNKNOWN;
    uint32_t width = 0;
    uint32_t height = 0;
    // The value the next hand-over writes (1-255). Each is used by one hand-over between two clears of the buffer, so the
    // pixels holding the current one are exactly those its draw wrote: the buffer is cleared only when it is new and when
    // the values run out, not before every draw.
    uint32_t nextReference = 0;
};

struct CoverageFramebuffer
{
    const RenderTexture* colorImage;
    const CoverageStencil* stencil;
    std::unique_ptr<RenderFramebuffer> framebuffer;
};

struct CoverageFixup
{
    bool pending = false;
    bool clearStencil = false;
    uint32_t reference = 0;
    GuestSurface* surface = nullptr;
    CoverageStencil* stencil = nullptr;
    uint32_t sourceDescriptor = 0;
    RenderPipeline* drawPipeline = nullptr;
};

static std::vector<std::unique_ptr<CoverageStencil>> g_coverageStencils;
static std::vector<CoverageFramebuffer> g_coverageFramebuffers;
static ankerl::unordered_dense::map<RenderFormat, std::unique_ptr<RenderPipeline>> g_coverageFixupPipelines;
static CoverageFixup g_coverageFixup;

static void ForgetCoverageFramebuffersOf(const RenderTexture* image)
{
    for (size_t i = 0; i < g_coverageFramebuffers.size();)
    {
        if (g_coverageFramebuffers[i].colorImage == image)
        {
            if (g_framebuffer == g_coverageFramebuffers[i].framebuffer.get())
                g_framebuffer = nullptr;

            g_coverageFramebuffers[i] = std::move(g_coverageFramebuffers.back());
            g_coverageFramebuffers.pop_back();
        }
        else
        {
            i++;
        }
    }
}

static CoverageStencil* GetCoverageStencil(uint32_t width, uint32_t height)
{
    for (auto& stencil : g_coverageStencils)
    {
        if (stencil->width == width && stencil->height == height)
            return stencil.get();
    }

    RenderTextureDesc desc;
    desc.dimension = RenderTextureDimension::TEXTURE_2D;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arraySize = 1;
    desc.format = RenderFormat::S8_UINT;
    desc.flags = RenderTextureFlag::DEPTH_TARGET | RenderTextureFlag::NO_ZCULL;

    auto stencil = std::make_unique<CoverageStencil>();
    stencil->texture = g_device->createTexture(desc);
    if (stencil->texture == nullptr)
        return nullptr;

    stencil->width = width;
    stencil->height = height;
    g_coverageStencils.push_back(std::move(stencil));
    fprintf(stderr, "Switch renderer: %ux%u coverage stencil created (hand-overs at draws).\n", width, height);
    return g_coverageStencils.back().get();
}

static RenderFramebuffer* GetCoverageFramebuffer(const RenderTexture* colorImage, const CoverageStencil* stencil)
{
    for (auto& entry : g_coverageFramebuffers)
    {
        if (entry.colorImage == colorImage && entry.stencil == stencil)
            return entry.framebuffer.get();
    }

    RenderFramebufferDesc desc;
    desc.colorAttachments = const_cast<const RenderTexture**>(&colorImage);
    desc.colorAttachmentsCount = 1;
    desc.depthAttachment = stencil->texture.get();

    CoverageFramebuffer entry{ colorImage, stencil, g_device->createFramebuffer(desc) };
    g_coverageFramebuffers.push_back(std::move(entry));
    return g_coverageFramebuffers.back().framebuffer.get();
}

static RenderPipeline* GetCoverageFixupPipeline(RenderFormat format)
{
    auto& pipeline = g_coverageFixupPipelines[format];
    if (pipeline == nullptr)
    {
        RenderGraphicsPipelineDesc desc;
        desc.pipelineLayout = g_pipelineLayout.get();
        desc.vertexShader = g_copyShader.get();
        desc.pixelShader = g_copyColorShader.get();
        desc.renderTargetFormat[0] = format;
        desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
        desc.renderTargetCount = 1;
        desc.depthTargetFormat = RenderFormat::S8_UINT;
        desc.depthEnabled = false;
        desc.depthWriteEnabled = false;
        desc.stencilEnabled = true;
        desc.stencilReadMask = 0xFF;
        desc.stencilWriteMask = 0;
        desc.dynamicStencilReferenceEnabled = true; // The hand-over's value: pixels not holding it.
        desc.stencilFrontFace.compareFunction = RenderComparisonFunction::NOT_EQUAL;
        desc.stencilBackFace = desc.stencilFrontFace;
        pipeline = g_device->createGraphicsPipeline(desc);
    }

    return pipeline.get();
}

// The colour channels a surface of this format stores (colour write mask bits).
static uint32_t FormatChannelMask(RenderFormat format)
{
    switch (format)
    {
    case RenderFormat::R8_UNORM:
    case RenderFormat::R32_FLOAT:
        return 0x1;
    case RenderFormat::R16G16_FLOAT:
        return 0x3;
    case RenderFormat::R8G8B8A8_UNORM:
    case RenderFormat::B8G8R8A8_UNORM:
    case RenderFormat::R16G16B16A16_FLOAT:
        return 0xF;
    default:
        return 0; // Unknown: not handed over.
    }
}

// Blending whose result does not depend on the target: both destination factors are zero and nothing else reads it (the
// source factors must not name the destination; MIN and MAX ignore the factors). Only for UNORM targets, whose texels
// are finite: a zero factor times an infinity or a NaN would not be 0.
static bool BlendIgnoresDestination(const PipelineState& state, RenderFormat format)
{
    switch (format)
    {
    case RenderFormat::R8_UNORM:
    case RenderFormat::R8G8B8A8_UNORM:
    case RenderFormat::B8G8R8A8_UNORM:
        break;
    default:
        return false;
    }

    auto readsDestination = [](RenderBlend blend)
        {
            return blend == RenderBlend::DEST_COLOR || blend == RenderBlend::INV_DEST_COLOR ||
                blend == RenderBlend::DEST_ALPHA || blend == RenderBlend::INV_DEST_ALPHA || blend == RenderBlend::SRC_ALPHA_SAT;
        };

    auto combinesLinearly = [](RenderBlendOperation operation)
        {
            return operation == RenderBlendOperation::ADD || operation == RenderBlendOperation::SUBTRACT ||
                operation == RenderBlendOperation::REV_SUBTRACT;
        };

    return state.destBlend == RenderBlend::ZERO && state.destBlendAlpha == RenderBlend::ZERO &&
        combinesLinearly(state.blendOp) && combinesLinearly(state.blendOpAlpha) &&
        !readsDestination(state.srcBlend) && !readsDestination(state.srcBlendAlpha);
}

// Whether a draw of this (raw) state may change the stencil of a bound depth surface.
static bool StencilMayBeWritten(const PipelineState& state)
{
    if (!state.stencilEnable || (state.stencilWriteMask & 0xFF) == 0)
        return false;

    auto writes = [](RenderStencilOp op) { return op != RenderStencilOp::KEEP; };
    const bool front = writes(state.stencilFail) || writes(state.stencilZFail) || writes(state.stencilPass);
    const bool back = state.stencilTwoSided ?
        (writes(state.stencilFailCCW) || writes(state.stencilZFailCCW) || writes(state.stencilPassCCW)) : front;

    return front || back;
}

// The vertices of the DrawPrimitiveUP being flushed (guest data, big-endian; ProcDrawPrimitiveUP), for
// GetFullScreenCoverage and IsIdentityRestore. Other draws leave data null.
struct CurrentDrawVertices
{
    const uint8_t* data = nullptr;
    uint32_t count = 0;
    uint32_t stride = 0;
    uint32_t primitiveType = 0;
};

static CurrentDrawVertices g_currentDrawVertices;

// Pixels along the edges of a target (up to four rectangles, one per side).
struct EdgePixels
{
    RenderRect rects[4];
    uint32_t count = 0;
};

// The viewport and scissor rectangle the draw being flushed gets (FlushViewport's, which it applies only when they are
// dirty: otherwise the ones it applied last, known when they equal what it would apply now). False when unknown.
static bool GetDrawViewport(RenderViewport& viewport, RenderRect& scissor)
{
    ComputeFlushViewport(viewport, scissor);

    const bool viewportKnown = g_dirtyStates.viewport || (g_appliedViewportValid && g_appliedViewport.x == viewport.x &&
        g_appliedViewport.y == viewport.y && g_appliedViewport.width == viewport.width && g_appliedViewport.height == viewport.height &&
        g_appliedViewport.minDepth == viewport.minDepth && g_appliedViewport.maxDepth == viewport.maxDepth);
    const bool scissorKnown = g_dirtyStates.scissorRect || (g_appliedScissorValid && g_appliedScissor.left == scissor.left &&
        g_appliedScissor.top == scissor.top && g_appliedScissor.right == scissor.right && g_appliedScissor.bottom == scissor.bottom);

    return viewportKnown && scissorKnown;
}

// The vertex input of the draw being flushed at `location`, as 32-bit floats in stream 0: its element and component
// count, or 0.
static uint32_t GetFloatElement(uint32_t location, const RenderInputElement*& element)
{
    element = nullptr;
    const GuestVertexDeclaration* declaration = g_pipelineState.vertexDeclaration;
    for (uint32_t i = 0; i < declaration->inputElementCount; i++)
    {
        if (declaration->inputElements[i].location == location)
        {
            element = &declaration->inputElements[i];
            break;
        }
    }

    if (element == nullptr || element->slotIndex != 0)
        return 0;

    switch (element->format)
    {
    case RenderFormat::R32G32_FLOAT:
        return 2;
    case RenderFormat::R32G32B32_FLOAT:
        return 3;
    case RenderFormat::R32G32B32A32_FLOAT:
        return 4;
    default:
        return 0;
    }
}

static float LoadGuestFloat(const uint8_t* data)
{
    uint32_t value;
    memcpy(&value, data, sizeof(value));
    return std::bit_cast<float>(ByteSwap(value));
}

#if defined(SHADER_FLAG_POSITION_XYZW_PASS_THROUGH) && defined(SHADER_FLAG_POSITION_XY01_PASS_THROUGH) && \
    defined(SHADER_FLAG_PIXEL_KILL_CONDITIONAL) && defined(SHADER_FLAG_COPY_SLOT_SHIFT)
// XenosRecomp's shader flags (shader_common.h): the coverage proofs and the restore detection need them.
#define MARATHON_RECOMP_SWITCH_SHADER_FLAGS
static constexpr uint32_t POSITION_PASS_THROUGH_FLAGS = SHADER_FLAG_POSITION_PASS_THROUGH |
    SHADER_FLAG_POSITION_XYZW_PASS_THROUGH | SHADER_FLAG_POSITION_XY01_PASS_THROUGH;
#endif

// The position of vertex `index` of the draw being flushed, as the vertex shader outputs it (one of the
// SHADER_FLAG_POSITION_* pass-throughs, before the half-pixel offset): false unless w is 1 and z within the clip volume.
static bool LoadPassThroughPosition(uint32_t shaderFlags, const RenderInputElement* position, uint32_t components, uint32_t index,
    float& x, float& y)
{
#ifdef MARATHON_RECOMP_SWITCH_SHADER_FLAGS
    const auto& draw = g_currentDrawVertices;
    const uint8_t* vertex = draw.data + size_t(index) * draw.stride + position->alignedByteOffset;

    // The input assembler supplies z = 0 and w = 1 for the components the element does not have.
    x = LoadGuestFloat(vertex);
    y = LoadGuestFloat(vertex + 4);
    float z = components >= 3 ? LoadGuestFloat(vertex + 8) : 0.0f;
    float w = 1.0f;

    if ((shaderFlags & SHADER_FLAG_POSITION_XYZW_PASS_THROUGH) != 0 && components >= 4)
        w = LoadGuestFloat(vertex + 12);
    else if ((shaderFlags & SHADER_FLAG_POSITION_XY01_PASS_THROUGH) != 0)
        z = 0.0f;

    // Clipped against 0 <= z <= w; w = 1 keeps the rasterization linear in x and y.
    return std::isfinite(x) && std::isfinite(y) && w == 1.0f && z >= 0.0f && z <= 1.0f;
#else
    return false;
#endif
}

// [Switch] SwitchExactCoverage. Whether the draw being flushed covers every pixel of `surface` except at most a frame
// EDGE_PIXELS wide, returned in `edges`. It must be a DrawPrimitiveUP of one axis-aligned rectangle made of two triangles
// that share its diagonal (a strip, fan or quad of four vertices), through a vertex shader whose position is the
// POSITION input with w = 1 plus the half-pixel offset (SHADER_FLAG_POSITION_*_PASS_THROUGH; Marathon's full-screen
// passes use the xyzw and xy01 forms), inside the depth range, not culled (the pipeline's front face), with the clip
// plane off (every translated vertex shader writes the clip distance while it is on). The GPU then covers each pixel
// whose centre lies inside the rectangle, the viewport and the scissor (FlushViewport's, scaled to the target as the
// GPU gets them); a centre on the diagonal belongs to exactly one of the two triangles (Vulkan's rule for a shared
// edge). Centres on or within EDGE_MARGIN of an outer edge depend on the rasterizer's tie rule and sub-pixel rounding:
// those pixels go into `edges`. The caller checks that every covered pixel is written in full.
static bool GetFullScreenCoverage(const GuestSurface* surface, uint32_t shaderFlags, EdgePixels& edges)
{
    static constexpr double EDGE_MARGIN = 1.0 / 64.0; // Pixels; vertices are snapped to 1/256.
    static constexpr int64_t EDGE_PIXELS = 2;

    const auto& draw = g_currentDrawVertices;

    // The two triangles in Vulkan's vertex order (facing) and the vertices of the edge they share.
    uint32_t triangles[2][3];
    uint32_t shared0, shared1;
    switch (draw.primitiveType)
    {
    case D3DPT_TRIANGLESTRIP:
        // (0, 1, 2) and (1, 3, 2).
        triangles[0][0] = 0; triangles[0][1] = 1; triangles[0][2] = 2;
        triangles[1][0] = 1; triangles[1][1] = 3; triangles[1][2] = 2;
        shared0 = 1;
        shared1 = 2;
        break;
    case D3DPT_TRIANGLEFAN:
    case D3DPT_QUADLIST:
        // (0, 1, 2) and (0, 2, 3): the quad list's indices, and a fan either as those indices or natively
        // ((1, 2, 0) and (2, 3, 0), the same windings).
        triangles[0][0] = 0; triangles[0][1] = 1; triangles[0][2] = 2;
        triangles[1][0] = 0; triangles[1][1] = 2; triangles[1][2] = 3;
        shared0 = 0;
        shared1 = 2;
        break;
    default:
        g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_NOT_QUAD]++;
        return false;
    }

    if (draw.data == nullptr || draw.count != 4)
    {
        g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_NOT_QUAD]++;
        return false;
    }

    const RenderInputElement* position;
    const uint32_t components = GetFloatElement(0, position);
    if (components == 0 || position->alignedByteOffset + components * sizeof(float) > draw.stride)
    {
        g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_SHAPE]++;
        return false;
    }

    float x[4], y[4];
    for (uint32_t i = 0; i < 4; i++)
    {
        if (!LoadPassThroughPosition(shaderFlags, position, components, i, x[i], y[i]))
        {
            g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_SHAPE]++;
            return false;
        }
    }

    // Two x and two y values, each corner once, the shared edge a diagonal.
    auto twoValues = [](const float* values, float& low, float& high)
        {
            low = std::min({ values[0], values[1], values[2], values[3] });
            high = std::max({ values[0], values[1], values[2], values[3] });
            for (uint32_t i = 0; i < 4; i++)
            {
                if (values[i] != low && values[i] != high)
                    return false;
            }
            return low < high;
        };

    float xLow, xHigh, yLow, yHigh;
    uint32_t corners = 0;
    if (twoValues(x, xLow, xHigh) && twoValues(y, yLow, yHigh))
    {
        for (uint32_t i = 0; i < 4; i++)
            corners |= 1u << ((x[i] == xHigh ? 1 : 0) | (y[i] == yHigh ? 2 : 0));
    }

    if (corners != 0xF || x[shared0] == x[shared1] || y[shared0] == y[shared1])
    {
        g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_SHAPE]++;
        return false;
    }

    RenderViewport viewport;
    RenderRect scissor;
    if (!GetDrawViewport(viewport, scissor))
    {
        g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_VIEWPORT]++;
        return false;
    }

    // Framebuffer coordinates. The vertex shader adds the half-pixel offset of the bound framebuffer (the surface's size,
    // SetFramebuffer); DXC then negates y (-fvk-invert-y), so y grows downwards as in D3D.
    const double offsetX = 1.0f / float(surface->width);
    const double offsetY = -1.0f / float(surface->height);
    double fx[4], fy[4];
    for (uint32_t i = 0; i < 4; i++)
    {
        fx[i] = double(viewport.x) + (double(x[i]) + offsetX + 1.0) * double(viewport.width) * 0.5;
        fy[i] = double(viewport.y) + (1.0 - (double(y[i]) + offsetY)) * double(viewport.height) * 0.5;
    }

    // Facing: twice the signed area with y down is positive for a triangle that turns clockwise on screen, which is the
    // front face with the pipeline's CLOCKWISE (plume: VK_FRONT_FACE_CLOCKWISE; D3DCULL_*_CW states), the back face with
    // COUNTER_CLOCKWISE.
    if (g_pipelineState.cullMode != RenderCullMode::NONE)
    {
        for (const auto& triangle : triangles)
        {
            double area = 0.0;
            for (uint32_t k = 0; k < 3; k++)
            {
                const uint32_t a = triangle[k];
                const uint32_t b = triangle[(k + 1) % 3];
                area += fx[a] * fy[b] - fx[b] * fy[a];
            }

            const bool clockwise = area > 0.0;
            const bool front = (g_pipelineState.frontFace == RenderFrontFace::CLOCKWISE) == clockwise;
            if ((g_pipelineState.cullMode == RenderCullMode::FRONT) == front)
            {
                g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_SHAPE]++;
                return false;
            }
        }
    }

    // What is rasterized: the rectangle within the viewport (the clip volume), then the scissor, within the target.
    const double left = std::max(std::min({ fx[0], fx[1], fx[2], fx[3] }), double(viewport.x));
    const double right = std::min(std::max({ fx[0], fx[1], fx[2], fx[3] }), double(viewport.x) + double(viewport.width));
    const double top = std::max(std::min({ fy[0], fy[1], fy[2], fy[3] }), double(viewport.y));
    const double bottom = std::min(std::max({ fy[0], fy[1], fy[2], fy[3] }), double(viewport.y) + double(viewport.height));

    if (!(left < right) || !(top < bottom))
    {
        g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_EDGES]++;
        return false;
    }

    const int64_t width = surface->width;
    const int64_t height = surface->height;
    const int64_t firstColumn = std::max<int64_t>({ int64_t(std::ceil(left + EDGE_MARGIN - 0.5)), scissor.left, 0 });
    const int64_t lastColumn = std::min<int64_t>({ int64_t(std::floor(right - EDGE_MARGIN - 0.5)), int64_t(scissor.right) - 1, width - 1 });
    const int64_t firstRow = std::max<int64_t>({ int64_t(std::ceil(top + EDGE_MARGIN - 0.5)), scissor.top, 0 });
    const int64_t lastRow = std::min<int64_t>({ int64_t(std::floor(bottom - EDGE_MARGIN - 0.5)), int64_t(scissor.bottom) - 1, height - 1 });

    if (firstColumn > EDGE_PIXELS || lastColumn < width - 1 - EDGE_PIXELS || firstRow > EDGE_PIXELS || lastRow < height - 1 - EDGE_PIXELS)
    {
        g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_EDGES]++;
        return false;
    }

    edges.count = 0;
    auto add = [&](int64_t l, int64_t t, int64_t r, int64_t b)
        {
            if (l < r && t < b)
                edges.rects[edges.count++] = RenderRect(int32_t(l), int32_t(t), int32_t(r), int32_t(b));
        };

    add(0, 0, firstColumn, height);
    add(lastColumn + 1, 0, width, height);
    add(firstColumn, 0, lastColumn + 1, firstRow);
    add(firstColumn, lastRow + 1, lastColumn + 1, height);
    return true;
}

// Whether the pixel shader of a pipeline of this sanitized state may discard a pixel: a Xenos kill instruction, the
// conditional rendering prologue while the pipeline has SPEC_CONSTANT_CONDITIONAL_RENDERING, the alpha test, alpha to
// coverage or the blend skip. The port's own pixel shaders are assumed to.
static bool PixelShaderMayDiscard(const PipelineState& sanitized)
{
    const GuestShader* pixelShader = sanitized.pixelShader;
    if (pixelShader == nullptr || pixelShader->shaderCacheEntry == nullptr)
        return true;

#ifdef MARATHON_RECOMP_SWITCH_SHADER_FLAGS
    uint32_t discardBits = SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE;
#if defined(SPEC_CONSTANT_BLEND_SKIP_ALPHA) && defined(SPEC_CONSTANT_BLEND_SKIP_ZERO)
    discardBits |= SPEC_CONSTANT_BLEND_SKIP_ALPHA | SPEC_CONSTANT_BLEND_SKIP_ZERO;
#endif
    if (sanitized.enableAlphaToCoverage || (sanitized.specConstants & discardBits) != 0)
        return true;

    const uint32_t flags = pixelShader->shaderCacheEntry->flags;
    if ((flags & SHADER_FLAG_PIXEL_KILL) == 0)
        return false;

    return (flags & SHADER_FLAG_PIXEL_KILL_CONDITIONAL) == 0 || (sanitized.specConstants & SPEC_CONSTANT_CONDITIONAL_RENDERING) != 0;
#else
    return true;
#endif
}

// Whether the draw being flushed, whose state `sanitized` is, writes every pixel of `surface` it covers in full and
// covers all of them but `edges` (GetFullScreenCoverage). The caller has checked the colour mask, the blend and the
// missing depth buffer.
static bool IsExactFullScreenDraw(const PipelineState& sanitized, const GuestSurface* surface, EdgePixels& edges)
{
#ifdef MARATHON_RECOMP_SWITCH_SHADER_FLAGS
    const GuestShader* vertexShader = sanitized.vertexShader;
    const uint32_t vertexFlags = vertexShader->shaderCacheEntry != nullptr ? vertexShader->shaderCacheEntry->flags : 0;
    if ((vertexFlags & POSITION_PASS_THROUGH_FLAGS) == 0 || g_sharedConstants.clipPlaneEnabled || sanitized.enableConditionalSurvey ||
        PixelShaderMayDiscard(sanitized))
    {
        g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_SHADERS]++;
        return false;
    }

    return GetFullScreenCoverage(surface, vertexFlags, edges);
#else
    g_resolveStats.exactCoverageMisses[EXACT_COVERAGE_MISS_SHADERS]++;
    return false;
#endif
}

// Work on the edge pixels of the draw being flushed, done once its framebuffer is bound (FlushRenderStateForRenderThread):
// the target's old contents copied in (an exact hand-over, before the draw overwrites what it covers of them) or a
// skipped clear's colour (DeferredClear).
struct PendingEdgePixels
{
    enum class Mode
    {
        NONE,
        COPY,
        CLEAR
    };

    Mode mode = Mode::NONE;
    EdgePixels edges;
    uint32_t sourceDescriptor = 0;
    RenderFormat format = RenderFormat::UNKNOWN;
    RenderColor color;
};

static PendingEdgePixels g_pendingEdgePixels;

// The state a copy draw of the port (a resolve copy, an edge copy, a coverage fix-up) leaves behind for the next draw:
// the same as ExecutePendingCopy's.
static void InvalidateStateAfterCopyDraw()
{
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    InvalidatePushedRootAddresses();
#endif

    g_dirtyStates.viewport = true;
    g_dirtyStates.pipelineState = true;
    g_dirtyStates.scissorRect = true;

    if (g_backend != Backend::D3D12)
    {
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
        if (!(g_constantsUbo && g_switchRenderer.copyKeepsVertexConstants))
#endif
        g_dirtyStates.vertexShaderConstants = true; // The push constant call invalidates vertex shader constants.
        g_dirtyStates.depthBias = true; // Static depth bias in copy pipeline invalidates dynamic depth bias.
    }
}

// The copy_color pipeline of a format, as ExecutePendingCopy makes it (the edge copies use it inside the draw's pass).
static RenderPipeline* GetCopyColorPipeline(RenderFormat format)
{
    auto& copyColorPipeline = g_copyColorPipelines[format];
    if (copyColorPipeline == nullptr)
    {
        RenderGraphicsPipelineDesc desc;
        desc.pipelineLayout = g_pipelineLayout.get();
        desc.vertexShader = g_copyShader.get();
        desc.pixelShader = g_copyColorShader.get();
        desc.renderTargetFormat[0] = format;
        desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
        desc.renderTargetCount = 1;
        copyColorPipeline = g_device->createGraphicsPipeline(desc);
    }

    return copyColorPipeline.get();
}

static void FinishEdgePixels(const GuestSurface* surface)
{
    auto& pending = g_pendingEdgePixels;
    if (pending.mode == PendingEdgePixels::Mode::NONE)
        return;

    auto& commandList = g_commandLists[g_frame];
    if (pending.mode == PendingEdgePixels::Mode::CLEAR)
    {
        commandList->clearColor(0, pending.color, pending.edges.rects, pending.edges.count);
    }
    else
    {
        commandList->setPipeline(GetCopyColorPipeline(pending.format));
        commandList->setViewports(RenderViewport(0.0f, 0.0f, float(surface->width), float(surface->height), 0.0f, 1.0f));
        commandList->setGraphicsPushConstants(0, &pending.sourceDescriptor, 0, sizeof(uint32_t));
        for (uint32_t i = 0; i < pending.edges.count; i++)
        {
            commandList->setScissors(pending.edges.rects[i]);
            commandList->drawInstanced(CopyTriangleVertexCount(), 1, 0, 0);
        }

        // The draw sets its own state again (as after a copy).
        InvalidateStateAfterCopyDraw();
    }

    pending.mode = PendingEdgePixels::Mode::NONE;
}

// [Switch] SwitchSkipOverwrittenClears. A colour clear waits for the next command. When that is a draw that replaces
// every pixel of the cleared target (DrawReplacesTarget), the clear is not made: only the edge pixels the draw may leave
// get its colour (FinishEdgePixels). Any other command makes it first, exactly as it would have been made
// (IssueDeferredClear); only commands that neither read nor write images, bind targets nor record GPU work
// (KeepsDeferredClear) are processed before it. SetTexture, which may make an owed copy, makes the clear first then.
struct DeferredClear
{
    bool pending = false;
    GuestSurface* surface = nullptr;
    RenderColor color;
};

static DeferredClear g_deferredClear;

static bool KeepsDeferredClear(RenderCommandType type)
{
    switch (type)
    {
    case RenderCommandType::SetRenderState:
    case RenderCommandType::SetSamplerState:
    case RenderCommandType::SetTexture:
    case RenderCommandType::SetViewport:
    case RenderCommandType::SetScissorRect:
    case RenderCommandType::SetBooleans:
    case RenderCommandType::SetVertexShaderConstants:
    case RenderCommandType::SetPixelShaderConstants:
    case RenderCommandType::SetVertexDeclaration:
    case RenderCommandType::SetVertexShader:
    case RenderCommandType::SetPixelShader:
    case RenderCommandType::SetStreamSource:
    case RenderCommandType::SetIndices:
    case RenderCommandType::AddPipeline:
    case RenderCommandType::SetConditionalRendering: // State only (a spec bit and a shared constant).
    case RenderCommandType::SetClipPlane:
    case RenderCommandType::SignalFence:
    case RenderCommandType::ExecuteCommandBatch:     // Its commands decide one by one.
    case RenderCommandType::DrawPrimitive:           // They decide in FlushRenderStateForRenderThread.
    case RenderCommandType::DrawIndexedPrimitive:
    case RenderCommandType::DrawPrimitiveUP:
        return true;
    default:
        return false;
    }
}

static void IssueDeferredClear()
{
    auto& clear = g_deferredClear;
    if (!clear.pending)
        return;

    clear.pending = false;
    FrameLogClearOutcome("made (it waited for the next command)", clear.surface);

    // The render target is still the cleared surface: changing it is not a command that keeps the clear. The depth
    // buffer is bound with it, as ProcClear would have, while it is still a depth attachment of the same size (which
    // framebuffer is bound for a colour clear changes nothing it writes).
    GuestSurface* surface = clear.surface;
    GuestSurface* depthStencil = g_depthStencil;
    if (depthStencil != nullptr && (depthStencil->layout != RenderTextureLayout::DEPTH_WRITE ||
        depthStencil->width != surface->width || depthStencil->height != surface->height))
    {
        depthStencil = nullptr;
    }

    AddBarrier(surface, RenderTextureLayout::COLOR_WRITE);
    FlushBarriers();
    SetFramebuffer(surface, depthStencil, true);
    g_commandLists[g_frame]->clearColor(0, clear.color);
}

// Whether the draw being flushed replaces every pixel of `surface` (its render target) but `edges` with values that do
// not depend on the old ones.
static bool DrawReplacesTarget(GuestSurface* surface, GuestSurface* depthStencil, EdgePixels& edges)
{
    const auto& state = g_pipelineState;
    const uint32_t channels = FormatChannelMask(surface->format);
    if (depthStencil != nullptr || channels == 0 || (state.colorWriteEnable & channels) != channels ||
        (state.alphaBlendEnable && !BlendIgnoresDestination(state, surface->format)) ||
        surface->sampleCount != RenderSampleCount::COUNT_1 || state.sampleCount != RenderSampleCount::COUNT_1)
    {
        return false;
    }

    PipelineState sanitized = state;
    sanitized.keptAttachments = 0;
    SanitizePipelineState(sanitized);
    return IsExactFullScreenDraw(sanitized, surface, edges);
}

// At the start of FlushRenderStateForRenderThread.
static void ResolveDeferredClear(GuestSurface* renderTarget, GuestSurface* depthStencil)
{
    EdgePixels edges;
    if (renderTarget == g_deferredClear.surface && DrawReplacesTarget(renderTarget, depthStencil, edges))
    {
        g_deferredClear.pending = false;
        if (edges.count != 0)
        {
            g_pendingEdgePixels.mode = PendingEdgePixels::Mode::CLEAR;
            g_pendingEdgePixels.edges = edges;
            g_pendingEdgePixels.color = g_deferredClear.color;
        }

        g_resolveStats.clearsSkipped++;
        FrameLogClearOutcome(edges.count != 0 ? "not made: the draw overwrites the target but its edges, which get the colour" :
            "not made: the draw overwrites the target", renderTarget);
    }
    else
    {
        IssueDeferredClear();
    }
}

// [Switch] SwitchSkipNoOpDraws: a draw that writes no colour channel (no render target or an empty mask), no depth (no
// depth buffer, depth off or depth writes off), no stencil (StencilMayBeWritten) and is no conditional survey (whose
// pixel shader writes the survey's counter; the translated shaders write no memory) leaves every image and buffer as it
// was. It is not sent: the state it set stays dirty for the next draw. Its FlushRenderStateForRenderThread would also
// have made the pending copies of its depth buffer (which retarget the slots of their textures): only a draw whose
// copies can wait is skipped, and they become owed (MarkCopiesOwed); and none of the array or MSAA copies a slot needs
// may be waiting (g_pendingResolves). The barriers it would have flushed stay pending, as between draws.
static bool IsNoOpDraw()
{
    const auto& state = g_pipelineState;
    if (state.enableConditionalSurvey || (g_renderTarget != nullptr && state.colorWriteEnable != 0) ||
        (g_depthStencil != nullptr && ((state.zEnable && state.zWriteEnable) || StencilMayBeWritten(state))) ||
        !g_pendingResolves.empty())
    {
        return false;
    }

    GuestSurface* depthStencil = (state.zEnable || state.stencilEnable) ? g_depthStencil : nullptr;
    if (depthStencil != nullptr && !depthStencil->destinationTextures.empty())
    {
        if (!CanDeferCopies(depthStencil))
            return false;

        MarkCopiesOwed(depthStencil);
    }

    g_resolveStats.noOpDraws++;
    return true;
}

// [Switch] SwitchSkipRestoreDraws. On the Xbox 360, a render target lives in EDRAM, which other targets reuse: to draw
// into it again after a resolve, the game first copies the resolved texture back into it. Here a surface keeps its own
// image, and while a resolve is pending the texture's slot reads that image itself. A draw that copies such a texture
// back into its own surface (SHADER_FLAG_PIXEL_COPY: the pixel shader outputs the fetched texel) writes every pixel it
// covers with the value the pixel holds, so it is skipped, and with it the copies its FlushRenderStateForRenderThread
// would have made first (they become owed, and the slots holding the textures read the surface as they would read the
// textures after the copies: SurfaceSamplesLikeTexture, SetSurfaceAsTexture). Exact when:
// - each pixel reads its own texel: point filtering and texture coordinates at every vertex equal to its framebuffer
//   position divided by the surface's size (the vertex shader passes both through, w = 1, so the interpolation is
//   linear; the tolerance is 1/64 of a texel against the half texel that would be needed to reach another one);
// - the value survives the round trip: the slot reads the surface's own image in its own format;
// - nothing else is written: no blending, no depth or stencil write (discarded or clipped pixels keep their value
//   anyway), and the draw needs none of the other copies its flush would have made.
// Depth copies (SHADER_FLAG_DEPTH_COPY) are not skipped: the port's depth textures read through swizzled views.
static bool IsIdentityRestore()
{
#ifdef MARATHON_RECOMP_SWITCH_SHADER_FLAGS
    const auto& draw = g_currentDrawVertices;
    const auto& state = g_pipelineState;
    const GuestShader* vertexShader = state.vertexShader;
    const GuestShader* pixelShader = state.pixelShader;
    if (draw.data == nullptr || pixelShader == nullptr || pixelShader->shaderCacheEntry == nullptr ||
        (pixelShader->shaderCacheEntry->flags & SHADER_FLAG_PIXEL_COPY) == 0 || state.enableConditionalSurvey)
    {
        return false;
    }

    const uint32_t vertexFlags = vertexShader != nullptr && vertexShader->shaderCacheEntry != nullptr ? vertexShader->shaderCacheEntry->flags : 0;
    if ((vertexFlags & POSITION_PASS_THROUGH_FLAGS) == 0 || (vertexFlags & SHADER_FLAG_TEXCOORD_PASS_THROUGH) == 0)
        return false;

    GuestSurface* target = state.colorWriteEnable != 0 ? g_renderTarget : nullptr;
    GuestSurface* depthStencil = (state.zEnable || state.stencilEnable) ? g_depthStencil : nullptr;
    if (target == nullptr || target->sampleCount != RenderSampleCount::COUNT_1 || state.sampleCount != RenderSampleCount::COUNT_1 ||
        state.alphaBlendEnable || state.enableAlphaToCoverage || !g_pendingResolves.empty() ||
        (depthStencil != nullptr && ((state.zEnable && state.zWriteEnable) || StencilMayBeWritten(state))))
    {
        return false;
    }

    const uint32_t slot = (pixelShader->shaderCacheEntry->flags >> SHADER_FLAG_COPY_SLOT_SHIFT) & SHADER_FLAG_COPY_SLOT_MASK;
    const GuestTexture* texture = slot < std::size(g_textures) ? g_textures[slot] : nullptr;
    if (texture == nullptr || texture->sourceSurface != target || g_sharedConstants.texture2DIndices[slot] != target->descriptorIndex)
        return false;

    // Every pending texture of the target must read the target's descriptor as it reads its own after the copy.
    for (const auto [pendingTexture, slice] : target->destinationTextures)
    {
        if (!SurfaceSamplesLikeTexture(target, pendingTexture, slice))
            return false;
    }

    // The depth buffer's copies the flush would have made must be able to wait.
    if (depthStencil != nullptr && !depthStencil->destinationTextures.empty() && !CanDeferCopies(depthStencil))
        return false;

    const RenderSamplerDesc& sampler = g_samplerDescs[slot];
    if (sampler.minFilter != RenderFilter::NEAREST || sampler.magFilter != RenderFilter::NEAREST || sampler.anisotropyEnabled)
        return false;

    // POSITION (location 0) and TEXCOORD0 (location 13, CreateVertexDeclarationWithoutAddRef), 32-bit floats in stream 0.
    const RenderInputElement* position;
    const RenderInputElement* texcoord;
    const uint32_t positionComponents = GetFloatElement(0, position);
    const uint32_t texcoordComponents = GetFloatElement(13, texcoord);
    if (positionComponents == 0 || texcoordComponents == 0 || position->alignedByteOffset + positionComponents * 4 > draw.stride ||
        texcoord->alignedByteOffset + 8 > draw.stride || draw.count == 0 || draw.count > 64 ||
        (state.vertexDeclaration->swappedTexcoords & 1) != 0) // Only 16-bit texture coordinates are swapped.
    {
        return false;
    }

    RenderViewport viewport;
    RenderRect scissor;
    if (!GetDrawViewport(viewport, scissor))
        return false;

    // Framebuffer coordinates as in GetFullScreenCoverage (the draw's framebuffer has the target as its colour
    // attachment, whose size gives the half-pixel offset).
    const double offsetX = 1.0f / float(target->width);
    const double offsetY = -1.0f / float(target->height);
    const double width = double(target->width);
    const double height = double(target->height);
    constexpr double TOLERANCE = 1.0 / 64.0; // Texels.

    for (uint32_t i = 0; i < draw.count; i++)
    {
        float x, y;
        if (!LoadPassThroughPosition(vertexFlags, position, positionComponents, i, x, y))
            return false;

        const uint8_t* vertex = draw.data + size_t(i) * draw.stride + texcoord->alignedByteOffset;
        const double u = LoadGuestFloat(vertex);
        const double v = LoadGuestFloat(vertex + 4);

        const double fx = double(viewport.x) + (double(x) + offsetX + 1.0) * double(viewport.width) * 0.5;
        const double fy = double(viewport.y) + (1.0 - (double(y) + offsetY)) * double(viewport.height) * 0.5;

        if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(u) || !std::isfinite(v) ||
            std::abs(u * width - fx) > TOLERANCE || std::abs(v * height - fy) > TOLERANCE)
        {
            return false;
        }
    }

    // Skipped: the copies the flush would have made become owed; slots holding the target's textures read it as they
    // would read the textures after the copies.
    MarkCopiesOwed(target);
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (g_textures[i] != nullptr && g_textures[i]->sourceSurface == target)
            SetSurfaceAsTexture(i, target);
    }

    if (depthStencil != nullptr && !depthStencil->destinationTextures.empty())
        MarkCopiesOwed(depthStencil);

    g_resolveStats.restoresSkipped++;
    return true;
#else
    return false;
#endif
}

// Called for the render target of a draw, before its pending copies are made.
static bool TryCoverageHandOver(GuestSurface* surface, GuestSurface* depthStencil)
{
    if (!g_switchRenderer.coverageHandOver || surface == nullptr || surface->destinationTextures.empty())
        return false;

    const auto& state = g_pipelineState;
    if (state.enableConditionalSurvey || state.pixelShader == nullptr || state.sampleCount != RenderSampleCount::COUNT_1)
    {
        g_resolveStats.coverageMisses[COVERAGE_MISS_SHADER]++;
        return false;
    }

    const uint32_t channels = FormatChannelMask(surface->format);
    if ((state.alphaBlendEnable && !BlendIgnoresDestination(state, surface->format)) || channels == 0 ||
        (state.colorWriteEnable & channels) != channels)
    {
        g_resolveStats.coverageMisses[COVERAGE_MISS_BLEND]++;
        return false;
    }

    if (depthStencil != nullptr)
    {
        g_resolveStats.coverageMisses[COVERAGE_MISS_DEPTH]++;
        return false;
    }

    if (surface->destinationTextures.size() != 1)
    {
        g_resolveStats.coverageMisses[COVERAGE_MISS_TEXTURES]++;
        return false;
    }

    const auto [texture, slice] = *surface->destinationTextures.begin();
    if (texture->type != ResourceType::Texture || slice != 0)
    {
        g_resolveStats.coverageMisses[COVERAGE_MISS_TEXTURES]++;
        return false;
    }

    if (!CanHandOver(surface, texture, slice))
    {
        g_resolveStats.coverageMisses[COVERAGE_MISS_IMAGE]++;
        return false;
    }

    PipelineState variant = state;
    variant.keptAttachments = 0;
    SanitizePipelineState(variant);

    // [Switch] SwitchExactCoverage: a draw that writes every pixel of the target needs neither the marks nor the fix-up.
    // The few edge pixels whose coverage the rasterizer decides get the old contents first (FinishEdgePixels); the draw
    // then overwrites those it covers, as it would after the copy.
    EdgePixels edges;
    if (g_switchRenderer.exactCoverage && IsExactFullScreenDraw(variant, surface, edges))
    {
        const uint32_t sourceDescriptor = HandOverSurfaceImage(surface, texture);
        if (sourceDescriptor == 0)
        {
            g_resolveStats.coverageMisses[COVERAGE_MISS_IMAGE]++;
            return false;
        }

        AddBarrier(texture, RenderTextureLayout::SHADER_READ);

        if (edges.count != 0)
        {
            g_pendingEdgePixels.mode = PendingEdgePixels::Mode::COPY;
            g_pendingEdgePixels.edges = edges;
            g_pendingEdgePixels.sourceDescriptor = sourceDescriptor;
            g_pendingEdgePixels.format = surface->format;
        }

        g_resolveStats.exactHandOvers++;
        g_resolveStats.coverageHandOvers++;
        return true;
    }

    variant.coverageStencil = 1;
    const XXH64_hash_t hash = XXH3_64bits(&variant, sizeof(variant));
    auto findResult = g_pipelines.find(hash);
    if (findResult == g_pipelines.end() || findResult->second == nullptr)
    {
        RequestPipelineVariant(variant, hash);
        g_resolveStats.coverageMisses[COVERAGE_MISS_VARIANT]++;
        return false;
    }

    CoverageStencil* stencil = GetCoverageStencil(surface->width, surface->height);
    if (stencil == nullptr)
    {
        g_resolveStats.coverageMisses[COVERAGE_MISS_IMAGE]++;
        return false;
    }

    const uint32_t sourceDescriptor = HandOverSurfaceImage(surface, texture);
    if (sourceDescriptor == 0)
    {
        g_resolveStats.coverageMisses[COVERAGE_MISS_IMAGE]++;
        return false;
    }

    // Flushed with the draw's own barriers: the texture's image is read by the fix-up (and maybe the draw).
    AddBarrier(texture, RenderTextureLayout::SHADER_READ);
    if (stencil->layout != RenderTextureLayout::DEPTH_WRITE)
    {
        g_barrierMap[stencil->texture.get()] = RenderTextureLayout::DEPTH_WRITE;
        stencil->layout = RenderTextureLayout::DEPTH_WRITE;
    }

    // A value no pixel of the buffer holds since its last clear.
    g_coverageFixup.clearStencil = stencil->nextReference == 0 || stencil->nextReference > 0xFF;
    if (g_coverageFixup.clearStencil)
        stencil->nextReference = 1;
    g_coverageFixup.reference = stencil->nextReference++;

    GetCoverageFixupPipeline(surface->format);

    g_coverageFixup.pending = true;
    g_coverageFixup.surface = surface;
    g_coverageFixup.stencil = stencil;
    g_coverageFixup.sourceDescriptor = sourceDescriptor;
    g_coverageFixup.drawPipeline = findResult->second.get();
    g_resolveStats.coverageHandOvers++;
    return true;
}

// In place of SetFramebuffer for the draw of a coverage hand-over, once every other pending copy is made.
static void BindCoverageFramebuffer()
{
    auto& commandList = g_commandLists[g_frame];
    GuestSurface* surface = g_coverageFixup.surface;
    RenderFramebuffer* framebuffer = GetCoverageFramebuffer(surface->texture, g_coverageFixup.stencil);
    if (g_framebuffer != framebuffer)
    {
        PassProfilerFramebuffer(surface, nullptr);
        FrameLogPass(surface, nullptr, "coverage hand-over, with its stencil marks");
        g_resolveStats.framebufferChanges++;
        commandList->setFramebuffer(framebuffer);
        g_framebuffer = framebuffer;
        g_framebufferBindCount++;
    }

    // What SetFramebuffer does for a new target (the same size as the surface's own framebuffer).
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetX, 1.0f / float(framebuffer->getWidth()));
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetY, -1.0f / float(framebuffer->getHeight()));
    g_dirtyStates.renderTargetAndDepthStencil = false;
    g_boundFramebufferDepthReadOnly = false;

    if (g_coverageFixup.clearStencil)
        commandList->clearDepthStencil(false, true, 0.0f, 0);
}

// After every draw: the fix-up of a coverage hand-over (see TryCoverageHandOver).
static void FinishCoverageFixup()
{
    if (!g_coverageFixup.pending)
        return;

    g_coverageFixup.pending = false;

    auto& commandList = g_commandLists[g_frame];
    GuestSurface* surface = g_coverageFixup.surface;
    commandList->setPipeline(GetCoverageFixupPipeline(surface->format));
    commandList->setStencilReference(g_coverageFixup.reference);
    commandList->setViewports(RenderViewport(0.0f, 0.0f, float(surface->width), float(surface->height), 0.0f, 1.0f));
    commandList->setScissors(RenderRect(0, 0, surface->width, surface->height));
    commandList->setGraphicsPushConstants(0, &g_coverageFixup.sourceDescriptor, 0, sizeof(uint32_t));
    commandList->drawInstanced(CopyTriangleVertexCount(), 1, 0, 0);

    // The next draw goes back to the target's own framebuffer and state.
    g_dirtyStates.renderTargetAndDepthStencil = true;
    InvalidateStateAfterCopyDraw();
}

// [Switch] SwitchReadOnlyDepthSampling. A draw that tests against a depth surface without writing its depth or stencil,
// while slots hold textures whose copy from that surface is pending: the port copied them before the draw (it binds the
// surface) and the draw read the textures. Vulkan lets an image be a read-only depth attachment and sampled at once, so
// the copies become owed instead (the surface does not change before they are made, see g_owedCopies), the surface is
// attached read-only (DEPTH_STENCIL_READ_ONLY_OPTIMAL, loaded and stored), and the slots read it through views made like
// the textures' own views (their component mapping: the port's depth textures read R as RRR1), which return what the
// textures would hold after the copy (copy_depth writes the same 32-bit depth). The depth and stencil tests read the
// same values. Only when every pending texture is a single-level 2D texture of the surface's format and size, and no
// array or MSAA copy of the surface waits (g_pendingResolves). The views' descriptors say DEPTH_READ, and the slots keep
// them only while the surface stays in that layout: everything that moves it out (a draw that writes it or cannot
// attach it read-only, a clear, a copy from it: owed copies are made all together, a hand-over) makes the owed copies
// first, which point the slots at the textures again.
static uint32_t GetDepthReadView(GuestSurface* surface, const RenderComponentMapping& mapping)
{
    for (const auto& depthReadView : surface->depthReadViews)
    {
        const auto& m = depthReadView.componentMapping;
        if (m.r == mapping.r && m.g == mapping.g && m.b == mapping.b && m.a == mapping.a)
            return depthReadView.descriptorIndex;
    }

    const uint32_t descriptorIndex = g_textureDescriptorAllocator.allocate();
    if (descriptorIndex < TEXTURE_DESCRIPTOR_NULL_COUNT)
        return 0;

    RenderTextureViewDesc viewDesc;
    viewDesc.format = surface->format;
    viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    viewDesc.mipLevels = 1;
    viewDesc.componentMapping = mapping;

    auto& depthReadView = surface->depthReadViews.emplace_back();
    depthReadView.componentMapping = mapping;
    depthReadView.view = surface->texture->createTextureView(viewDesc);
    depthReadView.descriptorIndex = descriptorIndex;
    SetTextureDescriptor(descriptorIndex, surface->texture, surface->width, surface->height, RenderTextureLayout::DEPTH_READ,
        depthReadView.view.get(), 1);

    return descriptorIndex;
}

static bool CanSampleDepthReadOnly(GuestSurface* surface)
{
    const auto& state = g_pipelineState;
    if ((state.zEnable && state.zWriteEnable) || StencilMayBeWritten(state) || surface->sampleCount != RenderSampleCount::COUNT_1 ||
        surface == g_backBuffer || g_pendingResolves.find(surface) != g_pendingResolves.end())
    {
        uint32_t reason = READ_ONLY_REFUSED_PENDING;
        if (state.zEnable && state.zWriteEnable)
            reason = READ_ONLY_REFUSED_DEPTH_WRITE;
        else if (StencilMayBeWritten(state))
            reason = READ_ONLY_REFUSED_STENCIL_WRITE;
        else if (surface->sampleCount != RenderSampleCount::COUNT_1 || surface == g_backBuffer)
            reason = READ_ONLY_REFUSED_MSAA;
        g_resolveStats.readOnlyRefused[reason]++;
        return false;
    }

    for (const auto [texture, slice] : surface->destinationTextures)
    {
        if (!texture->handOverCapable || slice != 0 || texture->mipLevels != 1 || texture->format != surface->format ||
            texture->width != surface->width || texture->height != surface->height || texture->patchedTexture != nullptr ||
            GetDepthReadView(surface, texture->viewDesc.componentMapping) == 0)
        {
            uint32_t reason = READ_ONLY_REFUSED_VIEW;
            if (!texture->handOverCapable || slice != 0 || texture->mipLevels != 1 || texture->patchedTexture != nullptr)
                reason = READ_ONLY_REFUSED_TEXTURE;
            else if (texture->format != surface->format)
                reason = READ_ONLY_REFUSED_FORMAT;
            else if (texture->width != surface->width || texture->height != surface->height)
                reason = READ_ONLY_REFUSED_SIZE;
            g_resolveStats.readOnlyRefused[reason]++;
            return false;
        }
    }

    return true;
}

static void SampleDepthReadOnly(GuestSurface* surface)
{
    MarkCopiesOwed(surface);

    // What SetTextureInRenderThread sets for a texture with a 2D view, with a view of the surface made like it.
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        const GuestTexture* texture = g_textures[i];
        if (texture != nullptr && texture->sourceSurface == surface)
        {
            const uint32_t descriptorIndex = GetDepthReadView(surface, texture->viewDesc.componentMapping);
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DIndices[i], descriptorIndex);
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DArrayIndices[i], descriptorIndex);
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.textureCubeIndices[i], uint32_t(TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE));
#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
            UpdateTextureSlotTables(i);
#endif
        }
    }

    g_resolveStats.readOnlyDepthDraws++;
}

// [Switch] SwitchStableFramebuffers. The port binds, for each draw, the render target only if it writes colour and the
// depth buffer only if it tests depth or stencil, so a draw that turns one of them off ended the render pass (and began
// it again for the next draw) just to leave the other attachment out. When the framebuffer bound last already holds
// both, with the other one the same size (render area and half-pixel offset unchanged), and the attachment the draw
// leaves out is still the current target in its attachment layout (so no slot samples it: sampling moves it to a read
// layout first), the framebuffer stays bound; the pipeline gets that attachment's format with nothing written to it
// (colour mask 0 and blending off, or depth and stencil tests off; SanitizePipelineState, KEPT_*_ATTACHMENT). Nothing
// reads or writes the kept attachment: every fragment and every other attachment's value is the same.
static uint8_t GetKeptAttachments(GuestSurface* renderTarget, GuestSurface* depthStencil, bool depthReadOnly,
    GuestSurface*& attachTarget, GuestSurface*& attachDepth)
{
    const auto& bound = g_boundTargets;
    if (g_framebuffer == nullptr || bound.framebuffer != g_framebuffer || bound.bindCount != g_framebufferBindCount ||
        g_boundFramebufferDepthReadOnly != depthReadOnly || bound.renderTarget == nullptr || bound.depthStencil == nullptr ||
        bound.renderTarget->width != bound.depthStencil->width || bound.renderTarget->height != bound.depthStencil->height ||
        bound.renderTarget->sampleCount != bound.depthStencil->sampleCount)
    {
        return 0;
    }

    // Colour writes off: the port binds (nullptr, depthStencil).
    if (renderTarget == nullptr && depthStencil == bound.depthStencil && bound.renderTarget == g_renderTarget &&
        bound.renderTarget->layout == RenderTextureLayout::COLOR_WRITE)
    {
        attachTarget = bound.renderTarget;
        return KEPT_COLOR_ATTACHMENT;
    }

    // Depth and stencil tests off: the port binds (renderTarget, nullptr).
    if (depthStencil == nullptr && renderTarget == bound.renderTarget && bound.depthStencil == g_depthStencil &&
        bound.depthStencil->layout == RenderTextureLayout::DEPTH_WRITE)
    {
        attachDepth = bound.depthStencil;
        return KEPT_DEPTH_ATTACHMENT;
    }

    return 0;
}
#endif

static void ProcClear(const RenderCommand& cmd)
{
    const auto& args = cmd.clear;
#if defined(__SWITCH__)
    FrameLogClear(args.flags, args.color, args.z, args.stencil);
#endif

    GuestSurface* resolveTarget = g_renderTarget;
    GuestSurface* resolveDepth = g_depthStencil;

    const bool clearDepth = (args.flags & D3DCLEAR_ZBUFFER) != 0;
    bool clearStencil = (args.flags & D3DCLEAR_STENCIL) != 0;
    uint32_t stencilValue = args.stencil;

#if defined(__SWITCH__)
    // [Switch] SwitchUniformStencilClears. A depth clear of a surface whose every stencil texel holds the same value
    // (stencilKnown: a clear of the whole stencil, and nothing since that may write it) also clears the stencil to that
    // value: the same texels, as one clear of the whole image.
    if (g_switchRenderer.uniformStencilClears && g_depthStencil != nullptr && clearDepth && !clearStencil && g_depthStencil->stencilKnown)
    {
        clearStencil = true;
        stencilValue = g_depthStencil->stencilValue;
        g_resolveStats.stencilClearsWidened++;
    }

    // [Switch] SwitchLazyResolves: only the surfaces this clear changes need their pending copies now; the others' become
    // owed (a clear of the stencil alone leaves the depth, which is all a depth copy reads).
    if (g_switchRenderer.lazyResolves)
    {
        if ((args.flags & D3DCLEAR_TARGET) == 0 && resolveTarget != nullptr && !resolveTarget->destinationTextures.empty() &&
            CanDeferCopies(resolveTarget))
        {
            MarkCopiesOwed(resolveTarget);
            resolveTarget = nullptr;
        }

        if (!clearDepth && resolveDepth != nullptr && !resolveDepth->destinationTextures.empty() && CanDeferCopies(resolveDepth))
        {
            MarkCopiesOwed(resolveDepth);
            resolveDepth = nullptr;
        }
    }

    g_resolveCopyTrigger = RESOLVE_COPY_AT_CLEAR;

    if (g_switchRenderer.resolveHandOver)
    {
        if (resolveTarget != nullptr && (args.flags & D3DCLEAR_TARGET) != 0)
            TryResolveHandOver(resolveTarget);
        if (resolveDepth != nullptr && clearDepth && clearStencil)
            TryResolveHandOver(resolveDepth);
    }
#endif

    if (PopulateBarriersForStretchRect(resolveTarget, resolveDepth))
    {
        FlushBarriers();
        ExecutePendingStretchRectCommands(resolveTarget, resolveDepth);
    }

#if defined(__SWITCH__)
    // [Switch] SwitchCascadeAdoption: a full clear of the shadow surface starts a cascade in the cascade image's next
    // layer (made there); a partial one takes the surface's content back into its own image first.
    if (g_switchRenderer.cascadeAdoption && g_depthStencil != nullptr && g_depthStencil == g_cascade.surface &&
        (clearDepth || clearStencil) && CascadeOnClear(args.flags, clearDepth, clearStencil, args.z, stencilValue))
    {
        return;
    }

    // [Switch] SwitchSkipOverwrittenClears (DeferredClear): a colour-only clear (or one whose depth part has no depth
    // buffer) of a single-sampled target waits for the next command. Its transition goes out with the next batch.
    if (g_switchRenderer.skipOverwrittenClears && g_renderTarget != nullptr && (args.flags & D3DCLEAR_TARGET) != 0 &&
        ((args.flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) == 0 || g_depthStencil == nullptr) &&
        g_renderTarget->sampleCount == RenderSampleCount::COUNT_1)
    {
        AddBarrier(g_renderTarget, RenderTextureLayout::COLOR_WRITE);
        g_deferredClear.pending = true;
        g_deferredClear.surface = g_renderTarget;
        g_deferredClear.color = RenderColor(args.color[0], args.color[1], args.color[2], args.color[3]);
        g_resolveStats.clearsDeferred++;
        FrameLogClearOutcome("waits for the next command", g_renderTarget);
        return;
    }
#endif

    AddBarrier(g_renderTarget, RenderTextureLayout::COLOR_WRITE);
    AddBarrier(g_depthStencil, RenderTextureLayout::DEPTH_WRITE);
    FlushBarriers();

    bool canClearInOnePass = (g_renderTarget == nullptr) || (g_depthStencil == nullptr) ||
        (g_renderTarget->width == g_depthStencil->width && g_renderTarget->height == g_depthStencil->height);

    if (canClearInOnePass)
    {
        SetFramebuffer(g_renderTarget, g_depthStencil, true);
    }

    auto& commandList = g_commandLists[g_frame];

    if (g_renderTarget != nullptr && (args.flags & D3DCLEAR_TARGET) != 0)
    {
        if (!canClearInOnePass) {
            SetFramebuffer(g_renderTarget, nullptr, true);
        }

        commandList->clearColor(0, RenderColor(args.color[0], args.color[1], args.color[2], args.color[3]));
    }

    if (g_depthStencil != nullptr && (clearDepth || clearStencil))
    {
        if (!canClearInOnePass) {
            SetFramebuffer(nullptr, g_depthStencil, true);
        }

        commandList->clearDepthStencil(clearDepth, clearStencil, args.z, stencilValue);

#if defined(__SWITCH__)
        // Vulkan stores the low 8 bits of the value in the S8 part.
        if (clearStencil)
        {
            g_depthStencil->stencilKnown = true;
            g_depthStencil->stencilValue = uint8_t(stencilValue);
        }
#endif
    }
}

static void SetViewport(GuestDevice* device, GuestViewport* viewport)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetViewport;
    cmd.setViewport.x = viewport->x;
    cmd.setViewport.y = viewport->y;
    cmd.setViewport.width = viewport->width;
    cmd.setViewport.height = viewport->height;
    cmd.setViewport.minDepth = viewport->minZ;
    cmd.setViewport.maxDepth = viewport->maxZ;
    EnqueueRenderCommand(cmd);

    device->viewport.x = float(viewport->x);
    device->viewport.y = float(viewport->y);
    device->viewport.width = float(viewport->width);
    device->viewport.height = float(viewport->height);
    device->viewport.minZ = viewport->minZ;
    device->viewport.maxZ = viewport->maxZ;
}

static void ProcSetViewport(const RenderCommand& cmd)
{
    const auto& args = cmd.setViewport;

    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.x, args.x);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.y, args.y);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.width, args.width);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.height, args.height);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.minDepth, args.minDepth);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.maxDepth, args.maxDepth);
    
    uint32_t specConstants = g_pipelineState.specConstants;
    if (args.minDepth > args.maxDepth)
        specConstants |= SPEC_CONSTANT_REVERSE_Z;
    else 
        specConstants &= ~SPEC_CONSTANT_REVERSE_Z;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);

    g_dirtyStates.scissorRect |= g_dirtyStates.viewport;
}

static void SetTexture(GuestDevice* device, uint32_t index, GuestTexture* texture) 
{
    // printf("SetTexture: %x %d %x\n", device, index, texture);

    if (Config::IsControllerIconsPS3() && texture != nullptr && texture->patchedTexture != nullptr)
        texture = texture->patchedTexture.get();

    RenderCommand cmd;
    cmd.type = RenderCommandType::SetTexture;
    cmd.setTexture.index = index;
    cmd.setTexture.texture = texture;
    EnqueueRenderCommand(cmd);
}

static void SetTextureInRenderThread(uint32_t index, GuestTexture* texture)
{
    AddBarrier(texture, RenderTextureLayout::SHADER_READ);

    auto viewDimension = texture != nullptr ? texture->viewDimension : RenderTextureViewDimension::UNKNOWN;

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DIndices[index],
        viewDimension == RenderTextureViewDimension::TEXTURE_2D ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D);

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DArrayIndices[index], texture != nullptr &&
        viewDimension == RenderTextureViewDimension::TEXTURE_2D ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D_ARRAY);

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.textureCubeIndices[index], texture != nullptr &&
        viewDimension == RenderTextureViewDimension::TEXTURE_CUBE ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE);

#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
    UpdateTextureSlotTables(index);
#endif
}

static void SetSurface(uint32_t index, GuestSurface* surface)
{
    AddBarrier(surface, RenderTextureLayout::SHADER_READ);

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DIndices[index], surface->descriptorIndex);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DArrayIndices[index], uint32_t(TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D_ARRAY));
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.textureCubeIndices[index], uint32_t(TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE));

#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
    UpdateTextureSlotTables(index);
#endif
}

static void ProcSetTexture(const RenderCommand& cmd)
{
    const auto& args = cmd.setTexture;

    // If a pending copy operation is detected, set the source surface. The indices will be fixed later if flushing is necessary.
    bool shouldSetTexture = true;
#if defined(__SWITCH__)
    // SwitchCascadeAdoption (perf8): binding the cascaded shadow map reads nothing; its held slices become its content
    // at the first draw that samples it (CascadeAdoptForDraw).

    if (args.texture != nullptr && args.texture->sourceSurface != nullptr)
    {
        // A copy the port had made already: it is made now, and the slot reads the texture as the port's did (an owed
        // copy, see g_owedCopies). One kept over a Present reads the surface's descriptor, which samples exactly like
        // the texture's (SwitchKeepResolvesPending, SurfaceSamplesLikeTexture), in the state the port gave the slot.
        if (args.texture->copyOwed)
        {
            MakeOwedCopies(args.texture->sourceSurface, RESOLVE_COPY_OTHER);
        }
        else if (args.texture->pendingCarried)
        {
            SetSurfaceAsTexture(args.index, args.texture->sourceSurface);
            shouldSetTexture = false;
        }
    }

    if (shouldSetTexture && args.texture != nullptr && args.texture->sourceSurface != nullptr)
#else
    if (args.texture != nullptr && args.texture->sourceSurface != nullptr)
#endif
    {
        // TODO: Render depth directly to slice and avoid copy
        // MSAA surfaces or surface-to-array need to be resolved and cannot be used directly.
        if (args.texture->sourceSurface->sampleCount != RenderSampleCount::COUNT_1 ||
            args.texture->type == ResourceType::ArrayTexture)
        {
            g_pendingResolves.emplace(args.texture->sourceSurface);
        }
        else
        {
            SetSurface(args.index, args.texture->sourceSurface);
            shouldSetTexture = false;
        }
    }
    
    if (shouldSetTexture)
        SetTextureInRenderThread(args.index, args.texture);
    
    g_textures[args.index] = args.texture;
}

static void SetScissorRect(GuestDevice* device, GuestRect* rect)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetScissorRect;
    cmd.setScissorRect.top = rect->top;
    cmd.setScissorRect.left = rect->left;
    cmd.setScissorRect.bottom = rect->bottom;
    cmd.setScissorRect.right = rect->right;
    EnqueueRenderCommand(cmd);
}

static void ProcSetScissorRect(const RenderCommand& cmd)
{
    const auto& args = cmd.setScissorRect;

    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.top, args.top);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.left, args.left);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.bottom, args.bottom);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.right, args.right);
}

#if defined(__SWITCH__)
// [Switch] The analyses of a translated shader's SPIR-V the pipelines use (GuestShader, video.h), stored before the
// shader is first used (GetOrLinkShader, under its mutex; readers load them with acquire).
static void AnalyseTranslatedShader(GuestShader* guestShader, const uint8_t* spirv, size_t spirvSize)
{
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    // Every constant read of a translated shader that declares set 5 follows SPEC_CONSTANT_CONSTANTS_UBO
    // (shader_common.h); a shader of an older cache reads them through the pointers only (no set 5).
    guestShader->constantsThroughUbo.store(switch_spirv::ReadsConstantsThroughUbo(spirv, spirvSize, CONSTANTS_UBO_SET_INDEX),
        std::memory_order_release);
#endif

#ifdef MARATHON_RECOMP_SWITCH_SHADER_SPECIALIZATION
    if (guestShader->type != ResourceType::PixelShader)
        return;

    const uint32_t specConstantsMask = guestShader->shaderCacheEntry->specConstantsMask;
    const uint32_t flags = guestShader->shaderCacheEntry->flags;

    if (g_switchRenderer.depthOnlyWithoutPixelShader)
    {
        // The kills a pipeline without colour target disables through its spec constants (PixelShaderRemovedFromPipeline
        // requires those bits clear): the alpha test's clip() and the conditional rendering prologue's discard, which
        // every translated Marathon pixel shader starts with, plus the blend skip's discards, which only a pipeline
        // with a colour target enables. A Xenos kill instruction is never removable: XenosRecomp flags it
        // (SHADER_FLAG_PIXEL_KILL without _CONDITIONAL), and it would be one more kill than these anyway.
        uint32_t allowedKills = ((specConstantsMask & SPEC_CONSTANT_ALPHA_TEST) != 0 ? 1 : 0) +
            ((specConstantsMask & SPEC_CONSTANT_CONDITIONAL_RENDERING) != 0 ? 1 : 0);
#if defined(SPEC_CONSTANT_BLEND_SKIP_ALPHA) && defined(SPEC_CONSTANT_BLEND_SKIP_ZERO)
        allowedKills += ((specConstantsMask & SPEC_CONSTANT_BLEND_SKIP_ALPHA) != 0 ? 1 : 0) +
            ((specConstantsMask & SPEC_CONSTANT_BLEND_SKIP_ZERO) != 0 ? 1 : 0);
#endif
        const bool xenosKill = (flags & SHADER_FLAG_PIXEL_KILL) != 0 && (flags & SHADER_FLAG_PIXEL_KILL_CONDITIONAL) == 0;
        guestShader->removableInDepthOnlyPass.store(!xenosKill && switch_spirv::RemovableInDepthOnlyPass(spirv, spirvSize, allowedKills),
            std::memory_order_release);
    }

    if (g_switchRenderer.trimVertexOutputs)
        guestShader->inputLocationsRead.store(switch_spirv::InputLocationsRead(spirv, spirvSize), std::memory_order_release);
#endif
}
#endif

static RenderShader* GetOrLinkShader(GuestShader* guestShader, uint32_t specConstants)
{
    if (g_backend != Backend::D3D12 ||
        guestShader->shaderCacheEntry == nullptr || 
        guestShader->shaderCacheEntry->specConstantsMask == 0)
    {
        std::lock_guard lock(guestShader->mutex);

        if (guestShader->shader == nullptr)
        {
            assert(guestShader->shaderCacheEntry != nullptr);

            switch (g_backend) {
            case Backend::VULKAN:
            {
                auto compressedSpirvData = g_shaderCache.get() + guestShader->shaderCacheEntry->spirvOffset;

                std::vector<uint8_t> decoded(smolv::GetDecodedBufferSize(compressedSpirvData, guestShader->shaderCacheEntry->spirvSize));
                bool result = smolv::Decode(compressedSpirvData, guestShader->shaderCacheEntry->spirvSize, decoded.data(), decoded.size());
                assert(result);

                guestShader->shader = g_device->createShader(decoded.data(), decoded.size(), "shaderMain", RenderShaderFormat::SPIRV);
#if defined(__SWITCH__)
                AnalyseTranslatedShader(guestShader, decoded.data(), decoded.size());
                DrawProfilerHashSpirv(guestShader, decoded.data(), decoded.size());
#endif
                break;
            }
            case Backend::D3D12:
            {
                guestShader->shader = g_device->createShader(g_shaderCache.get() + guestShader->shaderCacheEntry->dxilOffset, 
                    guestShader->shaderCacheEntry->dxilSize, "shaderMain", RenderShaderFormat::DXIL);
                break;
            }
            case Backend::METAL:
            {
                guestShader->shader = g_device->createShader(g_shaderCache.get() + guestShader->shaderCacheEntry->airOffset,
                    guestShader->shaderCacheEntry->airSize, "shaderMain", RenderShaderFormat::METAL);
                break;
            }
            }

#ifdef _DEBUG
            guestShader->shader->setName(fmt::format("{}:{:x}", guestShader->shaderCacheEntry->filename, guestShader->shaderCacheEntry->hash));
#endif
        }

        return guestShader->shader.get();
    }

    specConstants &= guestShader->shaderCacheEntry->specConstantsMask;

    RenderShader* shader;
    {
        std::lock_guard lock(guestShader->mutex);
        shader = guestShader->linkedShaders[specConstants].get();
    }

#ifdef MARATHON_RECOMP_D3D12
    if (shader == nullptr)
    {
        static RecompMutex g_compiledSpecConstantLibraryBlobMutex;
        static ankerl::unordered_dense::map<uint32_t, ComPtr<IDxcBlob>> g_compiledSpecConstantLibraryBlobs;

        thread_local ComPtr<IDxcCompiler3> s_dxcCompiler;
        thread_local ComPtr<IDxcLinker> s_dxcLinker;
        thread_local ComPtr<IDxcUtils> s_dxcUtils;

        wchar_t specConstantsLibName[0x100];
        swprintf_s(specConstantsLibName, L"SpecConstants_%d", specConstants);

        ComPtr<IDxcBlob> specConstantLibraryBlob;
        {
            std::lock_guard lock(g_compiledSpecConstantLibraryBlobMutex);
            specConstantLibraryBlob = g_compiledSpecConstantLibraryBlobs[specConstants];
        }

        if (specConstantLibraryBlob == nullptr)
        {
            if (s_dxcCompiler == nullptr)
            {
                HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(s_dxcCompiler.GetAddressOf()));
                assert(SUCCEEDED(hr) && s_dxcCompiler != nullptr);
            }

            char libraryHlsl[0x100];
            sprintf_s(libraryHlsl, "export uint g_SpecConstants() { return %d; }", specConstants);

            DxcBuffer buffer{};
            buffer.Ptr = libraryHlsl;
            buffer.Size = strlen(libraryHlsl);

            const wchar_t* args[1];
            args[0] = L"-T lib_6_3";

            ComPtr<IDxcResult> result;
            HRESULT hr = s_dxcCompiler->Compile(&buffer, args, std::size(args), nullptr, IID_PPV_ARGS(result.GetAddressOf()));
            assert(SUCCEEDED(hr) && result != nullptr);

            hr = result->GetResult(specConstantLibraryBlob.GetAddressOf());
            assert(SUCCEEDED(hr) && specConstantLibraryBlob != nullptr);

            std::lock_guard lock(g_compiledSpecConstantLibraryBlobMutex);
            g_compiledSpecConstantLibraryBlobs.emplace(specConstants, specConstantLibraryBlob);
        }

        if (s_dxcLinker == nullptr)
        {
            HRESULT hr = DxcCreateInstance(CLSID_DxcLinker, IID_PPV_ARGS(s_dxcLinker.GetAddressOf()));
            assert(SUCCEEDED(hr) && s_dxcLinker != nullptr);
        }

        s_dxcLinker->RegisterLibrary(specConstantsLibName, specConstantLibraryBlob.Get());

        wchar_t shaderLibName[0x100];
        swprintf_s(shaderLibName, L"Shader_%d", guestShader->shaderCacheEntry->dxilOffset);

        ComPtr<IDxcBlobEncoding> shaderLibraryBlob;
        {
            std::lock_guard lock(guestShader->mutex);
            shaderLibraryBlob = guestShader->libraryBlob;
        }

        if (shaderLibraryBlob == nullptr)
        {
            if (s_dxcUtils == nullptr)
            {
                HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(s_dxcUtils.GetAddressOf()));
                assert(SUCCEEDED(hr) && s_dxcUtils != nullptr);
            }

            HRESULT hr = s_dxcUtils->CreateBlobFromPinned(
                g_shaderCache.get() + guestShader->shaderCacheEntry->dxilOffset,
                guestShader->shaderCacheEntry->dxilSize,
                DXC_CP_ACP,
                shaderLibraryBlob.GetAddressOf());

            assert(SUCCEEDED(hr) && shaderLibraryBlob != nullptr);

            std::lock_guard lock(guestShader->mutex);
            guestShader->libraryBlob = shaderLibraryBlob;
        }

        s_dxcLinker->RegisterLibrary(shaderLibName, shaderLibraryBlob.Get());

        const wchar_t* libraryNames[] = { specConstantsLibName, shaderLibName };

        ComPtr<IDxcOperationResult> result;
        HRESULT hr = s_dxcLinker->Link(L"shaderMain", guestShader->type == ResourceType::VertexShader ? L"vs_6_0" : L"ps_6_0",
            libraryNames, std::size(libraryNames), nullptr, 0, result.GetAddressOf());

        assert(SUCCEEDED(hr) && result != nullptr);

        ComPtr<IDxcBlob> blob;
        hr = result->GetResult(blob.GetAddressOf());
        assert(SUCCEEDED(hr) && blob != nullptr);

        {
            std::lock_guard lock(guestShader->mutex);

            auto& linkedShader = guestShader->linkedShaders[specConstants];
            if (linkedShader == nullptr)
            {
                linkedShader = g_device->createShader(blob->GetBufferPointer(), blob->GetBufferSize(), "shaderMain", RenderShaderFormat::DXIL);
                guestShader->shaderBlobs.push_back(std::move(blob));
            }

            shader = linkedShader.get();

#ifdef _DEBUG
            shader->setName(fmt::format("{}:{:x}", guestShader->shaderCacheEntry->filename, guestShader->shaderCacheEntry->hash));
#endif
        }        
    }
#endif

    return shader;
}

#if defined(__SWITCH__) && defined(SPEC_CONSTANT_BLEND_SKIP_ALPHA) && defined(SPEC_CONSTANT_BLEND_SKIP_ZERO)
// [Switch] SwitchSkipTransparentPixels: the SPEC_CONSTANT_BLEND_SKIP_* bit of a sanitized state whose blend leaves a pixel
// of the target exactly as it was when the shader's output is transparent (shader_common.h). UNORM targets only (not
// sRGB): the blend clamps their source values to [0, 1] first, so an output of 0 or less contributes exactly 0, and
// their texels are finite, so a destination factor of 1 keeps them. NaN outputs are never discarded. The discarded pixel
// must have no other effect: no depth write and no alpha to coverage, as in Unleashed, and for Marathon also no stencil
// write (a discard skips the stencil operations, which a blend that changes nothing does not) and no survey. The
// pipeline's only colour target is the one this blend is for (Marathon binds one), and a pixel shader's depth output is
// only stored by a depth write. Never for a pipeline without a colour target (no blend), which depth-only pipelines rely on.
static uint32_t BlendSkipBits(const PipelineState& state)
{
    if (!g_skipTransparentPixels || state.pixelShader == nullptr || state.enableConditionalSurvey || !state.alphaBlendEnable ||
        state.colorWriteEnable == 0 || state.enableAlphaToCoverage || (state.specConstants & SPEC_CONSTANT_ALPHA_TO_COVERAGE) != 0 ||
        (state.zWriteEnable && state.depthStencilFormat != RenderFormat::UNKNOWN) || StencilMayBeWritten(state))
    {
        return 0;
    }

    switch (state.renderTargetFormat)
    {
    case RenderFormat::R8_UNORM:
    case RenderFormat::R16G16_UNORM:
    case RenderFormat::R8G8B8A8_UNORM:
    case RenderFormat::B8G8R8A8_UNORM:
    case RenderFormat::R16G16B16A16_UNORM:
        break;
    default:
        return 0;
    }

    auto keepsDestination = [](RenderBlendOperation operation)
        {
            return operation == RenderBlendOperation::ADD || operation == RenderBlendOperation::REV_SUBTRACT;
        };

    // A destination factor of exactly 1 once the source is 0 (colour) or its alpha is 0 (ALPHA).
    auto factorOne = [](RenderBlend factor, bool sourceZero)
        {
            return factor == RenderBlend::ONE || factor == RenderBlend::INV_SRC_ALPHA ||
                (sourceZero && factor == RenderBlend::INV_SRC_COLOR);
        };

    const bool colorWritten = (state.colorWriteEnable & 0x7) != 0;
    const bool alphaWritten = (state.colorWriteEnable & 0x8) != 0;

    // _ALPHA: a source alpha of 0 or less. The colour source term is 0 through a SRC_ALPHA or ZERO factor; the alpha
    // source term is 0 whatever its factor (all are finite).
    const bool alphaSkip =
        (!colorWritten || ((state.srcBlend == RenderBlend::SRC_ALPHA || state.srcBlend == RenderBlend::ZERO) &&
            factorOne(state.destBlend, false) && keepsDestination(state.blendOp))) &&
        (!alphaWritten || (factorOne(state.destBlendAlpha, true) && keepsDestination(state.blendOpAlpha)));

    if (alphaSkip)
        return SPEC_CONSTANT_BLEND_SKIP_ALPHA;

    // _ZERO: all four outputs 0 or less, so every source term is 0 whatever its factor.
    const bool zeroSkip =
        (!colorWritten || (factorOne(state.destBlend, true) && keepsDestination(state.blendOp))) &&
        (!alphaWritten || (factorOne(state.destBlendAlpha, true) && keepsDestination(state.blendOpAlpha)));

    return zeroSkip ? SPEC_CONSTANT_BLEND_SKIP_ZERO : 0;
}
#endif

static void SanitizePipelineState(PipelineState& pipelineState)
{
#if defined(__SWITCH__)
    // SwitchStableFramebuffers: only an attachment the draw does not use can be kept (GetKeptAttachments).
    if (pipelineState.colorWriteEnable)
        pipelineState.keptAttachments &= ~KEPT_COLOR_ATTACHMENT;
    if (pipelineState.zEnable || pipelineState.stencilEnable)
        pipelineState.keptAttachments &= ~KEPT_DEPTH_ATTACHMENT;
#endif

    if (!pipelineState.zEnable && !pipelineState.stencilEnable)
    {
#if defined(__SWITCH__)
        // A kept depth attachment keeps its format; the tests stay off.
        if ((pipelineState.keptAttachments & KEPT_DEPTH_ATTACHMENT) == 0)
#endif
        pipelineState.depthStencilFormat = RenderFormat::UNKNOWN;
    }

    if (!pipelineState.zEnable)
    {
        pipelineState.zWriteEnable = false;
        pipelineState.zFunc = RenderComparisonFunction::LESS;
        pipelineState.slopeScaledDepthBias = 0.0f;
        pipelineState.depthBias = 0;
    }

    if (!pipelineState.stencilEnable)
    {
        pipelineState.stencilTwoSided = false;
        pipelineState.stencilFunc = RenderComparisonFunction::ALWAYS;
        pipelineState.stencilFail = RenderStencilOp::KEEP;
        pipelineState.stencilZFail = RenderStencilOp::KEEP;
        pipelineState.stencilPass = RenderStencilOp::KEEP;
        pipelineState.stencilMask = 0xFFFFFFFF;
        pipelineState.stencilWriteMask = 0xFFFFFFFF;
        pipelineState.stencilRef = 0;
    }

    if (!pipelineState.stencilTwoSided)
    {
        pipelineState.stencilFuncCCW = pipelineState.stencilFunc;
        pipelineState.stencilFailCCW = pipelineState.stencilFail;
        pipelineState.stencilZFailCCW = pipelineState.stencilZFail;
        pipelineState.stencilPassCCW = pipelineState.stencilPass;
    }

    if (pipelineState.slopeScaledDepthBias == 0.0f)
        pipelineState.slopeScaledDepthBias = 0.0f; // Remove sign.

    if (!pipelineState.colorWriteEnable)
    {
        pipelineState.alphaBlendEnable = false;
#if defined(__SWITCH__)
        // A kept colour attachment keeps its format; the write mask stays 0.
        if ((pipelineState.keptAttachments & KEPT_COLOR_ATTACHMENT) == 0)
#endif
        pipelineState.renderTargetFormat = RenderFormat::UNKNOWN;
    }

    if (!pipelineState.alphaBlendEnable)
    {
        pipelineState.srcBlend = RenderBlend::ONE;
        pipelineState.destBlend = RenderBlend::ZERO;
        pipelineState.blendOp = RenderBlendOperation::ADD;
        pipelineState.srcBlendAlpha = RenderBlend::ONE;
        pipelineState.destBlendAlpha = RenderBlend::ZERO;
        pipelineState.blendOpAlpha = RenderBlendOperation::ADD;
    }

    for (size_t i = 0; i < 16; i++)
    {
        if (!pipelineState.vertexDeclaration->vertexStreams[i])
            pipelineState.vertexStrides[i] = 0;
    }

    uint32_t specConstantsMask = 0;
    if (pipelineState.vertexShader->shaderCacheEntry != nullptr)
        specConstantsMask |= pipelineState.vertexShader->shaderCacheEntry->specConstantsMask;

    if (pipelineState.pixelShader != nullptr && pipelineState.pixelShader->shaderCacheEntry != nullptr)
        specConstantsMask |= pipelineState.pixelShader->shaderCacheEntry->specConstantsMask;

#if defined(__SWITCH__) && defined(SPEC_CONSTANT_BLEND_SKIP_ALPHA) && defined(SPEC_CONSTANT_BLEND_SKIP_ZERO)
    {
        // Before masking: these bits are in the masks of the pixel shaders that have the code (shader_common.h). The
        // early-out may go into every pipeline (without SPEC_CONSTANT_ALPHA_TEST it changes nothing); the sunk variants
        // only into pipelines whose early-out can skip pixels (the alpha test, or the transparent pixels of a blend),
        // the others would run their previous code anyway (UR_ALPHA_TEST_SINK). Derived from the state alone, which
        // the pipeline's hash covers.
        const uint32_t blendSkipBits = BlendSkipBits(pipelineState);
        const bool earlyOutSkips = (pipelineState.specConstants & SPEC_CONSTANT_ALPHA_TEST) != 0 ||
            (blendSkipBits & SPEC_CONSTANT_BLEND_SKIP_ALPHA) != 0;
        pipelineState.specConstants |= g_alphaTestEarlyOutBit | blendSkipBits | (earlyOutSkips ? g_alphaTestSinkBit : 0);
    }
#endif

    pipelineState.specConstants &= specConstantsMask;

#if defined(__SWITCH__)
    // After masking on purpose: these bits are in no shader's mask, so that the masks (and D3D12's linking) stay as
    // they are, and every pipeline, whichever thread creates it, gets them here, so the hashes agree.
    pipelineState.specConstants |= g_postMaskSpecConstants;

    [[maybe_unused]] const ShaderCacheEntry* vertexEntry = pipelineState.vertexShader->shaderCacheEntry;
    [[maybe_unused]] const uint32_t vertexFlags = vertexEntry != nullptr ? vertexEntry->flags : 0;

#if defined(SPEC_CONSTANT_NO_CLIP_DISTANCE) && defined(SHADER_FLAG_CLIP_DISTANCE_SPECIALIZED)
    // [Switch] SwitchClipDistanceSpecialization. A pipeline drawn with the clip plane disabled: every vertex wrote a clip
    // distance of +0.0 (the zeroed output; g_ClipPlaneEnabled false), and a primitive whose clip distances are all >= 0
    // is entirely inside the clip half-space, neither clipped nor split, so the primitives were those of no clip
    // distance at all. With this bit its vertex shader stores no clip distance (the translator made that store depend on
    // it, SHADER_FLAG_CLIP_DISTANCE_SPECIALIZED), so the driver finds none (Mesa's nir_shader_gather_info clears
    // clip_distance_array_size, NAK leaves the user clip plane off): the same primitives, positions (oPos stays
    // `precise`) and interpolants, without the clip test and the extra output. The plane state is part of the raw state
    // (clipPlaneEnabled, set with the shared constant), so every draw with the plane enabled selects a pipeline without
    // the bit, which writes the clip distance exactly as before. The field itself is cleared: the bit carries it.
    if (g_clipDistanceSpecialization && pipelineState.clipPlaneEnabled == 0 && (vertexFlags & SHADER_FLAG_CLIP_DISTANCE_SPECIALIZED) != 0)
        pipelineState.specConstants |= SPEC_CONSTANT_NO_CLIP_DISTANCE;
#endif
    pipelineState.clipPlaneEnabled = 0;

#if defined(SPEC_CONSTANT_RELATIVE_FROM_MEMORY) && defined(SHADER_FLAG_RELATIVE_CONSTANTS)
    // [Switch] SwitchIndexedConstantsFromMemory (with SwitchConstantsUBO): the a0-indexed constant arrays of these vertex
    // shaders are read through the constant pointer instead of the uniform buffer, the same bytes of the same upload
    // allocation (shader_common.h, X_Rel). PushRootAddressesIfNeeded pushes the pointers for their draws.
    if (g_relativeFromMemory && (vertexFlags & SHADER_FLAG_RELATIVE_CONSTANTS) != 0)
        pipelineState.specConstants |= SPEC_CONSTANT_RELATIVE_FROM_MEMORY;
#endif
#endif
}

#if defined(__SWITCH__)
// [Switch] SwitchDepthOnlyWithoutPixelShader. Without a colour target, a pixel shader's only possible effects are
// depth, stencil and sample mask writes, memory writes and kills. When the SPIR-V analysis shows it has none but kills
// that this pipeline's spec constants leave out (the alpha test, the conditional rendering prologue, the blend skip:
// AnalyseTranslatedShader), and there is no alpha to coverage, the pipeline is built without a fragment stage: the
// rasterised coverage, the depth and stencil tests and writes are the same, so is every attachment, and the GPU skips
// fragment shading for these draws (shadow maps, depth passes). Never for a survey pipeline: its fragment stage is the
// survey shader, whose write is the point of the draw.
//
// Holds for a sanitized state (CreateGraphicsPipeline) and, one way, for the raw state the render thread holds
// (PipelineHasNoPixelStage): each condition on the raw state implies the same on its sanitized form, because
// SanitizePipelineState only clears the colour target (no colour writes), the depth target (no depth or stencil) and
// spec bits, and adds none of the tested bits to a state without a colour target. Keep it that way.
static bool PixelShaderRemovedFromPipeline(const PipelineState& state)
{
    uint32_t discardBits = SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_CONDITIONAL_RENDERING;
#if defined(SPEC_CONSTANT_BLEND_SKIP_ALPHA) && defined(SPEC_CONSTANT_BLEND_SKIP_ZERO)
    discardBits |= SPEC_CONSTANT_BLEND_SKIP_ALPHA | SPEC_CONSTANT_BLEND_SKIP_ZERO;
#endif

    return g_switchRenderer.depthOnlyWithoutPixelShader && !state.enableConditionalSurvey && state.pixelShader != nullptr &&
        (!state.colorWriteEnable || state.renderTargetFormat == RenderFormat::UNKNOWN) &&
        (state.zEnable || state.stencilEnable) && state.depthStencilFormat != RenderFormat::UNKNOWN &&
        !state.enableAlphaToCoverage && (state.specConstants & discardBits) == 0 &&
        state.pixelShader->removableInDepthOnlyPass.load(std::memory_order_acquire);
}

// The pipeline of the render thread's (raw) state has no fragment stage: nothing reads the pixel shader constants.
static bool PipelineHasNoPixelStage(const PipelineState& state)
{
    if (state.pixelShader == nullptr)
        return !state.enableConditionalSurvey;

    return PixelShaderRemovedFromPipeline(state);
}

// [Switch] perf8: the texture slots a shader can sample (bit s = slot s): the declared samplers the translator records
// in its cache entry; every slot for a hand-written shader (no entry).
static uint32_t ShaderTextureSlotsRead(const GuestShader* shader)
{
    if (shader->shaderCacheEntry == nullptr)
        return 0xFFFF;

    return shader->shaderCacheEntry->textureSlotsRead & 0xFFFF;
}

// The slots the next draw can sample: its vertex shader's, and its pixel shader's when the pipeline has a pixel stage
// (none for a depth-only draw without one; the conditional survey's own pixel shader samples nothing).
static uint32_t DrawTextureSlotsRead()
{
    uint32_t slots = g_pipelineState.vertexShader != nullptr ? ShaderTextureSlotsRead(g_pipelineState.vertexShader) : 0xFFFF;
    if (g_pipelineState.pixelShader != nullptr && !PipelineHasNoPixelStage(g_pipelineState))
        slots |= ShaderTextureSlotsRead(g_pipelineState.pixelShader);

    return slots;
}

// SwitchCascadeAdoption (perf8): before a draw that can sample the cascaded shadow map, its held slices become its
// content. A slot that only holds it (the game binds the map before its last shadow pass) changes nothing.
static void CascadeAdoptForDraw()
{
    if (g_cascade.held == 0 || g_cascade.array == nullptr)
        return;

    const uint32_t slots = DrawTextureSlotsRead();
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if ((slots & (1u << i)) != 0 && g_textures[i] == g_cascade.array)
        {
            CascadeAdopt();
            return;
        }
    }
}

// [Switch] SwitchTrimConstantUploads. The bytes of a constant block a translated shader can read: its registers from
// c0 up to the last one it reads (XenosRecomp counts them; the whole block when it indexes one by a0 or aL), rounded
// up to 256 bytes. Nothing past them is read, so nothing past them is copied. Hand-written shaders (no cache entry)
// and draws without a shader get the whole block.
static uint32_t ConstantBytesToUpload(const GuestShader* shader, uint32_t blockSize)
{
    if (!g_switchRenderer.trimConstantUploads || shader == nullptr || shader->shaderCacheEntry == nullptr)
        return blockSize;

    const uint32_t bytes = (shader->shaderCacheEntry->float4ConstantRegisters * 16 + 0xFF) & ~0xFFu;
    return std::min(bytes, blockSize);
}
#endif

static std::unique_ptr<RenderPipeline> CreateGraphicsPipeline(const PipelineState& pipelineState)
{
#ifdef ASYNC_PSO_DEBUG
    ++g_pipelinesCurrentlyCompiling;
#endif

    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = GetOrLinkShader(pipelineState.vertexShader, pipelineState.specConstants);
    if (pipelineState.enableConditionalSurvey)
        desc.pixelShader = GetOrLinkShader(g_conditionalSurveyPSShader.get(), pipelineState.specConstants);
    else if (pipelineState.pixelShader != nullptr)
        desc.pixelShader = GetOrLinkShader(pipelineState.pixelShader, pipelineState.specConstants);
    else
        desc.pixelShader = nullptr;
    desc.depthFunction = pipelineState.zFunc;
    desc.depthEnabled = pipelineState.zEnable;
    desc.depthWriteEnabled = pipelineState.zWriteEnable;
    desc.depthBias = pipelineState.depthBias;
    desc.stencilEnabled = pipelineState.stencilEnable;
    desc.stencilReadMask = pipelineState.stencilMask;
    desc.stencilWriteMask = pipelineState.stencilWriteMask;
    desc.stencilReference = pipelineState.stencilRef;
    desc.stencilFrontFace.compareFunction = pipelineState.stencilFunc;
    desc.stencilFrontFace.failOp = pipelineState.stencilFail;
    desc.stencilFrontFace.depthFailOp = pipelineState.stencilZFail;
    desc.stencilFrontFace.passOp = pipelineState.stencilPass;
    if (pipelineState.stencilTwoSided) {
        desc.stencilBackFace.compareFunction = pipelineState.stencilFuncCCW;
        desc.stencilBackFace.failOp = pipelineState.stencilFailCCW;
        desc.stencilBackFace.depthFailOp = pipelineState.stencilZFailCCW;
        desc.stencilBackFace.passOp = pipelineState.stencilPassCCW;
    } else {
        desc.stencilBackFace = desc.stencilFrontFace;
    }
    desc.slopeScaledDepthBias = pipelineState.slopeScaledDepthBias;
    desc.dynamicDepthBiasEnabled = g_capabilities.dynamicDepthBias;
    desc.depthClipEnabled = true;
    desc.primitiveTopology = pipelineState.primitiveTopology;
    desc.cullMode = pipelineState.cullMode;
    desc.frontFace = pipelineState.frontFace;
    desc.renderTargetFormat[0] = pipelineState.renderTargetFormat;
    desc.renderTargetBlend[0].blendEnabled = pipelineState.alphaBlendEnable;
    desc.renderTargetBlend[0].srcBlend = pipelineState.srcBlend;
    desc.renderTargetBlend[0].dstBlend = pipelineState.destBlend;
    desc.renderTargetBlend[0].blendOp = pipelineState.blendOp;
    desc.renderTargetBlend[0].srcBlendAlpha = pipelineState.srcBlendAlpha;
    desc.renderTargetBlend[0].dstBlendAlpha = pipelineState.destBlendAlpha;
    desc.renderTargetBlend[0].blendOpAlpha = pipelineState.blendOpAlpha;
    desc.renderTargetBlend[0].renderTargetWriteMask = pipelineState.colorWriteEnable;
    desc.renderTargetCount = pipelineState.renderTargetFormat != RenderFormat::UNKNOWN ? 1 : 0;
    desc.depthTargetFormat = pipelineState.depthStencilFormat;
    desc.multisampling.sampleCount = pipelineState.sampleCount;
    desc.alphaToCoverageEnabled = pipelineState.enableAlphaToCoverage;
    desc.inputElements = pipelineState.vertexDeclaration->inputElements.get();
    desc.inputElementsCount = pipelineState.vertexDeclaration->inputElementCount;
    
#if defined(__SWITCH__)
    // [Switch] SwitchCoverageHandOver: the same draw, also marking every pixel it writes with the hand-over's reference
    // (dynamic, TryCoverageHandOver) in an 8-bit stencil buffer of its own. Only for draws without a depth buffer: the
    // pass has none of the game's, so the depth and stencil state the game set is irrelevant; the colour output is
    // untouched. A fragment the pixel shader discards writes no mark (the translated shaders use late tests).
    if (pipelineState.coverageStencil != 0)
    {
        desc.depthTargetFormat = RenderFormat::S8_UINT;
        desc.depthEnabled = false;
        desc.depthWriteEnabled = false;
        desc.stencilEnabled = true;
        desc.stencilReadMask = 0xFF;
        desc.stencilWriteMask = 0xFF;
        desc.stencilReference = 1;
        desc.dynamicStencilReferenceEnabled = true;
        desc.stencilFrontFace.passOp = RenderStencilOp::REPLACE;
        desc.stencilFrontFace.failOp = RenderStencilOp::KEEP;
        desc.stencilFrontFace.depthFailOp = RenderStencilOp::KEEP;
        desc.stencilFrontFace.compareFunction = RenderComparisonFunction::ALWAYS;
        desc.stencilBackFace = desc.stencilFrontFace;
    }

    // Depth-only draws without a fragment stage (PixelShaderRemovedFromPipeline).
    if (desc.pixelShader != nullptr && PixelShaderRemovedFromPipeline(pipelineState))
        desc.pixelShader = nullptr;

    // Constant 0: g_SpecConstants. 1-3: g_SpecUnusedOutputs0-2, 4: g_SpecVertexSwaps (shader_common.h); the pixel
    // shaders and the hand-written shaders do not declare them, which Vulkan allows. 0 is what a constant left out
    // means, so ids 1-3 are 0 when only id 4 is given.
    RenderSpecConstant specConstants[5];
    for (uint32_t i = 0; i < std::size(specConstants); i++)
        specConstants[i].index = i;

    specConstants[0].value = pipelineState.specConstants;
    uint32_t specConstantCount = 1;

#ifdef MARATHON_RECOMP_SWITCH_SHADER_SPECIALIZATION
    // [Switch] SwitchTrimVertexOutputs. The vertex outputs nothing reads: all of them without a fragment stage,
    // otherwise every component of the locations the pixel shader never loads (its SPIR-V analysed; DXC gives both
    // stages the translator's interpolator order: TEXCOORD0-15, COLOR0-1 at locations 0-17). The translated vertex
    // shader writes 0 to them instead of computing them. The position and the clip distance are built-ins and never
    // touched; the driver also drops single components nobody reads (NVK_LINK_VARYINGS). Derived from the shaders and
    // the pipeline state alone, which the pipeline's hash covers. Hand-written shaders keep every output.
    if (g_switchRenderer.trimVertexOutputs && pipelineState.vertexShader->shaderCacheEntry != nullptr)
    {
        uint32_t read = ~0u;
        if (desc.pixelShader == nullptr)
            read = 0;
        else if (pipelineState.enableConditionalSurvey)
            read = g_conditionalSurveyPSShader->inputLocationsRead.load(std::memory_order_acquire);
        else if (pipelineState.pixelShader != nullptr && pipelineState.pixelShader->shaderCacheEntry != nullptr)
            read = pipelineState.pixelShader->inputLocationsRead.load(std::memory_order_acquire);

        for (uint32_t location = 0; location < SPEC_CONSTANT_UNUSED_OUTPUT_COMPONENT_COUNT / 4; location++)
        {
            if ((read & (1u << location)) == 0)
                specConstants[1 + location / 8].value |= 0xFu << ((location % 8) * 4);
        }

        specConstantCount = 4;
    }

    // [Switch] SwitchTrimPixelOutputs. The oC0 components nothing uses: not written (colour write mask, no colour
    // target) and not read by blending through a source alpha factor, the alpha test or alpha to coverage (Marathon's
    // SPEC_CONSTANT_ALPHA_TO_COVERAGE never survives the mask: enableAlphaToCoverage says it). The translated pixel
    // shader writes 0 to them at every exit, after the alpha test and the blend skip, so their math is dropped; oC1-3
    // and the depth output are never touched. Derived from state the pipeline's hash covers.
    if (g_switchRenderer.trimPixelOutputs && desc.pixelShader != nullptr && !pipelineState.enableConditionalSurvey &&
        pipelineState.pixelShader != nullptr && pipelineState.pixelShader->shaderCacheEntry != nullptr)
    {
        auto readsSourceAlpha = [](RenderBlend blend)
            {
                return blend == RenderBlend::SRC_ALPHA || blend == RenderBlend::INV_SRC_ALPHA || blend == RenderBlend::SRC_ALPHA_SAT;
            };

        const uint32_t written = desc.renderTargetCount != 0 ? (pipelineState.colorWriteEnable & 0xF) : 0;
        uint32_t used = written;

        if (pipelineState.alphaBlendEnable && (written & 0x7) != 0 &&
            (readsSourceAlpha(pipelineState.srcBlend) || readsSourceAlpha(pipelineState.destBlend)))
        {
            used |= 0x8;
        }

        if ((pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) != 0 ||
            pipelineState.enableAlphaToCoverage)
        {
            used |= 0x8;
        }

        specConstants[0].value |= (~used & 0xFu) << SPEC_CONSTANT_UNUSED_COLOR_SHIFT;
    }
#endif

#if defined(VERTEX_SWAPS_SPECIALIZED) && defined(VERTEX_SWAPS_TEXCOORDS_SHIFT) && defined(VERTEX_SWAPS_BLEND_WEIGHTS_SHIFT)
    // [Switch] SwitchVertexSwapSpecialization. The translated vertex shaders take the half-swap masks of their inputs
    // from constant 4 instead of the shared constants (shader_common.h, g_SpecVertexSwaps). Both come from the pipeline's
    // vertex declaration: ProcSetVertexDeclaration writes the shared masks from the declaration it puts in the state, and
    // the declaration is part of the pipeline's hash (declarations are deduplicated by content and never freed), so every
    // draw of this pipeline had these very masks in the shared constants. A swap is a pure permutation of the components
    // (swapFloats), resolved when the driver compiles the pipeline: the same bits reach the same instructions. The shaders
    // test bits 0-3 only (usage indices 0-3, USAGE_LOCATIONS); masking each field keeps it from spilling into the next.
    const GuestVertexDeclaration* vertexDeclaration = pipelineState.vertexDeclaration;
    if (g_vertexSwapSpecialization && pipelineState.vertexShader->shaderCacheEntry != nullptr && vertexDeclaration != nullptr)
    {
        specConstants[4].value = VERTEX_SWAPS_SPECIALIZED |
            ((vertexDeclaration->swappedTexcoords & 0xF) << VERTEX_SWAPS_TEXCOORDS_SHIFT) |
            ((vertexDeclaration->swappedNormals & 0xF) << VERTEX_SWAPS_NORMALS_SHIFT) |
            ((vertexDeclaration->swappedBinormals & 0xF) << VERTEX_SWAPS_BINORMALS_SHIFT) |
            ((vertexDeclaration->swappedTangents & 0xF) << VERTEX_SWAPS_TANGENTS_SHIFT) |
            ((vertexDeclaration->swappedBlendWeights & 0xF) << VERTEX_SWAPS_BLEND_WEIGHTS_SHIFT);

        specConstantCount = 5;
    }
#endif

    if (specConstants[0].value != 0 || specConstantCount > 1)
    {
        desc.specConstants = specConstants;
        desc.specConstantsCount = specConstantCount;
    }
#else
    RenderSpecConstant specConstant{};
    specConstant.value = pipelineState.specConstants;
    
    if (pipelineState.specConstants != 0)
    {
        desc.specConstants = &specConstant;
        desc.specConstantsCount = 1;
    }
#endif
    
    RenderInputSlot inputSlots[16]{};
    uint32_t inputSlotIndices[16]{};
    uint32_t inputSlotCount = 0;
    
    for (size_t i = 0; i < pipelineState.vertexDeclaration->inputElementCount; i++)
    {
        auto& inputElement = pipelineState.vertexDeclaration->inputElements[i];
        auto& inputSlotIndex = inputSlotIndices[inputElement.slotIndex];
    
        if (inputSlotIndex == NULL)
            inputSlotIndex = ++inputSlotCount;
    
        auto& inputSlot = inputSlots[inputSlotIndex - 1];
        inputSlot.index = inputElement.slotIndex;
        inputSlot.stride = pipelineState.vertexStrides[inputElement.slotIndex];
        inputSlot.classification = RenderInputSlotClassification::PER_VERTEX_DATA;
    }
    
    desc.inputSlots = inputSlots;
    desc.inputSlotsCount = inputSlotCount;
    
    auto pipeline = g_device->createGraphicsPipeline(desc);

#ifdef ASYNC_PSO_DEBUG
    --g_pipelinesCurrentlyCompiling;
#endif

    return pipeline;
}

#if defined(__SWITCH__)
// [Switch] SwitchPipelineLookupCache. The render thread looks up the pipeline of every state it binds: it
// sanitizes a copy, hashes the whole state (XXH3) and looks it up in g_pipelines. Draws come back to the same
// few states, so the last results are kept, keyed by the raw state (PipelineState is packed, memcmp sees every
// byte). The lookup reads nothing else: SanitizePipelineState only reads the state, its shaders' cache entries
// and its vertex declaration, which are never freed (CreateShader and the vertex declaration cache hold a
// reference), and a g_pipelines entry is never replaced once created. Storing a pipeline in g_pipelines starts
// a new generation anyway, so a result found before is not used again; a failed creation is not kept, so it is
// retried at the next bind as before. Anything a later change makes the lookup read must join the key: with
// SwitchShadowGatherSpecialization, the gatherable slots (FindOrCreateGraphicsPipeline).
struct PipelineLookupEntry
{
    PipelineState state;
    uint32_t gatherableSlots = 0;
    uint32_t generation = 0;
    RenderPipeline* pipeline = nullptr;
};

// What the lookup reads besides the raw state: the gatherable slots that any shader gathers (FindShadowGatherVariant).
static uint32_t PipelineLookupGatherableSlots()
{
#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
    if (g_shadowGatherSpecialization)
        return g_sharedConstants.gatherableSlots & g_gatherSlotsRead;
#endif
    return 0;
}

static PipelineLookupEntry g_pipelineLookups[64];
static uint32_t g_pipelineGeneration = 1;

static PipelineLookupEntry& PipelineLookupSlot(const PipelineState& state)
{
    uint64_t key = reinterpret_cast<uintptr_t>(state.vertexShader) ^ (reinterpret_cast<uintptr_t>(state.pixelShader) >> 3) ^
        (reinterpret_cast<uintptr_t>(state.vertexDeclaration) >> 5) ^ (uint64_t(state.specConstants) << 32) ^
        (uint64_t(state.colorWriteEnable) << 24) ^ (uint64_t(state.srcBlend) << 40) ^ (uint64_t(state.destBlend) << 44) ^
        (uint64_t(state.alphaBlendEnable) << 48) ^ (uint64_t(state.zEnable) << 49) ^ (uint64_t(state.zWriteEnable) << 50) ^
        (uint64_t(state.stencilEnable) << 51) ^ (uint64_t(state.cullMode) << 52) ^ (uint64_t(state.enableConditionalSurvey) << 55) ^
        (uint64_t(state.renderTargetFormat) << 56) ^ (uint64_t(state.primitiveTopology) << 60);
    key *= 0x9E3779B97F4A7C15ull;
    return g_pipelineLookups[key >> (64 - 6)];
}
#endif

static RenderPipeline* FindOrCreateGraphicsPipeline(PipelineState pipelineState);

static RenderPipeline* CreateGraphicsPipelineInRenderThread(const PipelineState& pipelineState)
{
#if defined(__SWITCH__)
    g_rendererStats.pipelineBinds++;

    if (g_switchRenderer.pipelineLookupCache)
    {
        auto& entry = PipelineLookupSlot(pipelineState);
        const uint32_t gatherableSlots = PipelineLookupGatherableSlots();
        if (entry.generation == g_pipelineGeneration && entry.gatherableSlots == gatherableSlots &&
            memcmp(&entry.state, &pipelineState, sizeof(PipelineState)) == 0)
        {
            g_rendererStats.pipelineLookupHits++;
            return entry.pipeline;
        }

        RenderPipeline* pipeline = FindOrCreateGraphicsPipeline(pipelineState);
        if (pipeline != nullptr)
        {
            entry.state = pipelineState;
            entry.gatherableSlots = gatherableSlots;
            entry.generation = g_pipelineGeneration;
            entry.pipeline = pipeline;
        }

        return pipeline;
    }
#endif

    return FindOrCreateGraphicsPipeline(pipelineState);
}

#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
static RenderPipeline* FindOrRequestPipelineVariant(const PipelineState& sanitizedState, uint32_t bits);

// [Switch] SwitchShadowGatherSpecialization. A pixel shader whose shadow gathers read only slots that are gatherable for
// this draw (its cache entry's gatherSlots, all in g_GatherableSlots) has a variant with SPEC_CONSTANT_SHADOW_GATHER_KNOWN,
// which compiles the runtime test of g_GatherableSlots out: the test would pass, so the variant takes the same path and
// computes the same values (the point fetches stay for the pixels outside tfetch2DArrayGatherExact). The compiler
// threads build it; the generic pipeline draws until it exists. Chosen again whenever the slots change
// (UpdateTextureSlotTables marks the pipeline dirty), and the lookup cache keys on them. Not for a survey pipeline (its
// pixel shader is the survey's) nor one without a fragment stage.
static RenderPipeline* FindShadowGatherVariant(const PipelineState& sanitizedState)
{
    if (!g_shadowGatherSpecialization || (sanitizedState.specConstants & SPEC_CONSTANT_SHADOW_GATHER) == 0 ||
        sanitizedState.enableConditionalSurvey || sanitizedState.pixelShader == nullptr ||
        sanitizedState.pixelShader->shaderCacheEntry == nullptr)
    {
        return nullptr;
    }

    const uint32_t slots = sanitizedState.pixelShader->shaderCacheEntry->gatherSlots;
    if (slots == 0 || (slots & ~g_sharedConstants.gatherableSlots) != 0 || PixelShaderRemovedFromPipeline(sanitizedState))
        return nullptr;

    return FindOrRequestPipelineVariant(sanitizedState, SPEC_CONSTANT_SHADOW_GATHER_KNOWN);
}
#endif

static RenderPipeline* FindOrCreateGraphicsPipeline(PipelineState pipelineState)
{
    SanitizePipelineState(pipelineState);

#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
    if (RenderPipeline* variant = FindShadowGatherVariant(pipelineState))
        return variant;
#endif

    XXH64_hash_t hash = XXH3_64bits(&pipelineState, sizeof(pipelineState));
    auto& pipeline = g_pipelines[hash];
    if (pipeline == nullptr)
    {
#if defined(__SWITCH__)
        // SwitchGpuPassProfiler: pipelines compiled on the render thread, and how long it waited for them.
        const auto creationStart = g_passProfilerEnabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
        pipeline = CreateGraphicsPipeline(pipelineState);
#if defined(__SWITCH__)
        g_pipelineGeneration++;
        g_rendererStats.pipelinesCreated++;
        if (g_passProfilerEnabled)
        {
            g_rendererStats.pipelineCreationUs += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - creationStart).count());
        }
#endif

#ifdef ASYNC_PSO_DEBUG
        bool loading = *SWA::SGlobals::ms_IsLoading;

        if (loading)
            ++g_pipelinesCreatedAsynchronously;
        else
            ++g_pipelinesCreatedInRenderThread;

        pipeline->setName(fmt::format("{} {} {} {:X}", loading ? "ASYNC" : "",
            pipelineState.vertexShader->name, pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->name : "<none>", hash));
        
        if (!loading)
        {
            std::lock_guard lock(g_debugMutex);
            g_pipelineDebugText = fmt::format(
                "PipelineState {:X}:\n"
                "  vertexShader: {}\n"
                "  pixelShader: {}\n"
                "  vertexDeclaration: {:X}\n"
                "  zEnable: {}\n"
                "  zWriteEnable: {}\n"
                "  stencilEnable: {}\n"
                "  stencilTwoSided: {}\n"
                "  srcBlend: {}\n"
                "  destBlend: {}\n"
                "  cullMode: {}\n"
                "  frontFace: {}\n"
                "  zFunc: {}\n"
                "  stencilFunc: {}\n"
                "  stencilFail: {}\n"
                "  stencilZFail: {}\n"
                "  stencilPass: {}\n"
                "  stencilFuncCCW: {}\n"
                "  stencilFailCCW: {}\n"
                "  stencilZFailCCW: {}\n"
                "  stencilPassCCW: {}\n"
                "  stencilMask: {}\n"
                "  stencilWriteMask: {}\n"
                "  stencilRef: {}\n"
                "  alphaBlendEnable: {}\n"
                "  blendOp: {}\n"
                "  slopeScaledDepthBias: {}\n"
                "  depthBias: {}\n"
                "  srcBlendAlpha: {}\n"
                "  destBlendAlpha: {}\n"
                "  blendOpAlpha: {}\n"
                "  colorWriteEnable: {:X}\n"
                "  primitiveTopology: {}\n"
                "  vertexStrides[0]: {}\n"
                "  vertexStrides[1]: {}\n"
                "  vertexStrides[2]: {}\n"
                "  vertexStrides[3]: {}\n"
                "  renderTargetFormat: {}\n"
                "  depthStencilFormat: {}\n"
                "  sampleCount: {}\n"
                "  enableAlphaToCoverage: {}\n"
                "  enableConditionalSurvey: {}\n"
                "  specConstants: {:X}\n",
                hash,
                pipelineState.vertexShader->name,
                pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->name : "<none>",
                reinterpret_cast<size_t>(pipelineState.vertexDeclaration),
                pipelineState.zEnable,
                pipelineState.zWriteEnable,
                pipelineState.stencilEnable,
                pipelineState.stencilTwoSided,
                magic_enum::enum_name(pipelineState.srcBlend),
                magic_enum::enum_name(pipelineState.destBlend),
                magic_enum::enum_name(pipelineState.cullMode),
                magic_enum::enum_name(pipelineState.frontFace),
                magic_enum::enum_name(pipelineState.zFunc),
                magic_enum::enum_name(pipelineState.stencilFunc),
                magic_enum::enum_name(pipelineState.stencilFail),
                magic_enum::enum_name(pipelineState.stencilZFail),
                magic_enum::enum_name(pipelineState.stencilPass),
                magic_enum::enum_name(pipelineState.stencilFuncCCW),
                magic_enum::enum_name(pipelineState.stencilFailCCW),
                magic_enum::enum_name(pipelineState.stencilZFailCCW),
                magic_enum::enum_name(pipelineState.stencilPassCCW),
                pipelineState.stencilMask,
                pipelineState.stencilWriteMask,
                pipelineState.stencilRef,
                pipelineState.alphaBlendEnable,
                magic_enum::enum_name(pipelineState.blendOp),
                pipelineState.slopeScaledDepthBias,
                pipelineState.depthBias,
                magic_enum::enum_name(pipelineState.srcBlendAlpha),
                magic_enum::enum_name(pipelineState.destBlendAlpha),
                magic_enum::enum_name(pipelineState.blendOpAlpha),
                pipelineState.colorWriteEnable,
                magic_enum::enum_name(pipelineState.primitiveTopology),
                pipelineState.vertexStrides[0],
                pipelineState.vertexStrides[1],
                pipelineState.vertexStrides[2],
                pipelineState.vertexStrides[3],
                magic_enum::enum_name(pipelineState.renderTargetFormat),
                magic_enum::enum_name(pipelineState.depthStencilFormat),
                pipelineState.sampleCount,
                pipelineState.enableAlphaToCoverage,
                pipelineState.enableConditionalSurvey,
                pipelineState.specConstants)
                + g_pipelineDebugText;
        }
#endif

#ifdef PSO_CACHING
        std::lock_guard lock(g_pipelineCacheMutex);
        g_pipelineStatesToCache.emplace(hash, pipelineState);
#endif
    }
    
    return pipeline.get();
}

static RenderTextureAddressMode ConvertTextureAddressMode(size_t value)
{
    switch (value)
    {
    case D3DTADDRESS_WRAP:
        return RenderTextureAddressMode::WRAP;
    case D3DTADDRESS_MIRROR:
        return RenderTextureAddressMode::MIRROR;
    case D3DTADDRESS_CLAMP:
        return RenderTextureAddressMode::CLAMP;
    case D3DTADDRESS_MIRRORONCE:
        return RenderTextureAddressMode::MIRROR_ONCE;
    case D3DTADDRESS_BORDER:
        return RenderTextureAddressMode::BORDER;
    default:
        assert(false && "Unknown texture address mode");
        return RenderTextureAddressMode::UNKNOWN;
    }
}

static RenderFilter ConvertTextureFilter(uint32_t value)
{
    switch (value)
    {
    case D3DTEXF_POINT:
    case D3DTEXF_NONE:
        return RenderFilter::NEAREST;
    case D3DTEXF_LINEAR:
        return RenderFilter::LINEAR;
    default:
        assert(false && "Unknown texture filter");
        return RenderFilter::UNKNOWN;
    }
}

static RenderBorderColor ConvertBorderColor(uint32_t value)
{
    switch (value)
    {
    case 0:
        return RenderBorderColor::TRANSPARENT_BLACK;
    case 1:
        return RenderBorderColor::OPAQUE_WHITE;
    default:
        assert(false && "Unknown border color");
        return RenderBorderColor::UNKNOWN;
    }
}

struct LocalRenderCommandQueue
{
    // Booleans, 16 samplers, up to 4 + 4 constant runs (SwitchSparseConstantCopies) and the draw: 26 at most.
    RenderCommand commands[32];
    uint32_t count = 0;

    RenderCommand& enqueue()
    {
        assert(count < std::size(commands));
        return commands[count++];
    }

    void submit()
    {
#if defined(__SWITCH__)
        if (ShouldBatchRenderCommands())
        {
            // The draw goes out together with the state changes that preceded it.
            auto& batch = g_deferredRenderCommands;
            if (batch.count + count > g_renderCommandBatchCapacity)
                FlushDeferredRenderCommands();

            for (uint32_t i = 0; i < count; i++)
                batch.commands[batch.count++] = commands[i];

            // [Switch] SwitchBatchSeveralDraws. Each hand-over of the batch can wake the render thread, which has
            // usually gone to sleep between two draws: a kernel signal paid on this thread. The batch now goes out
            // once it holds a few draws' worth of commands (RenderCommandBatchThreshold), at the flush points or
            // when the next draw would not fit. The order and content of the commands are unchanged.
            if (g_switchRenderer.batchSeveralDraws && batch.count < RenderCommandBatchThreshold())
                return;

            FlushDeferredRenderCommands();
            return;
        }

        for (uint32_t i = 0; i < count; i++)
            CountDirectRenderCommand(commands[i].type);
#endif
        g_renderQueue.enqueue_bulk(commands, count);
    }
};

#if defined(__SWITCH__)
// The changed float constants of one stage, from the device's dirty flags: bit 63 - g means registers
// 4g..4g+3 (64 bytes) changed (D3DDevice_BeginShaderConstantF4). Before, the whole span from the first changed
// group to the last one was copied, unchanged registers in between included. [Switch] SwitchSparseConstantCopies
// copies each run of consecutive changed groups instead (at most four; more runs copy the span as before). The
// render thread applies each copy to its own array, which already holds the unchanged registers: every change
// to the device's constants sets its group's bit, so a group without it holds what was copied last time. The
// render thread ends up with the same values either way.
static void EnqueueShaderConstants(LocalRenderCommandQueue& queue, RenderCommandType type, const uint32_t* constants,
    uint64_t dirty, uint32_t groupCount)
{
    static constexpr uint32_t MAX_RUNS = 4;

    auto enqueue = [&](uint32_t group, uint32_t groups)
        {
            const uint32_t index = group * 16;
            const uint32_t size = groups * 64;
            auto& cmd = queue.enqueue();
            cmd.type = type;
            if (type == RenderCommandType::SetVertexShaderConstants)
            {
                cmd.setVertexShaderConstants.memory = CurrentIntermediaryUploadAllocator().allocate(&constants[index], size);
                cmd.setVertexShaderConstants.index = index;
                cmd.setVertexShaderConstants.size = size;
            }
            else
            {
                cmd.setPixelShaderConstants.memory = CurrentIntermediaryUploadAllocator().allocate(&constants[index], size);
                cmd.setPixelShaderConstants.index = index;
                cmd.setPixelShaderConstants.size = size;
            }
        };

    // Groups past the stage's registers are not constants (the span copy clamped its end to them).
    if (groupCount < 64)
        dirty &= ~((uint64_t(1) << (64 - groupCount)) - 1);
    if (dirty == 0)
        return;

    // SwitchGpuPassProfiler: the bytes copied, and the span of the changed registers (what the span copy copied).
    auto countCopy = [dirty](uint32_t copiedBytes)
        {
            if (g_passProfilerEnabled)
            {
                AddGameThreadCount(g_profilerConstantBytesCopied, copiedBytes);
                AddGameThreadCount(g_profilerConstantBytesSpanned, (64 - std::countr_zero(dirty) - std::countl_zero(dirty)) * 64);
            }
        };

    if (g_switchRenderer.sparseConstantCopies)
    {
        uint32_t runs = 0;
        for (uint64_t bits = dirty; bits != 0 && runs <= MAX_RUNS; runs++)
        {
            const uint32_t start = std::countl_zero(bits);
            const uint32_t length = std::countl_one(bits << start);
            bits = (start + length >= 64) ? 0 : (bits & (~uint64_t(0) >> (start + length)));
        }

        if (runs <= MAX_RUNS)
        {
            for (uint64_t bits = dirty; bits != 0;)
            {
                const uint32_t start = std::countl_zero(bits);
                const uint32_t length = std::countl_one(bits << start);
                enqueue(start, length);
                bits = (start + length >= 64) ? 0 : (bits & (~uint64_t(0) >> (start + length)));
            }

            countCopy(uint32_t(std::popcount(dirty)) * 64);
            return;
        }
    }

    const uint32_t first = std::countl_zero(dirty);
    const uint32_t end = 64 - std::countr_zero(dirty);
    enqueue(first, end - first);
    countCopy((end - first) * 64);
}
#endif

static void FlushRenderStateForMainThread(GuestDevice* device, LocalRenderCommandQueue& queue)
{
    constexpr size_t BOOL_MASK = 0x2ull;
    if ((device->dirtyFlags[3].get() & BOOL_MASK) != 0)
    {
        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetBooleans;
        cmd.setBooleans.booleans = (device->vertexShaderBoolConstants[0].get() & 0xFF) | ((device->pixelShaderBoolConstants[0].get() & 0xFF) << 16);

        device->dirtyFlags[3] = device->dirtyFlags[3].get() & ~BOOL_MASK;
    }

#if defined(__SWITCH__)
    // The dirty samplers are bits 43-28 of the word (sampler i is bit 43 - i), read and cleared once, walked by bit
    // instead of testing all 16.
    const uint64_t samplerDirtyFlags = device->dirtyFlags[2].get();
    if ((samplerDirtyFlags & (0xFFFFull << 28)) != 0)
    {
        const bool filter = g_switchRenderer.skipRedundantSamplerStates && EnterStateFilter();
        for (uint32_t dirty = uint32_t(samplerDirtyFlags >> 28) & 0xFFFF; dirty != 0;)
        {
            const uint32_t i = std::countl_zero(dirty) - 16;
            dirty &= ~(0x8000u >> i);

            const uint32_t data0 = device->samplerStates[i].data[0];
            const uint32_t data3 = device->samplerStates[i].data[3];
            const uint32_t data5 = device->samplerStates[i].data[5];

            // SwitchSkipRedundantSamplerStates (StateFilter). ProcSetSamplerState is a function of these three words
            // and the anisotropic filtering setting (which the filter was entered with), and nothing else writes the
            // slot's description or descriptor index.
            if (filter)
            {
                auto& filterState = g_stateFilter;
                uint32_t* known = filterState.samplerStates[i];
                if ((filterState.samplerStatesKnown & (1u << i)) != 0 && known[0] == data0 && known[1] == data3 && known[2] == data5)
                {
                    if (g_passProfilerEnabled)
                        AddGameThreadCount(g_profilerStatesSkipped, 1);

                    continue;
                }

                filterState.samplerStatesKnown |= 1u << i;
                known[0] = data0;
                known[1] = data3;
                known[2] = data5;
            }

            auto& cmd = queue.enqueue();
            cmd.type = RenderCommandType::SetSamplerState;
            cmd.setSamplerState.index = i;
            cmd.setSamplerState.data0 = data0;
            cmd.setSamplerState.data3 = data3;
            cmd.setSamplerState.data5 = data5;
        }

        device->dirtyFlags[2] = samplerDirtyFlags & ~(0xFFFFull << 28);
    }
#else
    for (uint32_t i = 0; i < 16; i++)
    {
        const size_t mask = 0x8000000000000000ull >> (i + 20);
        if (device->dirtyFlags[2].get() & mask)
        {
            auto& cmd = queue.enqueue();
            cmd.type = RenderCommandType::SetSamplerState;
            cmd.setSamplerState.index = i;
            cmd.setSamplerState.data0 = device->samplerStates[i].data[0];
            cmd.setSamplerState.data3 = device->samplerStates[i].data[3];
            cmd.setSamplerState.data5 = device->samplerStates[i].data[5];

            device->dirtyFlags[2] = device->dirtyFlags[2].get() & ~mask;
        }
    }
#endif

#if defined(__SWITCH__)
    uint64_t dirtyFlags = device->dirtyFlags[0].get();
    if (dirtyFlags != 0)
    {
        EnqueueShaderConstants(queue, RenderCommandType::SetVertexShaderConstants, device->vertexShaderFloatConstants, dirtyFlags, 64);
        device->dirtyFlags[0] = 0;
    }

    dirtyFlags = device->dirtyFlags[1].get();
    if (dirtyFlags != 0)
    {
        EnqueueShaderConstants(queue, RenderCommandType::SetPixelShaderConstants, device->pixelShaderFloatConstants, dirtyFlags, 56);
        device->dirtyFlags[1] = 0;
    }
#else
    uint64_t dirtyFlags = device->dirtyFlags[0].get();
    if (dirtyFlags != 0)
    {
        int startRegister = std::countl_zero(dirtyFlags);
        int endRegister = 64 - std::countr_zero(dirtyFlags);

        uint32_t index = startRegister * 16;
        uint32_t size = (endRegister - startRegister) * 64;

        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetVertexShaderConstants;
        cmd.setVertexShaderConstants.memory = CurrentIntermediaryUploadAllocator().allocate(&device->vertexShaderFloatConstants[index], size);
        cmd.setVertexShaderConstants.index = index;
        cmd.setVertexShaderConstants.size = size;

        device->dirtyFlags[0] = 0;
    }

    dirtyFlags = device->dirtyFlags[1].get();
    if (dirtyFlags != 0)
    {
        int startRegister = std::countl_zero(dirtyFlags);
        int endRegister = std::min(56, 64 - std::countr_zero(dirtyFlags));

        uint32_t index = startRegister * 16;
        uint32_t size = (endRegister - startRegister) * 64;

        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetPixelShaderConstants;
        cmd.setPixelShaderConstants.memory = CurrentIntermediaryUploadAllocator().allocate(&device->pixelShaderFloatConstants[index], size);
        cmd.setPixelShaderConstants.index = index;
        cmd.setPixelShaderConstants.size = size;

        device->dirtyFlags[1] = 0;
    }
#endif
}

static void ProcSetBooleans(const RenderCommand& cmd)
{
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.booleans, cmd.setBooleans.booleans);
}

#if defined(__SWITCH__)
// [Switch] SwitchSamplerCache. The sampler state bits ProcSetSamplerState reads (address modes, filters, border
// colour) and the anisotropic filtering setting give its description and descriptor, which are kept for the
// words seen last: a repeated state skips the conversions, the hash and the lookup. The description is a pure
// function of them (every slot's description starts from the same default and nothing else writes these
// fields), and a description's descriptor never changes (g_samplerStates entries are never removed).
struct SamplerLookupEntry
{
    uint32_t key = ~0u;
    uint32_t anisotropicFiltering = 0;
    RenderSamplerDesc desc;
    uint32_t descriptorIndex = 0;
};

static SamplerLookupEntry g_samplerLookups[64];
#endif

static void ProcSetSamplerState(const RenderCommand& cmd)
{
    const auto& args = cmd.setSamplerState;

#if defined(__SWITCH__)
    g_rendererStats.samplerStates++;

    SamplerLookupEntry* lookup = nullptr;
    if (g_switchRenderer.samplerCache)
    {
        const uint32_t key = ((args.data0 >> 10) & 0x1FF) | (((args.data3 >> 19) & 0x3F) << 9) | ((args.data5 & 0x3) << 15);
        const uint32_t anisotropicFiltering = Config::AnisotropicFiltering;
        lookup = &g_samplerLookups[(key * 0x9E3779B1u) >> (32 - 6)];
        if (lookup->key == key && lookup->anisotropicFiltering == anisotropicFiltering)
        {
            auto& samplerDesc = g_samplerDescs[args.index];
            if (memcmp(&samplerDesc, &lookup->desc, sizeof(RenderSamplerDesc)) != 0)
            {
                samplerDesc = lookup->desc;
                SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.samplerIndices[args.index], lookup->descriptorIndex);
#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
                UpdateTextureSlotTables(args.index);
#endif
            }

            g_rendererStats.samplerCacheHits++;
            return;
        }

        lookup->key = key;
        lookup->anisotropicFiltering = anisotropicFiltering;
    }
#endif

    const auto addressU = ConvertTextureAddressMode((args.data0 >> 10) & 0x7);
    const auto addressV = ConvertTextureAddressMode((args.data0 >> 13) & 0x7);
    const auto addressW = ConvertTextureAddressMode((args.data0 >> 16) & 0x7);
    auto magFilter = ConvertTextureFilter((args.data3 >> 19) & 0x3);
    auto minFilter = ConvertTextureFilter((args.data3 >> 21) & 0x3);
    auto mipFilter = ConvertTextureFilter((args.data3 >> 23) & 0x3);
    const auto borderColor = ConvertBorderColor(args.data5 & 0x3);

#if defined(__SWITCH__)
    // Read once, so that a cached description matches the setting it is keyed by.
    const uint32_t anisotropicFiltering = lookup != nullptr ? lookup->anisotropicFiltering : uint32_t(Config::AnisotropicFiltering);
    bool anisotropyEnabled = anisotropicFiltering > 0 && mipFilter == RenderFilter::LINEAR;
#else
    bool anisotropyEnabled = Config::AnisotropicFiltering > 0 && mipFilter == RenderFilter::LINEAR;
#endif
    if (anisotropyEnabled)
    {
        magFilter = RenderFilter::LINEAR;
        minFilter = RenderFilter::LINEAR;
    }

    auto& samplerDesc = g_samplerDescs[args.index];

    bool dirty = false;

    SetDirtyValue(dirty, samplerDesc.addressU, addressU);
    SetDirtyValue(dirty, samplerDesc.addressV, addressV);
    SetDirtyValue(dirty, samplerDesc.addressW, addressW);
    SetDirtyValue(dirty, samplerDesc.minFilter, minFilter);
    SetDirtyValue(dirty, samplerDesc.magFilter, magFilter);
    SetDirtyValue(dirty, samplerDesc.mipmapMode, RenderMipmapMode(mipFilter));
#if defined(__SWITCH__)
    SetDirtyValue(dirty, samplerDesc.maxAnisotropy, anisotropyEnabled ? anisotropicFiltering : 16u);
#else
    SetDirtyValue(dirty, samplerDesc.maxAnisotropy, anisotropyEnabled ? Config::AnisotropicFiltering : 16u);
#endif
    SetDirtyValue(dirty, samplerDesc.anisotropyEnabled, anisotropyEnabled);
    SetDirtyValue(dirty, samplerDesc.borderColor, borderColor);

    if (dirty)
    {
        auto& [descriptorIndex, sampler] = g_samplerStates[XXH3_64bits(&samplerDesc, sizeof(RenderSamplerDesc))];
        if (descriptorIndex == NULL)
        {
            descriptorIndex = g_samplerStates.size();
            sampler = g_device->createSampler(samplerDesc);

            g_samplerDescriptorSet->setSampler(descriptorIndex - 1, sampler.get());
        }

        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.samplerIndices[args.index], descriptorIndex - 1);

#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
        // The slot's gatherable bit depends on its sampler's filters.
        UpdateTextureSlotTables(args.index);
#endif
    }

#if defined(__SWITCH__)
    // The slot's description is now this state's, and its descriptor index the description's.
    if (lookup != nullptr)
    {
        lookup->desc = samplerDesc;
        lookup->descriptorIndex = g_sharedConstants.samplerIndices[args.index];
    }
#endif
}

// g_vertexShaderConstants and g_pixelShaderConstants hold the constants in host byte order: they are swapped once
// here, when the game sets them, instead of the whole 4 KB and 3.5 KB blocks at every upload. The upload
// buffer receives the same bytes.
//
// Stores the byte-swapped words and says whether any of them changed, in one pass over them (instead of swap,
// compare and copy). The stored values are the same.
static bool StoreSwappedConstants(uint32_t* destination, const uint32_t* source, uint32_t count)
{
    uint32_t difference = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t value = ByteSwap(source[i]);
        difference |= destination[i] ^ value;
        destination[i] = value;
    }

    return difference != 0;
}

static void ProcSetVertexShaderConstants(const RenderCommand& cmd)
{
    auto& args = cmd.setVertexShaderConstants;
    assert((args.index * sizeof(uint32_t) + args.size) <= sizeof(g_vertexShaderConstants));

    // The game often sets constants to the values they already have. Only a real change needs the 4 KB block
    // to be uploaded again: when nothing changed, the block uploaded last in this command list holds these
    // values (the frame start and the port's copy draws mark the block dirty themselves).
    if (StoreSwappedConstants(&g_vertexShaderConstants[args.index], reinterpret_cast<const uint32_t*>(args.memory),
        args.size / sizeof(uint32_t)))
    {
        g_dirtyStates.vertexShaderConstants = true;
    }
#if defined(__SWITCH__)
    else
    {
        g_rendererStats.constantSetsUnchanged++;
    }

    g_rendererStats.constantSets++;
#endif
}

static void ProcSetPixelShaderConstants(const RenderCommand& cmd)
{
    auto& args = cmd.setPixelShaderConstants;
    assert((args.index * sizeof(uint32_t) + args.size) <= sizeof(g_pixelShaderConstants));

    if (StoreSwappedConstants(&g_pixelShaderConstants[args.index], reinterpret_cast<const uint32_t*>(args.memory),
        args.size / sizeof(uint32_t)))
    {
        g_dirtyStates.pixelShaderConstants = true;
    }
#if defined(__SWITCH__)
    else
    {
        g_rendererStats.constantSetsUnchanged++;
    }

    g_rendererStats.constantSets++;
#endif
}

static void ProcAddPipeline(const RenderCommand& cmd)
{
    auto& args = cmd.addPipeline;
    auto& pipeline = g_pipelines[args.hash];

    if (pipeline == nullptr)
    {
        pipeline = std::unique_ptr<RenderPipeline>(args.pipeline);
#if defined(__SWITCH__)
        g_pipelineGeneration++; // SwitchPipelineLookupCache: a lookup may now find it.
        g_rendererStats.pipelineVariantsAdded++;
#endif
#ifdef ASYNC_PSO_DEBUG
        ++g_pipelinesCreatedAsynchronously;
#endif
    }
    else
    {
#ifdef ASYNC_PSO_DEBUG
        ++g_pipelinesDropped;
#endif
        delete args.pipeline;
    }
}

static constexpr int32_t COMMON_DEPTH_BIAS_VALUE = int32_t((1 << 24) * 0.002f);
static constexpr float COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE = 1.0f;

static void FlushRenderStateForRenderThread()
{
    auto renderTarget = g_pipelineState.colorWriteEnable ? g_renderTarget : nullptr;
    auto depthStencil = g_pipelineState.zEnable || g_pipelineState.stencilEnable ? g_depthStencil : nullptr;

#if defined(__SWITCH__)
    g_resolveStats.draws++;
    if (g_pipelineState.enableConditionalSurvey)
    {
        g_resolveStats.surveyDraws++;
        g_conservativeBarriersUntilFrame = g_renderFrameCounter + 1; // SwitchPreciseBarriers (SurveyBarrierScope).
        g_surveyWritesPending = true;                                   // SwitchPreciseSurveyBarriers
    }

    if ((g_pipelineState.specConstants & SPEC_CONSTANT_CONDITIONAL_RENDERING) != 0)
        g_rendererStats.conditionalRenderingDraws++;

    if (g_passProfilerEnabled && PipelineHasNoPixelStage(g_pipelineState))
        g_rendererStats.drawsWithoutPixelStage++;

    // SwitchSkipOverwrittenClears: the clear waiting for this draw is made first, or skipped.
    if (g_deferredClear.pending)
        ResolveDeferredClear(renderTarget, depthStencil);

    g_resolveCopyTrigger = RESOLVE_COPY_BEFORE_DRAW;

    // SwitchCascadeAdoption: the map takes its held slices before a draw that samples it (perf8); a draw on the shadow
    // surface that cannot draw into the cascade image takes its content back, or continues in the next layer.
    if (g_cascade.surface != nullptr)
    {
        CascadeAdoptForDraw();
        CascadeBeforeDraw(renderTarget, depthStencil);
    }

    if (renderTarget != nullptr && !renderTarget->destinationTextures.empty())
        TryCoverageHandOver(renderTarget, depthStencil);

    // The depth buffer's pending copies: made first when this draw writes its depth, as always. When it only tests
    // depth (stencil writes do not change the depth aspect the copies read), they may wait (SwitchLazyResolves) while
    // nothing samples them, or the draw attaches it read-only and samples it itself (SwitchReadOnlyDepthSampling).
    GuestSurface* depthToResolve = depthStencil;
    bool depthReadOnly = false;
    if (depthStencil != nullptr && !depthStencil->destinationTextures.empty() &&
        !(g_pipelineState.zEnable && g_pipelineState.zWriteEnable))
    {
        if (g_switchRenderer.lazyResolves && CanDeferCopies(depthStencil))
        {
            MarkCopiesOwed(depthStencil);
            depthToResolve = nullptr;
        }
        else if (g_switchRenderer.readOnlyDepthSampling && CanSampleDepthReadOnly(depthStencil))
        {
            SampleDepthReadOnly(depthStencil);
            depthToResolve = nullptr;
            depthReadOnly = true;
        }
    }

    // SwitchUniformStencilClears: a draw that may write the stencil makes it unknown.
    if (depthStencil != nullptr && StencilMayBeWritten(g_pipelineState))
        depthStencil->stencilKnown = false;

    bool foundAny = PopulateBarriersForStretchRect(renderTarget, depthToResolve);
#else
    auto depthToResolve = depthStencil;
    bool foundAny = PopulateBarriersForStretchRect(renderTarget, depthStencil);
#endif

    for (const auto surface : g_pendingResolves)
    {
        bool isDepthStencil = RenderFormatIsDepth(surface->format);
        foundAny |= PopulateBarriersForStretchRect(isDepthStencil ? nullptr : surface, isDepthStencil ? surface : nullptr);
    }

    if (foundAny)
    {
        FlushBarriers();
        ExecutePendingStretchRectCommands(renderTarget, depthToResolve);

        for (const auto surface : g_pendingResolves)
        {
            bool isDepthStencil = RenderFormatIsDepth(surface->format);
            ExecutePendingStretchRectCommands(isDepthStencil ? nullptr : surface, isDepthStencil ? surface : nullptr);
        }
    }

    if (!g_pendingResolves.empty())
        g_pendingResolves.clear();

#if defined(__SWITCH__)
    // [Switch] SwitchEagerSampleTransitions. The colour surface the previous draw rendered to, now left for another one,
    // has pending resolves: its textures sample it from now on. Its transition to SHADER_READ goes out with this batch,
    // which the change of render target makes anyway, instead of in a batch of its own when a later draw of the new
    // pass first samples it (a wait for idle in the middle of the pass). Only the barrier moves: the surface is not
    // written until it is a target again, which moves it back with that draw's (or clear's) batch.
    if (g_switchRenderer.eagerSampleTransitions && g_lastColorSurface != nullptr && g_lastColorSurface != renderTarget &&
        renderTarget != nullptr && !g_lastColorSurface->destinationTextures.empty() &&
        g_lastColorSurface->sampleCount == RenderSampleCount::COUNT_1 && g_lastColorSurface != g_backBuffer)
    {
        if (g_lastColorSurface->layout != RenderTextureLayout::SHADER_READ)
            g_resolveStats.eagerTransitions++;

        AddBarrier(g_lastColorSurface, RenderTextureLayout::SHADER_READ);
    }

    if (renderTarget != nullptr)
        g_lastColorSurface = renderTarget;
#endif

    AddBarrier(renderTarget, RenderTextureLayout::COLOR_WRITE);
#if defined(__SWITCH__)
    if (CascadeActive(depthStencil))
        AddBarrier(&g_cascade.image, RenderTextureLayout::DEPTH_WRITE);
    else
        AddBarrier(depthStencil, depthReadOnly ? RenderTextureLayout::DEPTH_READ : RenderTextureLayout::DEPTH_WRITE);

    // SwitchStableFramebuffers (GetKeptAttachments), decided when SetFramebuffer below binds a framebuffer; otherwise the
    // bound one stays, and with it the attachments the draw before kept (its targets and tests are the same: changing
    // them marks renderTargetAndDepthStencil). Not for a coverage hand-over, which binds its own framebuffer, nor an
    // exact one with edge copies (their pipeline has no depth attachment).
    GuestSurface* attachTarget = renderTarget;
    GuestSurface* attachDepth = depthStencil;
    if (g_switchRenderer.stableFramebuffers && !g_coverageFixup.pending)
    {
        // A kept attachment that has left its attachment layout since (a slot samples it now) is attached no more.
        const uint8_t kept = g_pipelineState.keptAttachments;
        if (((kept & KEPT_COLOR_ATTACHMENT) != 0 && g_renderTarget != nullptr && g_renderTarget->layout != RenderTextureLayout::COLOR_WRITE) ||
            ((kept & KEPT_DEPTH_ATTACHMENT) != 0 && g_depthStencil != nullptr && g_depthStencil->layout != RenderTextureLayout::DEPTH_WRITE))
        {
            g_dirtyStates.renderTargetAndDepthStencil = true;
        }

        if (g_dirtyStates.renderTargetAndDepthStencil || depthReadOnly != g_boundFramebufferDepthReadOnly)
        {
            uint8_t keptAttachments = 0;
            if (g_pendingEdgePixels.mode != PendingEdgePixels::Mode::COPY)
                keptAttachments = GetKeptAttachments(renderTarget, depthStencil, depthReadOnly, attachTarget, attachDepth);

            SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.keptAttachments, keptAttachments);
        }

        if (g_pipelineState.keptAttachments != 0)
            g_resolveStats.keptAttachmentDraws++;
    }

    if (!g_barrierMap.empty() && !g_dirtyStates.renderTargetAndDepthStencil && !g_coverageFixup.pending)
        g_resolveStats.midPassBarrierBatches++;
#else
    AddBarrier(depthStencil, RenderTextureLayout::DEPTH_WRITE);
#endif

    FlushBarriers();

#if defined(__SWITCH__)
    if (g_coverageFixup.pending)
    {
        BindCoverageFramebuffer();
    }
    else
    {
        g_framebufferDepthReadOnly = depthReadOnly;
        SetFramebuffer(attachTarget, attachDepth, false);
        g_framebufferDepthReadOnly = false;
    }

    FinishEdgePixels(renderTarget);
#else
    SetFramebuffer(renderTarget, depthStencil, false);
#endif
    FlushViewport();

    auto& commandList = g_commandLists[g_frame];

    // D3D12 resets depth bias values to the pipeline values, even if they are dynamic.
    // We can reduce unnecessary calls by making common depth bias values part of the pipeline.
    if (g_capabilities.dynamicDepthBias && g_backend == Backend::D3D12)
    {
        bool useDepthBias = (g_depthBias != 0) || (g_slopeScaledDepthBias != 0.0f);

        int32_t depthBias = useDepthBias ? COMMON_DEPTH_BIAS_VALUE : 0;
        float slopeScaledDepthBias = useDepthBias ? COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE : 0.0f;

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthBias, depthBias);
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.slopeScaledDepthBias, slopeScaledDepthBias);
    }

#if defined(__SWITCH__)
    // A coverage hand-over draws with the stencil variant (its fix-up puts the usual pipeline back).
    if (g_coverageFixup.pending)
    {
        commandList->setPipeline(g_coverageFixup.drawPipeline);
        commandList->setStencilReference(g_coverageFixup.reference);
        g_dirtyStates.pipelineState = false;
    }
#endif

    if (g_dirtyStates.pipelineState)
    {
        commandList->setPipeline(CreateGraphicsPipelineInRenderThread(g_pipelineState));

        // D3D12 resets the depth bias values. Check if they need to be set again.
        if (g_capabilities.dynamicDepthBias && g_backend == Backend::D3D12)
            g_dirtyStates.depthBias = (g_depthBias != g_pipelineState.depthBias) || (g_slopeScaledDepthBias != g_pipelineState.slopeScaledDepthBias);
    }

    if (g_dirtyStates.depthBias && g_capabilities.dynamicDepthBias)
        commandList->setDepthBias(g_depthBias, 0.0f, g_slopeScaledDepthBias);

#if defined(__SWITCH__)
    bool uploadVertexShaderConstants = g_dirtyStates.vertexShaderConstants;
    bool uploadPixelShaderConstants = g_dirtyStates.pixelShaderConstants;
    bool uploadSharedConstants = g_dirtyStates.sharedConstants;

    // [Switch] SwitchSkipUnusedPixelConstants: a draw without a fragment stage reads no pixel constants; they stay
    // dirty for the next draw that has one (the retry below uploads all three blocks together if it has to).
    const bool skippedPixelConstants = uploadPixelShaderConstants && g_switchRenderer.skipUnusedPixelConstants &&
        PipelineHasNoPixelStage(g_pipelineState);
    if (skippedPixelConstants)
        uploadPixelShaderConstants = false;

    // [Switch] SwitchTrimConstantUploads: a block uploaded for a shader that reads fewer registers does not cover
    // this one's; upload it again.
    const uint32_t vertexConstantBytes = ConstantBytesToUpload(g_pipelineState.vertexShader, sizeof(g_vertexShaderConstants));
    const uint32_t pixelConstantBytes = ConstantBytesToUpload(g_pipelineState.pixelShader, sizeof(g_pixelShaderConstants));
    if (vertexConstantBytes > g_vertexConstantBytesUploaded)
        uploadVertexShaderConstants = true;
    if (!skippedPixelConstants && pixelConstantBytes > g_pixelConstantBytesUploaded)
        uploadPixelShaderConstants = true;

    // Already in host byte order (StoreSwappedConstants): a plain copy.
    for (uint32_t attempt = 0; attempt < 2; attempt++)
    {
        if (uploadVertexShaderConstants)
        {
            auto vertexShaderConstants = g_uploadAllocators[g_frame].allocateConstants(g_vertexShaderConstants, vertexConstantBytes,
                sizeof(g_vertexShaderConstants), 0x100);
            g_vertexConstantBytesUploaded = vertexConstantBytes;
            g_rendererStats.vertexConstantBytes += vertexConstantBytes;
            g_rendererStats.constantBytesTrimmed += sizeof(g_vertexShaderConstants) - vertexConstantBytes;
            SetRootDescriptorForDraw(vertexShaderConstants, 0);
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
            g_constantsUboBinding.Record(0, vertexShaderConstants);
#endif
        }

        if (uploadPixelShaderConstants)
        {
            auto pixelShaderConstants = g_uploadAllocators[g_frame].allocateConstants(g_pixelShaderConstants, pixelConstantBytes,
                sizeof(g_pixelShaderConstants), 0x100);
            g_pixelConstantBytesUploaded = pixelConstantBytes;
            g_rendererStats.pixelConstantBytes += pixelConstantBytes;
            g_rendererStats.constantBytesTrimmed += sizeof(g_pixelShaderConstants) - pixelConstantBytes;
            SetRootDescriptorForDraw(pixelShaderConstants, 1);
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
            g_constantsUboBinding.Record(1, pixelShaderConstants);
#endif
        }

        if (uploadSharedConstants)
        {
            // The tables past the original 320 bytes are only copied while they are kept up to date (and so read:
            // g_textureSlotTables); the uniform buffer binding covers the whole block either way.
#ifdef MARATHON_RECOMP_SWITCH_TEXTURE_TABLES
            const uint32_t sharedConstantBytes = g_textureSlotTables ? uint32_t(sizeof(g_sharedConstants)) : SHARED_CONSTANTS_BASE_SIZE;
#else
            const uint32_t sharedConstantBytes = uint32_t(sizeof(g_sharedConstants));
#endif
            auto sharedConstants = g_uploadAllocators[g_frame].allocateConstants(&g_sharedConstants, sharedConstantBytes,
                sizeof(g_sharedConstants), 0x100);
            g_rendererStats.sharedConstantBytes += sharedConstantBytes;
            SetRootDescriptorForDraw(sharedConstants, 2);
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
            g_constantsUboBinding.Record(2, sharedConstants);
#endif
        }

#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
        if (!g_constantsUbo || g_constantsUboBinding.InOneBuffer())
            break;

        // A block rolled over into a new 16 MB upload buffer while the others stayed in the previous one (or one was
        // never uploaded in this command list). Upload all three again, into one buffer: the current one if they fit
        // there together, else a new one. (Without that, a buffer nearly filled by texture uploads could take the
        // first block and not the next, and the draw would keep the set 5 bound before, with the old values.)
        uploadVertexShaderConstants = true;
        uploadPixelShaderConstants = true;
        uploadSharedConstants = true;
        g_uploadAllocators[g_frame].ensureSpace(3 * 0x100 + sizeof(g_vertexShaderConstants) + sizeof(g_pixelShaderConstants) +
            sizeof(g_sharedConstants));
#else
        break;
#endif
    }

#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
    if (g_constantsUbo)
    {
        assert(g_constantsUboBinding.sets[0] != nullptr && g_constantsUboBinding.InOneBuffer());
        g_constantsUboBinding.Bind(commandList.get());
        PushRootAddressesIfNeeded();
    }
#endif
#else
    // Already in host byte order (StoreSwappedConstants): a plain copy.
    if (g_dirtyStates.vertexShaderConstants)
    {
        auto vertexShaderConstants = g_uploadAllocators[g_frame].allocate<false>(g_vertexShaderConstants, sizeof(g_vertexShaderConstants), 0x100);
        SetRootDescriptor(vertexShaderConstants, 0);
    }

    if (g_dirtyStates.pixelShaderConstants)
    {
        auto pixelShaderConstants = g_uploadAllocators[g_frame].allocate<false>(g_pixelShaderConstants, sizeof(g_pixelShaderConstants), 0x100);
        SetRootDescriptor(pixelShaderConstants, 1);
    }

    if (g_dirtyStates.sharedConstants)
    {
        auto sharedConstants = g_uploadAllocators[g_frame].allocate<false>(&g_sharedConstants, sizeof(g_sharedConstants), 0x100);
        SetRootDescriptor(sharedConstants, 2);
    }
#endif

    if (g_dirtyStates.vertexStreamFirst <= g_dirtyStates.vertexStreamLast)
    {
        commandList->setVertexBuffers(
            g_dirtyStates.vertexStreamFirst,
            g_vertexBufferViews + g_dirtyStates.vertexStreamFirst,
            g_dirtyStates.vertexStreamLast - g_dirtyStates.vertexStreamFirst + 1,
            g_inputSlots + g_dirtyStates.vertexStreamFirst);
    }

    if (g_dirtyStates.indices && (g_backend == Backend::D3D12 || g_indexBufferView.buffer.ref != nullptr))
        commandList->setIndexBuffer(&g_indexBufferView);

    g_dirtyStates = DirtyStates(false);

#if defined(__SWITCH__)
    if (skippedPixelConstants && !uploadPixelShaderConstants)
    {
        g_dirtyStates.pixelShaderConstants = true;
        g_rendererStats.pixelConstantsSkipped++;
    }
#endif
}

#if defined(__SWITCH__)
// SwitchFrameLog: a draw of the game, with the state its pipeline is made of and the textures it reads. `skipped`: why it
// was not drawn (SwitchSkipNoOpDraws, SwitchSkipRestoreDraws), or nullptr (called once its state is flushed).
static void FrameLogDraw(const char* kind, uint32_t count, const char* skipped)
{
    if (!g_frameLog.active)
        return;

    g_frameLog.draws++;
    const auto& state = g_pipelineState;
    const GuestShader* pixelShader = state.enableConditionalSurvey ? g_conditionalSurveyPSShader.get() : state.pixelShader;
    AppendFormat(g_frameLog.text, "[frame] D %s %u vs %s ps %s mask %X", kind, count,
        DrawProfilerShaderName(DrawProfilerShaderId(state.vertexShader), false).c_str(),
        DrawProfilerShaderName(DrawProfilerShaderId(pixelShader), false).c_str(), state.colorWriteEnable);
    if (state.alphaBlendEnable)
        AppendFormat(g_frameLog.text, " blend %u/%u/%u", uint32_t(state.srcBlend), uint32_t(state.destBlend), uint32_t(state.blendOp));
    if (state.zEnable)
        AppendFormat(g_frameLog.text, " z %u%s", uint32_t(state.zFunc), state.zWriteEnable ? " write" : "");
    if (state.stencilEnable)
        AppendFormat(g_frameLog.text, " stencil %u ref %u", uint32_t(state.stencilFunc), state.stencilRef);
    if ((state.specConstants & SPEC_CONSTANT_ALPHA_TEST) != 0)
        g_frameLog.text += " alphatest";
    if (state.enableAlphaToCoverage)
        g_frameLog.text += " a2c";
    if (g_sharedConstants.clipPlaneEnabled)
        g_frameLog.text += " clip";
    if (state.enableConditionalSurvey)
        AppendFormat(g_frameLog.text, " survey %u", g_sharedConstants.conditionalSurveyIndex);
    if ((state.specConstants & SPEC_CONSTANT_CONDITIONAL_RENDERING) != 0)
        g_frameLog.text += " conditional";
    if (skipped == nullptr && g_coverageFixup.pending)
        g_frameLog.text += " coverage";
    if (skipped == nullptr && PipelineHasNoPixelStage(state))
        g_frameLog.text += " no-pixel-stage";

    for (uint32_t slot = 0; slot < std::size(g_textures); slot++)
    {
        const GuestTexture* texture = g_textures[slot];
        if (texture == nullptr)
            continue;

        AppendFormat(g_frameLog.text, " t%u %06X %ux%u", slot, FrameLogId(texture), texture->width, texture->height);
        if (texture->sourceSurface != nullptr)
            AppendFormat(g_frameLog.text, "<%06X", FrameLogId(texture->sourceSurface));
    }

    if (skipped != nullptr)
        AppendFormat(g_frameLog.text, " SKIPPED %s", skipped);
    g_frameLog.text += '\n';
}

// SwitchGpuDrawProfiler: after every draw of the game's passes (with PassProfilerCountDraw), its timestamp and what it
// is grouped by.
static void DrawProfilerAfterDraw(uint32_t count)
{
    auto& frame = g_passProfilerFrames[g_frame];
    if (!g_drawProfilerEnabled || frame.passes.empty() || frame.draws.size() >= DRAW_PROFILER_POOL_SIZE * DRAW_PROFILER_MAX_POOLS)
        return;

    const auto& state = g_pipelineState;
    const bool colorWrites = state.colorWriteEnable != 0 && state.renderTargetFormat != RenderFormat::UNKNOWN;
    const bool depthTest = state.zEnable && state.depthStencilFormat != RenderFormat::UNKNOWN;

    DrawProfilerDraw draw{};
    draw.pass = uint32_t(frame.passes.size() - 1);
    draw.count = count;
    draw.vertexShader = DrawProfilerShaderId(state.vertexShader);

    // The fragment stage the pipeline has (CreateGraphicsPipeline): the survey's while a conditional survey runs, none
    // when PixelShaderRemovedFromPipeline dropped it.
    if (state.enableConditionalSurvey)
    {
        draw.state |= DRAW_PROFILER_SURVEY;
        draw.pixelShader = DrawProfilerShaderId(g_conditionalSurveyPSShader.get());
    }
    else if (PipelineHasNoPixelStage(state))
    {
        draw.state |= DRAW_PROFILER_NO_PIXEL_SHADER;
    }
    else
    {
        draw.pixelShader = DrawProfilerShaderId(state.pixelShader);
    }

    if (colorWrites && state.alphaBlendEnable)
        draw.state |= DRAW_PROFILER_BLEND;
    if (depthTest && state.zWriteEnable)
        draw.state |= DRAW_PROFILER_Z_WRITE;
    if ((state.specConstants & SPEC_CONSTANT_ALPHA_TEST) != 0)
        draw.state |= DRAW_PROFILER_ALPHA_TEST;
    if (state.enableAlphaToCoverage)
        draw.state |= DRAW_PROFILER_ALPHA_TO_COVERAGE;
    if ((state.specConstants & SPEC_CONSTANT_REVERSE_Z) != 0)
        draw.state |= DRAW_PROFILER_REVERSE_Z;
    if ((state.specConstants & SPEC_CONSTANT_CONDITIONAL_RENDERING) != 0)
        draw.state |= DRAW_PROFILER_CONDITIONAL;
    if (g_sharedConstants.clipPlaneEnabled)
        draw.state |= DRAW_PROFILER_CLIP_PLANE;
    if (state.stencilEnable && state.depthStencilFormat != RenderFormat::UNKNOWN)
        draw.state |= DRAW_PROFILER_STENCIL;
    if (g_coverageFixup.pending)
        draw.state |= DRAW_PROFILER_COVERAGE;
    draw.state |= uint32_t(depthTest ? state.zFunc : RenderComparisonFunction::UNKNOWN) << DRAW_PROFILER_Z_FUNC_SHIFT;

    const uint32_t index = uint32_t(frame.draws.size());
    g_commandLists[g_frame]->writeTimestamp(frame.drawQueries[index / DRAW_PROFILER_POOL_SIZE].get(), index % DRAW_PROFILER_POOL_SIZE);
    frame.draws.push_back(draw);
}
#endif

static RenderPrimitiveTopology ConvertPrimitiveType(uint32_t primitiveType)
{
    switch (primitiveType)
    {
    case D3DPT_POINTLIST:
        return RenderPrimitiveTopology::POINT_LIST;
    case D3DPT_LINELIST:
        return RenderPrimitiveTopology::LINE_LIST;
    case D3DPT_LINESTRIP:
        return RenderPrimitiveTopology::LINE_STRIP;
    case D3DPT_TRIANGLELIST:
    case D3DPT_QUADLIST:
        return RenderPrimitiveTopology::TRIANGLE_LIST;
    case D3DPT_TRIANGLESTRIP:
        return RenderPrimitiveTopology::TRIANGLE_STRIP;
    case D3DPT_TRIANGLEFAN:
        return g_capabilities.triangleFan ? RenderPrimitiveTopology::TRIANGLE_FAN : RenderPrimitiveTopology::TRIANGLE_LIST;
    default:
        assert(false && "Unknown primitive type");
        return RenderPrimitiveTopology::UNKNOWN;
    }
}

static void SetPrimitiveType(uint32_t primitiveType)
{
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.primitiveTopology, ConvertPrimitiveType(primitiveType));
}

static void DrawPrimitive(GuestDevice* device, uint32_t primitiveType, uint32_t startVertex, uint32_t primitiveCount) 
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawPrimitive;
    cmd.drawPrimitive.primitiveType = primitiveType;
    cmd.drawPrimitive.startVertex = startVertex;
    cmd.drawPrimitive.primitiveCount = primitiveCount;

    queue.submit();
}

static void ProcDrawPrimitive(const RenderCommand& cmd)
{
    const auto& args = cmd.drawPrimitive;

    SetPrimitiveType(args.primitiveType);

#if defined(__SWITCH__)
    if (g_switchRenderer.skipNoOpDraws && IsNoOpDraw())
    {
        FrameLogDraw("prim", args.primitiveCount, "no-op");
        return;
    }
#endif

    FlushRenderStateForRenderThread();

#if defined(__SWITCH__)
    FrameLogDraw("prim", args.primitiveCount, nullptr);
    PassProfilerCountDraw();
#endif

    auto& commandList = g_commandLists[g_frame];
    commandList->drawInstanced(args.primitiveCount, 1, args.startVertex, 0);

#if defined(__SWITCH__)
    DrawProfilerAfterDraw(args.primitiveCount);
    FinishCoverageFixup();
#endif
}

static void DrawIndexedPrimitive(GuestDevice* device, uint32_t primitiveType, int32_t baseVertexIndex, uint32_t startIndex, uint32_t primCount)
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawIndexedPrimitive;
    cmd.drawIndexedPrimitive.primitiveType = primitiveType;
    cmd.drawIndexedPrimitive.baseVertexIndex = baseVertexIndex;
    cmd.drawIndexedPrimitive.startIndex = startIndex;
    cmd.drawIndexedPrimitive.primCount = primCount;

    queue.submit();
}

static void ProcDrawIndexedPrimitive(const RenderCommand& cmd)
{
    const auto& args = cmd.drawIndexedPrimitive;

    SetPrimitiveType(args.primitiveType);

#if defined(__SWITCH__)
    if (g_switchRenderer.skipNoOpDraws && IsNoOpDraw())
    {
        FrameLogDraw("indexed", args.primCount, "no-op");
        return;
    }
#endif

    FlushRenderStateForRenderThread();

#if defined(__SWITCH__)
    FrameLogDraw("indexed", args.primCount, nullptr);
    PassProfilerCountDraw();
#endif

    g_commandLists[g_frame]->drawIndexedInstanced(args.primCount, 1, args.startIndex, args.baseVertexIndex, 0);

#if defined(__SWITCH__)
    DrawProfilerAfterDraw(args.primCount);
    FinishCoverageFixup();
#endif
}

static void DrawPrimitiveUP(GuestDevice* device, uint32_t primitiveType, uint32_t primitiveCount, void* vertexStreamZeroData, uint32_t vertexStreamZeroStride)
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawPrimitiveUP;
    cmd.drawPrimitiveUP.primitiveType = primitiveType;
    cmd.drawPrimitiveUP.primitiveCount = primitiveCount;
    cmd.drawPrimitiveUP.vertexStreamZeroData = CurrentIntermediaryUploadAllocator().allocate(vertexStreamZeroData, primitiveCount * vertexStreamZeroStride);
    cmd.drawPrimitiveUP.vertexStreamZeroSize = primitiveCount * vertexStreamZeroStride;
    cmd.drawPrimitiveUP.vertexStreamZeroStride = vertexStreamZeroStride;
    cmd.drawPrimitiveUP.csdFilterState = g_csdFilterState;
    
    queue.submit();
}

static void ProcDrawPrimitiveUP(const RenderCommand& cmd)
{
    const auto& args = cmd.drawPrimitiveUP;

    SetPrimitiveType(args.primitiveType);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexStrides[0], uint8_t(args.vertexStreamZeroStride));

    auto allocation = g_uploadAllocators[g_frame].allocate<true>(reinterpret_cast<const uint32_t*>(args.vertexStreamZeroData), args.vertexStreamZeroSize, 0x4);

    auto& vertexBufferView = g_vertexBufferViews[0];
    vertexBufferView.size = args.primitiveCount * args.vertexStreamZeroStride;
    vertexBufferView.buffer = allocation.buffer->at(allocation.offset);
    g_inputSlots[0].stride = args.vertexStreamZeroStride;
    g_dirtyStates.vertexStreamFirst = 0;

    uint32_t indexCount = 0;

    if (args.primitiveType == D3DPT_QUADLIST)
        indexCount = g_quadIndexData.prepare(args.primitiveCount);
    else if (!g_capabilities.triangleFan && args.primitiveType == D3DPT_TRIANGLEFAN)
        indexCount = g_triangleFanIndexData.prepare(args.primitiveCount);

    if (args.csdFilterState != CsdFilterState::Unknown &&
        (g_pipelineState.pixelShader == g_csdShader || g_pipelineState.pixelShader == g_csdFilterShader.get()))
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.pixelShader,
            args.csdFilterState == CsdFilterState::On ? g_csdFilterShader.get() : g_csdShader);
    }

#if defined(__SWITCH__)
    if (g_switchRenderer.skipNoOpDraws && IsNoOpDraw())
    {
        FrameLogDraw("up", args.primitiveCount, "no-op");
        return;
    }

    // The draw's vertices (guest data, big-endian) for the coverage proofs and the restore detection.
    g_currentDrawVertices.data = reinterpret_cast<const uint8_t*>(args.vertexStreamZeroData);
    g_currentDrawVertices.count = args.primitiveCount;
    g_currentDrawVertices.stride = args.vertexStreamZeroStride;
    g_currentDrawVertices.primitiveType = args.primitiveType;

    if (g_switchRenderer.skipRestoreDraws && IsIdentityRestore())
    {
        g_currentDrawVertices.data = nullptr;
        FrameLogDraw("up", args.primitiveCount, "restore");
        return;
    }
#endif

    FlushRenderStateForRenderThread();

#if defined(__SWITCH__)
    g_currentDrawVertices.data = nullptr;
    FrameLogDraw("up", args.primitiveCount, nullptr);
    PassProfilerCountDraw();
#endif

    if (indexCount != 0)
        g_commandLists[g_frame]->drawIndexedInstanced(indexCount, 1, 0, 0, 0);
    else
        g_commandLists[g_frame]->drawInstanced(args.primitiveCount, 1, 0, 0);

#if defined(__SWITCH__)
    DrawProfilerAfterDraw(indexCount != 0 ? indexCount : args.primitiveCount);
    FinishCoverageFixup();
#endif
}

static const char* ConvertDeclUsage(uint32_t usage)
{
    switch (usage)
    {
    case D3DDECLUSAGE_POSITION:
        return "POSITION";
    case D3DDECLUSAGE_BLENDWEIGHT:
        return "BLENDWEIGHT";
    case D3DDECLUSAGE_BLENDINDICES:
        return "BLENDINDICES";
    case D3DDECLUSAGE_NORMAL:
        return "NORMAL";
    case D3DDECLUSAGE_PSIZE:
        return "PSIZE";
    case D3DDECLUSAGE_TEXCOORD:
        return "TEXCOORD";
    case D3DDECLUSAGE_TANGENT:
        return "TANGENT";
    case D3DDECLUSAGE_BINORMAL:
        return "BINORMAL";
    case D3DDECLUSAGE_TESSFACTOR:
        return "TESSFACTOR";
    case D3DDECLUSAGE_POSITIONT:
        return "POSITIONT";
    case D3DDECLUSAGE_COLOR:
        return "COLOR";
    case D3DDECLUSAGE_FOG:
        return "FOG";
    case D3DDECLUSAGE_DEPTH:
        return "DEPTH";
    case D3DDECLUSAGE_SAMPLE:
        return "SAMPLE";
    default:
        assert(false && "Unknown usage");
        return "UNKNOWN";
    }
}

static RenderFormat ConvertDeclType(uint32_t type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1:
        return RenderFormat::R32_FLOAT;
    case D3DDECLTYPE_FLOAT2:
        return RenderFormat::R32G32_FLOAT;
    case D3DDECLTYPE_FLOAT3:
        return RenderFormat::R32G32B32_FLOAT;
    case D3DDECLTYPE_FLOAT4:
        return RenderFormat::R32G32B32A32_FLOAT;
    case D3DDECLTYPE_D3DCOLOR:
        return RenderFormat::B8G8R8A8_UNORM;
    case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_UBYTE4_2:
        return RenderFormat::R8G8B8A8_UINT;
    case D3DDECLTYPE_SHORT2:
        return RenderFormat::R16G16_SINT;
    case D3DDECLTYPE_SHORT4:
        return RenderFormat::R16G16B16A16_SINT;
    case D3DDECLTYPE_UBYTE4N:
    case D3DDECLTYPE_UBYTE4N_2:
        return RenderFormat::R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2N:
        return RenderFormat::R16G16_SNORM;
    case D3DDECLTYPE_SHORT4N:
        return RenderFormat::R16G16B16A16_SNORM;
    case D3DDECLTYPE_USHORT2N:
        return RenderFormat::R16G16_UNORM;
    case D3DDECLTYPE_USHORT4N:
        return RenderFormat::R16G16B16A16_UNORM;
    case D3DDECLTYPE_UINT1:
        return RenderFormat::R32_UINT;
    case D3DDECLTYPE_DEC3N_2:
    case D3DDECLTYPE_DEC3N_3:
        return RenderFormat::R32_UINT;
    case D3DDECLTYPE_FLOAT16_2:
        return RenderFormat::R16G16_FLOAT;
    case D3DDECLTYPE_FLOAT16_4:
        return RenderFormat::R16G16B16A16_FLOAT;
    default:
        assert(false && "Unknown type");
        return RenderFormat::UNKNOWN;
    }
}

static GuestVertexDeclaration* CreateVertexDeclarationWithoutAddRef(GuestVertexElement* vertexElements) 
{
    size_t vertexElementCount = 0;
    auto vertexElement = vertexElements;

    while (vertexElement->stream != 0xFF && vertexElement->type != D3DDECLTYPE_UNUSED)
    {
        vertexElement->padding = 0;
        ++vertexElement;
        ++vertexElementCount;
    }

    vertexElement->padding = 0; // Clear the padding in D3DDECL_END() 

    std::lock_guard lock(g_vertexDeclarationMutex);

    XXH64_hash_t hash = XXH3_64bits(vertexElements, vertexElementCount * sizeof(GuestVertexElement));
    auto& vertexDeclaration = g_vertexDeclarations[hash];

    if (vertexDeclaration == nullptr)
    {
        vertexDeclaration = g_userHeap.AllocPhysical<GuestVertexDeclaration>(ResourceType::VertexDeclaration);
        vertexDeclaration->hash = hash;

        static std::vector<RenderInputElement> inputElements;
        inputElements.clear();

        struct Location
        {
            uint32_t usage;
            uint32_t usageIndex;
            uint32_t location;
        };

        // Should match the locations defined in XenosRecomp.
        constexpr Location locations[] =
        {
            { D3DDECLUSAGE_POSITION, 0, 0 },
            { D3DDECLUSAGE_POSITION, 1, 1 },
            { D3DDECLUSAGE_POSITION, 2, 2 },
            { D3DDECLUSAGE_POSITION, 3, 3 },
            { D3DDECLUSAGE_NORMAL, 0, 4 },
            { D3DDECLUSAGE_NORMAL, 1, 5 },
            { D3DDECLUSAGE_NORMAL, 2, 6 },
            { D3DDECLUSAGE_NORMAL, 3, 7 },
            { D3DDECLUSAGE_TANGENT, 0, 8 },
            { D3DDECLUSAGE_TANGENT, 1, 9 },
            { D3DDECLUSAGE_TANGENT, 2, 10 },
            { D3DDECLUSAGE_TANGENT, 3, 11 },
            { D3DDECLUSAGE_BINORMAL, 0, 12 },
            { D3DDECLUSAGE_TEXCOORD, 0, 13 },
            { D3DDECLUSAGE_TEXCOORD, 1, 14 },
            { D3DDECLUSAGE_TEXCOORD, 2, 15 },
            { D3DDECLUSAGE_TEXCOORD, 3, 16 },
            { D3DDECLUSAGE_COLOR, 0, 17 },
            { D3DDECLUSAGE_BLENDINDICES, 0, 18 },
            { D3DDECLUSAGE_BLENDWEIGHT, 0, 19 },
        };

        vertexElement = vertexElements;
        while (vertexElement->stream != 0xFF && vertexElement->type != D3DDECLTYPE_UNUSED)
        {
            uint32_t resolvedLocation = ~0;
            for (auto& location : locations)
            {
                if (location.usage == vertexElement->usage && location.usageIndex == vertexElement->usageIndex)
                {
                    resolvedLocation = location.location;
                    break;
                }
            }

            if (resolvedLocation == ~0)
            {
                // Bound but not used by any guest shaders.
                ++vertexElement;
                continue;
            }

            auto& inputElement = inputElements.emplace_back();
            inputElement.semanticName = ConvertDeclUsage(vertexElement->usage);
            inputElement.semanticIndex = vertexElement->usageIndex;
            inputElement.location = resolvedLocation;
            inputElement.format = ConvertDeclType(vertexElement->type);
            inputElement.slotIndex = vertexElement->stream;
            inputElement.alignedByteOffset = vertexElement->offset;

            switch (vertexElement->usage)
            {
            case D3DDECLUSAGE_NORMAL:
                switch (vertexElement->type)
                {
                case D3DDECLTYPE_SHORT2:
                case D3DDECLTYPE_SHORT4:
                case D3DDECLTYPE_SHORT2N:
                case D3DDECLTYPE_SHORT4N:
                case D3DDECLTYPE_USHORT2N:
                case D3DDECLTYPE_USHORT4N:
                case D3DDECLTYPE_FLOAT16_2:
                case D3DDECLTYPE_FLOAT16_4:
                    vertexDeclaration->swappedNormals |= 1 << vertexElement->usageIndex;
                    break;
                }

                break;
            case D3DDECLUSAGE_BINORMAL:
                switch (vertexElement->type)
                {
                case D3DDECLTYPE_SHORT2:
                case D3DDECLTYPE_SHORT4:
                case D3DDECLTYPE_SHORT2N:
                case D3DDECLTYPE_SHORT4N:
                case D3DDECLTYPE_USHORT2N:
                case D3DDECLTYPE_USHORT4N:
                case D3DDECLTYPE_FLOAT16_2:
                case D3DDECLTYPE_FLOAT16_4:
                    vertexDeclaration->swappedBinormals |= 1 << vertexElement->usageIndex;
                    break;
                }

                break;
            case D3DDECLUSAGE_TANGENT:
                switch (vertexElement->type)
                {
                case D3DDECLTYPE_SHORT2:
                case D3DDECLTYPE_SHORT4:
                case D3DDECLTYPE_SHORT2N:
                case D3DDECLTYPE_SHORT4N:
                case D3DDECLTYPE_USHORT2N:
                case D3DDECLTYPE_USHORT4N:
                case D3DDECLTYPE_FLOAT16_2:
                case D3DDECLTYPE_FLOAT16_4:
                    vertexDeclaration->swappedTangents |= 1 << vertexElement->usageIndex;
                    break;
                }

                break;
            case D3DDECLUSAGE_BLENDWEIGHT:
                switch (vertexElement->type)
                {
                case D3DDECLTYPE_SHORT2:
                case D3DDECLTYPE_SHORT4:
                case D3DDECLTYPE_SHORT2N:
                case D3DDECLTYPE_SHORT4N:
                case D3DDECLTYPE_USHORT2N:
                case D3DDECLTYPE_USHORT4N:
                case D3DDECLTYPE_FLOAT16_2:
                case D3DDECLTYPE_FLOAT16_4:
                    vertexDeclaration->swappedBlendWeights |= 1 << vertexElement->usageIndex;
                    break;
                }

                break;

            case D3DDECLUSAGE_TEXCOORD:
                switch (vertexElement->type)
                {
                case D3DDECLTYPE_SHORT2:
                case D3DDECLTYPE_SHORT4:
                case D3DDECLTYPE_SHORT2N:
                case D3DDECLTYPE_SHORT4N:
                case D3DDECLTYPE_USHORT2N:
                case D3DDECLTYPE_USHORT4N:
                case D3DDECLTYPE_FLOAT16_2:
                case D3DDECLTYPE_FLOAT16_4:
                    vertexDeclaration->swappedTexcoords |= 1 << vertexElement->usageIndex;
                    break;
                }

                break;
            }

            vertexDeclaration->vertexStreams[vertexElement->stream] = true;

            ++vertexElement;
        }

        auto addInputElement = [&](uint32_t usage, uint32_t usageIndex)
            {
                uint32_t location = ~0;

                for (auto& alsoLocation : locations)
                {
                    if (alsoLocation.usage == usage && alsoLocation.usageIndex == usageIndex)
                    {
                        location = alsoLocation.location;
                        break;
                    }
                }

                assert(location != ~0);

                for (auto& inputElement : inputElements)
                {
                    if (inputElement.location == location)
                        return;
                }

                auto format = RenderFormat::R32_FLOAT;
                switch (usage)
                {
                case D3DDECLUSAGE_NORMAL:
                case D3DDECLUSAGE_TANGENT:
                case D3DDECLUSAGE_BINORMAL:
                case D3DDECLUSAGE_BLENDINDICES:
                    format = RenderFormat::R32G32B32_FLOAT;
                    break;
                }

                inputElements.emplace_back(ConvertDeclUsage(usage), usageIndex, location, format, 15, 0);
            };

        // Assign any unbound usages to null buffer slot.
        for (auto& location : locations)
        {
            addInputElement(location.usage, location.usageIndex);
        }

        vertexDeclaration->inputElements = std::make_unique<RenderInputElement[]>(inputElements.size());
        std::copy(inputElements.begin(), inputElements.end(), vertexDeclaration->inputElements.get());

        vertexDeclaration->vertexElements = std::make_unique<GuestVertexElement[]>(vertexElementCount + 1);
        std::copy(vertexElements, vertexElements + vertexElementCount + 1, vertexDeclaration->vertexElements.get());

        vertexDeclaration->inputElementCount = uint32_t(inputElements.size());
        vertexDeclaration->vertexElementCount = vertexElementCount + 1;
    }

    vertexDeclaration->AddRef();
    return vertexDeclaration;
}

static GuestVertexDeclaration* CreateVertexDeclaration(GuestVertexElement* vertexElements)
{
    auto vertexDeclaration = CreateVertexDeclarationWithoutAddRef(vertexElements);
    vertexDeclaration->AddRef();
    return vertexDeclaration;
}

static void SetVertexDeclaration(GuestDevice* device, GuestVertexDeclaration* vertexDeclaration) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetVertexDeclaration;
    cmd.setVertexDeclaration.vertexDeclaration = vertexDeclaration;
    EnqueueRenderCommand(cmd);

    device->vertexDeclaration = g_memory.MapVirtual(vertexDeclaration);
}

static void ProcSetVertexDeclaration(const RenderCommand& cmd)
{
    auto& args = cmd.setVertexDeclaration;

    if (args.vertexDeclaration != nullptr)
    {
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.swappedTexcoords, args.vertexDeclaration->swappedTexcoords);
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.swappedNormals, args.vertexDeclaration->swappedNormals);
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.swappedBinormals, args.vertexDeclaration->swappedBinormals);
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.swappedTangents, args.vertexDeclaration->swappedTangents);
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.swappedBlendWeights, args.vertexDeclaration->swappedBlendWeights);

        uint32_t specConstants = g_pipelineState.specConstants;
        if (args.vertexDeclaration->hasR11G11B10Normal)
            specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;
        else
            specConstants &= ~SPEC_CONSTANT_R11G11B10_NORMAL;

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);
    }
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexDeclaration, args.vertexDeclaration);
}

static ShaderCacheEntry* FindShaderCacheEntry(XXH64_hash_t hash)
{
    auto end = g_shaderCacheEntries + g_shaderCacheEntryCount;
    auto findResult = std::lower_bound(g_shaderCacheEntries, end, hash, [](ShaderCacheEntry& lhs, XXH64_hash_t rhs)
        {
            return lhs.hash < rhs;
        });

    return findResult != end && findResult->hash == hash ? findResult : nullptr;
}

static GuestShader* CreateShader(const be<uint32_t>* function, ResourceType resourceType)
{
    XXH64_hash_t hash = XXH3_64bits(function, function[1] + function[2]);

    auto findResult = FindShaderCacheEntry(hash);
    GuestShader* shader = nullptr;

    if (findResult == nullptr) {
        LOGF_WARNING("Shader of function {:x} is not found by value: {:x}", reinterpret_cast<uintptr_t>(function), hash);
        LOG_WARNING("Perhaps the path to the required shader will be printed before this error");
        __builtin_trap();
    }
    if (findResult != nullptr)
    {
        if (findResult->guestShader == nullptr)
        {
            shader = g_userHeap.AllocPhysical<GuestShader>(resourceType);

            if (hash == 0x85ED723035ECF535)
            {
                shader->shader = CREATE_SHADER(blend_color_alpha_ps);
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
                MARK_CONSTANTS_THROUGH_UBO(shader, g_blend_color_alpha_ps_spirv);
#endif
            }
            else if (hash == 0xB1086A4947A797DE)
            {
                shader->shader = CREATE_SHADER(csd_no_tex_vs);
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
                MARK_CONSTANTS_THROUGH_UBO(shader, g_csd_no_tex_vs_spirv);
#endif
            }
            else if (hash == 0xB4CAFC034A37C8A8)
            {
                shader->shader = CREATE_SHADER(csd_vs);
#ifdef MARATHON_RECOMP_SWITCH_CONSTANTS_UBO
                MARK_CONSTANTS_THROUGH_UBO(shader, g_csd_vs_spirv);
#endif
            }
            else
                shader->shaderCacheEntry = findResult;

            findResult->guestShader = shader;
        }
        else
        {
            shader = findResult->guestShader;
        }
    }

    if (shader == nullptr)
        shader = g_userHeap.AllocPhysical<GuestShader>(resourceType);
    else
        shader->AddRef();

    if (hash == 0x31173204A896098A)
        g_csdShader = shader;

    return shader;
}

static GuestShader* CreateVertexShader(const be<uint32_t>* function) 
{
    return CreateShader(function, ResourceType::VertexShader);
}

static void SetVertexShader(GuestDevice* device, GuestShader* shader)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetVertexShader;
    cmd.setVertexShader.shader = shader;
    EnqueueRenderCommand(cmd);
}

static void ProcSetVertexShader(const RenderCommand& cmd)
{
    GuestShader* shader = cmd.setVertexShader.shader;

    if (shader != nullptr &&
    shader->shaderCacheEntry != nullptr)
    {
        if (shader->shaderCacheEntry->hash == 0x3687D038CE7D0BEA || shader->shaderCacheEntry->hash == 0xB4DA7A442DBB16CC)
        {
            if (Config::RadialBlur == ERadialBlur::Enhanced)
                shader = g_enhancedBurnoutBlurVSShader.get();
        }
    }

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexShader, shader);
}

static void SetStreamSource(GuestDevice* device, uint32_t index, GuestBuffer* buffer, uint32_t offset, uint32_t stride) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetStreamSource;
    cmd.setStreamSource.index = index;
    cmd.setStreamSource.buffer = buffer;
    cmd.setStreamSource.offset = offset;
    cmd.setStreamSource.stride = stride;
    EnqueueRenderCommand(cmd);
}

static void ProcSetStreamSource(const RenderCommand& cmd)
{
    const auto& args = cmd.setStreamSource;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexStrides[args.index], uint8_t(args.buffer != nullptr ? args.stride : 0));

    bool dirty = false;

    SetDirtyValue(dirty, g_vertexBufferViews[args.index].buffer, args.buffer != nullptr ? args.buffer->buffer->at(args.offset) : RenderBufferReference{});
    SetDirtyValue(dirty, g_vertexBufferViews[args.index].size, args.buffer != nullptr ? (args.buffer->dataSize - args.offset) : 0u);
    SetDirtyValue(dirty, g_inputSlots[args.index].stride, args.buffer != nullptr ? args.stride : 0u);

    if (dirty)
    {
        g_dirtyStates.vertexStreamFirst = std::min<uint8_t>(g_dirtyStates.vertexStreamFirst, args.index);
        g_dirtyStates.vertexStreamLast = std::max<uint8_t>(g_dirtyStates.vertexStreamLast, args.index);
    }
}

static void SetIndices(GuestDevice* device, GuestBuffer* buffer) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetIndices;
    cmd.setIndices.buffer = buffer;
    EnqueueRenderCommand(cmd);
}

static void ProcSetIndices(const RenderCommand& cmd)
{
    const auto& args = cmd.setIndices;

    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.buffer, args.buffer != nullptr ? args.buffer->buffer->at(0) : RenderBufferReference{});
    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.format, args.buffer != nullptr ? args.buffer->format : RenderFormat::R16_UINT);
    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.size, args.buffer != nullptr ? args.buffer->dataSize : 0u);
}

static GuestShader* CreatePixelShader(const be<uint32_t>* function)
{
    return CreateShader(function, ResourceType::PixelShader);
}

static void SetPixelShader(GuestDevice* device, GuestShader* shader)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetPixelShader;
    cmd.setPixelShader.shader = shader;
    EnqueueRenderCommand(cmd);
}

static void ProcSetPixelShader(const RenderCommand& cmd)
{
    GuestShader* shader = cmd.setPixelShader.shader;

    if (shader != nullptr &&
        shader->shaderCacheEntry != nullptr)
    {
        if (shader->shaderCacheEntry->hash == 0xDA58F0110A8595D9 || shader->shaderCacheEntry->hash == 0x845A4EF989446C01)
        {
            if (Config::RadialBlur == ERadialBlur::Enhanced)
                shader = g_enhancedBurnoutBlurPSShader.get();
        }
    }

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.pixelShader, shader);
}

static void BeginConditionalSurvey(GuestDevice* device, uint32_t index)
{
    assert(index < CONDITIONAL_SURVEY_MAX && "Invalid conditional survey index.");

    RenderCommand cmd;
    cmd.type = RenderCommandType::SetConditionalSurvey;
    cmd.setConditionalSurvey.enabled = true;
    cmd.setConditionalSurvey.index = index;
    EnqueueRenderCommand(cmd);
}

static void EndConditionalSurvey(GuestDevice* device)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetConditionalSurvey;
    cmd.setConditionalSurvey.enabled = false;
    cmd.setConditionalSurvey.index = 0;
    EnqueueRenderCommand(cmd);
}

#ifdef MARATHON_RECOMP_SWITCH_SURVEY_SLOTS
// A slot no index has as its latest and nothing can still read (FreeRetiredSurveySlots). When none is left (thousands of
// surveys in two frames), one retired in a frame the GPU may still run, made safe the way the port reset every counter:
// zeroed between barriers, which order every earlier access to it before the new survey's. A slot whose generations ran
// out (after 2^32 - 2 surveys) is zeroed the same way and starts again at generation 1.
static uint32_t AllocateSurveySlot()
{
    auto& state = g_surveySlotState;
    uint32_t slot = 0;
    bool clear = false;

    if (!state.freeSlots.empty())
    {
        slot = state.freeSlots.front();
        state.freeSlots.pop_front();
    }
    else
    {
        for (auto& retired : state.retiredSlots)
        {
            if (!retired.empty())
            {
                slot = retired.back();
                retired.pop_back();
                clear = true;
                break;
            }
        }
    }

    // At most CONDITIONAL_SURVEY_MAX slots are some index's latest, far fewer than there are.
    assert(slot != 0 && "Every conditional survey slot is in use.");

    if (state.generations[slot] >= SURVEY_NEVER_SURVEYED - 1)
    {
        state.generations[slot] = 0;
        clear = true;
    }

    if (clear)
        ClearConditionalSurveyWords(slot, 1);

    return slot;
}

static void SetConditionalSurveySlot(bool enabled, uint32_t index)
{
    assert(index < CONDITIONAL_SURVEY_MAX && "Invalid conditional survey index.");

    auto& state = g_surveySlotState;
    uint32_t slot = 0;
    uint32_t generation = SURVEY_NEVER_SURVEYED;

    if (enabled)
    {
        // The port zeroed the index's counter here: the survey gets a fresh slot and generation instead.
        slot = AllocateSurveySlot();
        generation = ++state.generations[slot];

        if (state.lastSlot[index] != 0)
            state.retiredSlots[g_frame].push_back(state.lastSlot[index]);

        state.lastSlot[index] = slot;
        state.lastGeneration[index] = generation;
    }
    else if (state.lastSlot[index] != 0)
    {
        // The port set the index End sends (0): its latest survey.
        slot = state.lastSlot[index];
        generation = state.lastGeneration[index];
    }

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.conditionalSurveyIndex, slot);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.conditionalRenderingIndex, generation);
}
#endif

static void ProcSetConditionalSurvey(const RenderCommand& cmd)
{
#if defined(__SWITCH__)
    FrameLogConditional('S', cmd.setConditionalSurvey.enabled, cmd.setConditionalSurvey.index);
#endif
#ifdef MARATHON_RECOMP_SWITCH_SURVEY_SLOTS
    if (g_surveySlots)
    {
        SetConditionalSurveySlot(cmd.setConditionalSurvey.enabled, cmd.setConditionalSurvey.index);
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.enableConditionalSurvey, cmd.setConditionalSurvey.enabled);
        return;
    }
#endif

    if (cmd.setConditionalSurvey.enabled)
    {
        // Clear previous survey result first.
        ClearConditionalSurveyWords(cmd.setConditionalSurvey.index, 1);
    }

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.enableConditionalSurvey, cmd.setConditionalSurvey.enabled);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.conditionalSurveyIndex, cmd.setConditionalSurvey.index);
}

static void BeginConditionalRendering(GuestDevice* device, uint32_t index)
{
    assert(index < CONDITIONAL_SURVEY_MAX && "Invalid conditional rendering index.");

    RenderCommand cmd;
    cmd.type = RenderCommandType::SetConditionalRendering;
    cmd.setConditionalRendering.enabled = true;
    cmd.setConditionalRendering.index = index;
    EnqueueRenderCommand(cmd);
}

static void EndConditionalRendering(GuestDevice* device)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetConditionalRendering;
    cmd.setConditionalRendering.enabled = false;
    cmd.setConditionalRendering.index = 0;
    EnqueueRenderCommand(cmd);
}

static void ProcSetConditionalRendering(const RenderCommand& cmd)
{
#if defined(__SWITCH__)
    FrameLogConditional('Q', cmd.setConditionalRendering.enabled, cmd.setConditionalRendering.index);
#endif
    uint32_t specConstants = g_pipelineState.specConstants;
    if (cmd.setConditionalRendering.enabled)
        specConstants |= SPEC_CONSTANT_CONDITIONAL_RENDERING;
    else
        specConstants &= ~SPEC_CONSTANT_CONDITIONAL_RENDERING;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);

#ifdef MARATHON_RECOMP_SWITCH_SURVEY_SLOTS
    // SwitchSurveySlots: the word holds the generation of the slot in word 312 (no shader read the index).
    if (g_surveySlots)
        return;
#endif

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.conditionalRenderingIndex, cmd.setConditionalRendering.index);
}

static void SetClipPlane(GuestDevice* device, uint32_t index, const be<float>* plane)
{
    if (index != 0)
        return;

#if defined(__SWITCH__)
    // [Switch] Sent as a render command, in order with the draws, instead of written into the render thread's shared
    // constants from this thread (a data race: the plane took effect wherever the render thread happened to be, and
    // the dirty flag could be cleared by its next draw before that draw read the plane). The plane now applies
    // between the draws the game placed it between, which is what the render thread saw whenever it had caught up
    // with this thread; it also has to go through the batches (SwitchBatchRenderCommands) to stay in order.
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetClipPlane;
    cmd.setClipPlane.plane[0] = plane[0].get();
    cmd.setClipPlane.plane[1] = plane[1].get();
    cmd.setClipPlane.plane[2] = plane[2].get();
    cmd.setClipPlane.plane[3] = plane[3].get();
    EnqueueRenderCommand(cmd);
#else
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlane[0], plane[0].get());
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlane[1], plane[1].get());
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlane[2], plane[2].get());
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlane[3], plane[3].get());
#endif
}

#if defined(__SWITCH__)
static void ProcSetClipPlane(const RenderCommand& cmd)
{
    const auto& args = cmd.setClipPlane;
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlane[0], args.plane[0]);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlane[1], args.plane[1]);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlane[2], args.plane[2]);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.clipPlane[3], args.plane[3]);
}

static void ProcSignalFence(const RenderCommand& cmd)
{
    g_renderThreadFence.store(cmd.signalFence.value, std::memory_order_release);
    g_renderThreadFence.notify_all();
}
#endif

static void ProcessRenderCommand(const RenderCommand& cmd)
{
#if defined(__SWITCH__)
    // SwitchSkipOverwrittenClears: a waiting colour clear is made before any command that could see or record past it.
    if (g_deferredClear.pending && !KeepsDeferredClear(cmd.type))
        IssueDeferredClear();
#endif

    switch (cmd.type)
    {
    case RenderCommandType::SetRenderState:                    ProcSetRenderState(cmd); break;
    case RenderCommandType::DestructResource:                  ProcDestructResource(cmd); break;
    case RenderCommandType::UnlockTextureRect:                 ProcUnlockTextureRect(cmd); break;
    case RenderCommandType::UnlockBuffer16:                    ProcUnlockBuffer16(cmd); break;
    case RenderCommandType::UnlockBuffer32:                    ProcUnlockBuffer32(cmd); break;
    case RenderCommandType::DrawImGui:                         ProcDrawImGui(cmd); break;
    case RenderCommandType::ExecuteCommandList:                ProcExecuteCommandList(cmd); break;
    case RenderCommandType::BeginCommandList:                  ProcBeginCommandList(cmd); break;
    case RenderCommandType::StretchRect:                       ProcStretchRect(cmd); break;
    case RenderCommandType::SetRenderTarget:                   ProcSetRenderTarget(cmd); break;
    case RenderCommandType::SetDepthStencilSurface:            ProcSetDepthStencilSurface(cmd); break;
    case RenderCommandType::ExecutePendingStretchRectCommands: ProcExecutePendingStretchRectCommands(cmd); break;
    case RenderCommandType::Clear:                             ProcClear(cmd); break;
    case RenderCommandType::SetViewport:                       ProcSetViewport(cmd); break;
    case RenderCommandType::SetTexture:                        ProcSetTexture(cmd); break;
    case RenderCommandType::SetScissorRect:                    ProcSetScissorRect(cmd); break;
    case RenderCommandType::SetSamplerState:                   ProcSetSamplerState(cmd); break;
    case RenderCommandType::SetBooleans:                       ProcSetBooleans(cmd); break;
    case RenderCommandType::SetVertexShaderConstants:          ProcSetVertexShaderConstants(cmd); break;
    case RenderCommandType::SetPixelShaderConstants:           ProcSetPixelShaderConstants(cmd); break;
    case RenderCommandType::AddPipeline:                       ProcAddPipeline(cmd); break;
    case RenderCommandType::DrawPrimitive:                     ProcDrawPrimitive(cmd); break;
    case RenderCommandType::DrawIndexedPrimitive:              ProcDrawIndexedPrimitive(cmd); break;
    case RenderCommandType::DrawPrimitiveUP:                   ProcDrawPrimitiveUP(cmd); break;
    case RenderCommandType::SetVertexDeclaration:              ProcSetVertexDeclaration(cmd); break;
    case RenderCommandType::SetVertexShader:                   ProcSetVertexShader(cmd); break;
    case RenderCommandType::SetStreamSource:                   ProcSetStreamSource(cmd); break;
    case RenderCommandType::SetIndices:                        ProcSetIndices(cmd); break;
    case RenderCommandType::SetPixelShader:                    ProcSetPixelShader(cmd); break;
    case RenderCommandType::SetConditionalSurvey:              ProcSetConditionalSurvey(cmd); break;
    case RenderCommandType::SetConditionalRendering:           ProcSetConditionalRendering(cmd); break;
#if defined(__SWITCH__)
    case RenderCommandType::SetClipPlane:                      ProcSetClipPlane(cmd); break;
    case RenderCommandType::SignalFence:                       ProcSignalFence(cmd); break;
    case RenderCommandType::UnlockBufferSnapshot:              ProcUnlockBufferSnapshot(cmd); break;
    case RenderCommandType::ExecuteCommandBatch:
    {
        // SwitchZeroCopyBatches: the batch's commands, in place and in order (a batch never holds another one),
        // then its buffer goes back to the pool.
        const auto& args = cmd.executeCommandBatch;
        AddRenderCommandsTaken(args.count - 1); // The batch itself was counted when taken.
        for (uint32_t i = 0; i < args.count; i++)
            ProcessRenderCommand(args.batch->commands[i]);

        g_renderCommandBatchPool.GiveBack(args.batch);
        break;
    }
#endif
    default:                                                   assert(false && "Unrecognized render command type."); break;
    }
}

static std::thread g_renderThread([]
    {
#if defined(__SWITCH__)
    SwitchSetCurrentThreadPriority(0x2C); // must outrank guest spinners
#endif
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        GuestThread::SetThreadName(GetCurrentThreadId(), "Render Thread");
#endif

        RenderCommand commands[32];

#if defined(__SWITCH__)
        bool threadSetUp = false;
#endif

        while (true)
        {
#if defined(__SWITCH__)
            // SwitchIdleRenderThreadBatches: the D3D thread holds its batches longer while this thread waits.
            g_renderThreadWaiting.store(true, std::memory_order_relaxed);
#endif
            size_t count = g_renderQueue.wait_dequeue_bulk(commands, std::size(commands));

#if defined(__SWITCH__)
            g_renderThreadWaiting.store(false, std::memory_order_relaxed);
            AddRenderCommandsTaken(count);

            // This thread starts during static initialisation, before the configuration is loaded
            // (SwitchPerfInitRenderer), and before the CPU profiler's module is initialised.
            if (!threadSetUp && g_switchRendererConfigured.load(std::memory_order_acquire))
            {
                threadSetUp = true;
                os::switch_cpu_profiler::RegisterCurrentThread("render");

                // [Switch] SwitchHostThreadCores. libnx gives every thread the process's default core 0, the
                // one the game's main thread runs on, where each wake-up of this thread (0x2C, above every
                // guest thread) preempted it. Scheduling only: the thread may still run on every core.
                if (g_switchRenderer.hostThreadCores)
                    SwitchRendererSetThreadCore(1);
            }
#endif

            for (size_t i = 0; i < count; i++)
                ProcessRenderCommand(commands[i]);
        }
    });

struct GuestPictureData
{
    be<uint32_t> vtable;
    uint8_t flags;
    be<uint32_t> name;
    be<uint32_t> texture;
    be<uint32_t> type;
};

union Bxty {
    char _Buf[16];
    xpointer<uint32_t> _Ptr;
};

struct BEString
{
    char alval[1];
    Bxty bx;
    be<uint32_t> size;
    uint32_t res;

    const char* c_str() {
        uint32_t len = size.get();
        if (len == 0) {
            return alval;
        } else if (len < 16) {
            return bx._Buf;
        } else {
            return reinterpret_cast<const char*>(bx._Ptr.get());
        }
    }
};

struct GuestMyTexture
{
    be<uint32_t> vtable; // 0x0
    be<uint32_t> field0x4; // 0x4
    be<uint32_t> regIndex; // 0x8
    BEString str1; // 0xC
    BEString str2; // 0x28
    BEString str3; // 0x44
    be<uint32_t> byte60; // 0x60
    be<uint32_t> texture; // 0x64
    be<uint32_t> Surface[6]; // 0x64
    be<uint32_t> width; // 0x80
    be<uint32_t> height; // 0x84
    be<uint32_t> graphicsDevice; // 0x88
    be<uint32_t> guestDevice; // 0x8C
};

static RenderTextureDimension ConvertTextureDimension(ddspp::TextureType type)
{
    switch (type) 
    {
    case ddspp::Texture1D:
        return RenderTextureDimension::TEXTURE_1D;
    case ddspp::Texture2D:
    case ddspp::Cubemap:
        return RenderTextureDimension::TEXTURE_2D;
    case ddspp::Texture3D:
        return RenderTextureDimension::TEXTURE_3D;
    default:
        assert(false && "Unknown texture type from DDS.");
        return RenderTextureDimension::UNKNOWN;
    }
}

static RenderTextureViewDimension ConvertTextureViewDimension(ddspp::TextureType type)
{
    switch (type)
    {
    case ddspp::Texture1D:
        return RenderTextureViewDimension::TEXTURE_1D;
    case ddspp::Texture2D:
        return RenderTextureViewDimension::TEXTURE_2D;
    case ddspp::Texture3D:
        return RenderTextureViewDimension::TEXTURE_3D;
    case ddspp::Cubemap:
        return RenderTextureViewDimension::TEXTURE_CUBE;
    default:
        assert(false && "Unknown texture type from DDS.");
        return RenderTextureViewDimension::UNKNOWN;
    }
}

static RenderFormat ConvertDXGIFormat(ddspp::DXGIFormat format) 
{
    switch (format)
    {
    case ddspp::R32G32B32A32_TYPELESS:
        return RenderFormat::R32G32B32A32_TYPELESS;
    case ddspp::R32G32B32A32_FLOAT:
        return RenderFormat::R32G32B32A32_FLOAT;
    case ddspp::R32G32B32A32_UINT:
        return RenderFormat::R32G32B32A32_UINT;
    case ddspp::R32G32B32A32_SINT:
        return RenderFormat::R32G32B32A32_SINT;
    case ddspp::R32G32B32_TYPELESS:
        return RenderFormat::R32G32B32_TYPELESS;
    case ddspp::R32G32B32_FLOAT:
        return RenderFormat::R32G32B32_FLOAT;
    case ddspp::R32G32B32_UINT:
        return RenderFormat::R32G32B32_UINT;
    case ddspp::R32G32B32_SINT:
        return RenderFormat::R32G32B32_SINT;
    case ddspp::R16G16B16A16_TYPELESS:
        return RenderFormat::R16G16B16A16_TYPELESS;
    case ddspp::R16G16B16A16_FLOAT:
        return RenderFormat::R16G16B16A16_FLOAT;
    case ddspp::R16G16B16A16_UNORM:
        return RenderFormat::R16G16B16A16_UNORM;
    case ddspp::R16G16B16A16_UINT:
        return RenderFormat::R16G16B16A16_UINT;
    case ddspp::R16G16B16A16_SNORM:
        return RenderFormat::R16G16B16A16_SNORM;
    case ddspp::R16G16B16A16_SINT:
        return RenderFormat::R16G16B16A16_SINT;
    case ddspp::R32G32_TYPELESS:
        return RenderFormat::R32G32_TYPELESS;
    case ddspp::R32G32_FLOAT:
        return RenderFormat::R32G32_FLOAT;
    case ddspp::R32G32_UINT:
        return RenderFormat::R32G32_UINT;
    case ddspp::R32G32_SINT:
        return RenderFormat::R32G32_SINT;
    case ddspp::R8G8B8A8_TYPELESS:
        return RenderFormat::R8G8B8A8_TYPELESS;
    case ddspp::R8G8B8A8_UNORM:
        return RenderFormat::R8G8B8A8_UNORM;
    case ddspp::R8G8B8A8_UINT:
        return RenderFormat::R8G8B8A8_UINT;
    case ddspp::R8G8B8A8_SNORM:
        return RenderFormat::R8G8B8A8_SNORM;
    case ddspp::R8G8B8A8_SINT:
        return RenderFormat::R8G8B8A8_SINT;
    case ddspp::B8G8R8A8_UNORM:
        return RenderFormat::B8G8R8A8_UNORM;
    case ddspp::B8G8R8X8_UNORM:
        return RenderFormat::B8G8R8A8_UNORM;   
    case ddspp::R16G16_TYPELESS:
        return RenderFormat::R16G16_TYPELESS;
    case ddspp::R16G16_FLOAT:
        return RenderFormat::R16G16_FLOAT;
    case ddspp::R16G16_UNORM:
        return RenderFormat::R16G16_UNORM;
    case ddspp::R16G16_UINT:
        return RenderFormat::R16G16_UINT;
    case ddspp::R16G16_SNORM:
        return RenderFormat::R16G16_SNORM;
    case ddspp::R16G16_SINT:
        return RenderFormat::R16G16_SINT;
    case ddspp::R32_TYPELESS:
        return RenderFormat::R32_TYPELESS;
    case ddspp::D32_FLOAT:
        return RenderFormat::D32_FLOAT;
    case ddspp::R32_FLOAT:
        return RenderFormat::R32_FLOAT;
    case ddspp::R32_UINT:
        return RenderFormat::R32_UINT;
    case ddspp::R32_SINT:
        return RenderFormat::R32_SINT;
    case ddspp::R8G8_TYPELESS:
        return RenderFormat::R8G8_TYPELESS;
    case ddspp::R8G8_UNORM:
        return RenderFormat::R8G8_UNORM;
    case ddspp::R8G8_UINT:
        return RenderFormat::R8G8_UINT;
    case ddspp::R8G8_SNORM:
        return RenderFormat::R8G8_SNORM;
    case ddspp::R8G8_SINT:
        return RenderFormat::R8G8_SINT;
    case ddspp::R16_TYPELESS:
        return RenderFormat::R16_TYPELESS;
    case ddspp::R16_FLOAT:
        return RenderFormat::R16_FLOAT;
    case ddspp::D16_UNORM:
        return RenderFormat::D16_UNORM;
    case ddspp::R16_UNORM:
        return RenderFormat::R16_UNORM;
    case ddspp::R16_UINT:
        return RenderFormat::R16_UINT;
    case ddspp::R16_SNORM:
        return RenderFormat::R16_SNORM;
    case ddspp::R16_SINT:
        return RenderFormat::R16_SINT;
    case ddspp::R8_TYPELESS:
        return RenderFormat::R8_TYPELESS;
    case ddspp::R8_UNORM:
    case ddspp::A8_UNORM:
        return RenderFormat::R8_UNORM;
    case ddspp::R8_UINT:
        return RenderFormat::R8_UINT;
    case ddspp::R8_SNORM:
        return RenderFormat::R8_SNORM;
    case ddspp::R8_SINT:
        return RenderFormat::R8_SINT;
    case ddspp::BC1_TYPELESS:
        return RenderFormat::BC1_TYPELESS;
    case ddspp::BC1_UNORM:
        return RenderFormat::BC1_UNORM;
    case ddspp::BC1_UNORM_SRGB:
        return RenderFormat::BC1_UNORM_SRGB;
    case ddspp::BC2_TYPELESS:
        return RenderFormat::BC2_TYPELESS;
    case ddspp::BC2_UNORM:
        return RenderFormat::BC2_UNORM;
    case ddspp::BC2_UNORM_SRGB:
        return RenderFormat::BC2_UNORM_SRGB;
    case ddspp::BC3_TYPELESS:
        return RenderFormat::BC3_TYPELESS;
    case ddspp::BC3_UNORM:
        return RenderFormat::BC3_UNORM;
    case ddspp::BC3_UNORM_SRGB:
        return RenderFormat::BC3_UNORM_SRGB;
    case ddspp::BC4_TYPELESS:
        return RenderFormat::BC4_TYPELESS;
    case ddspp::BC4_UNORM:
        return RenderFormat::BC4_UNORM;
    case ddspp::BC4_SNORM:
        return RenderFormat::BC4_SNORM;
    case ddspp::BC5_TYPELESS:
        return RenderFormat::BC5_TYPELESS;
    case ddspp::BC5_UNORM:
        return RenderFormat::BC5_UNORM;
    case ddspp::BC5_SNORM:
        return RenderFormat::BC5_SNORM;
    case ddspp::BC6H_TYPELESS:
        return RenderFormat::BC6H_TYPELESS;
    case ddspp::BC6H_UF16:
        return RenderFormat::BC6H_UF16;
    case ddspp::BC6H_SF16:
        return RenderFormat::BC6H_SF16;
    case ddspp::BC7_TYPELESS:
        return RenderFormat::BC7_TYPELESS;
    case ddspp::BC7_UNORM:
        return RenderFormat::BC7_UNORM;
    case ddspp::BC7_UNORM_SRGB:
        return RenderFormat::BC7_UNORM_SRGB;
    default:
        printf("format: %x\n", format);
        assert(false && "Unsupported format from DDS.");
        return RenderFormat::UNKNOWN;
    }
}

static bool LoadTexture(GuestTexture& texture, const uint8_t* data, size_t dataSize, RenderComponentMapping componentMapping)
{
    ddspp::Descriptor ddsDesc;
    if (ddspp::decode_header((unsigned char *)(data), ddsDesc) != ddspp::Error)
    {
        RenderTextureDesc desc;
        desc.dimension = ConvertTextureDimension(ddsDesc.type);
        desc.width = ddsDesc.width;
        desc.height = ddsDesc.height;
        desc.depth = ddsDesc.depth;
        desc.mipLevels = ddsDesc.numMips;
        desc.arraySize = ddsDesc.type == ddspp::TextureType::Cubemap ? ddsDesc.arraySize * 6 : ddsDesc.arraySize;
        desc.format = ConvertDXGIFormat(ddsDesc.format);
        desc.flags = ddsDesc.type == ddspp::TextureType::Cubemap ? RenderTextureFlag::CUBE : RenderTextureFlag::NONE;

        RenderTextureDesc achieved = desc;
        texture.textureHolder = CreateTextureChecked(desc, "dds-texture", &achieved);
        texture.texture = texture.textureHolder.get();
        texture.layout = RenderTextureLayout::COPY_DEST;

        RenderTextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.dimension = ConvertTextureViewDimension(ddsDesc.type);
        viewDesc.mipLevels = ddsDesc.numMips;

        if (ddsDesc.format == ddspp::A8_UNORM)
        {
            // Map A8_UNORM to R8_UNORM for compatability
            componentMapping = RenderComponentMapping(RenderSwizzle::ZERO, RenderSwizzle::ZERO, RenderSwizzle::ZERO, RenderSwizzle::R);
        }

        viewDesc.componentMapping = componentMapping;
        texture.textureView = texture.texture->createTextureView(viewDesc);
        texture.descriptorIndex = g_textureDescriptorAllocator.allocate();
        // The descriptor's size is the image's, which CreateTextureChecked may have halved (texture.width keeps
        // the file's, as before).
        SetTextureDescriptor(texture.descriptorIndex, texture.texture, achieved.width, achieved.height, RenderTextureLayout::SHADER_READ,
            texture.textureView.get(), viewDesc.mipLevels, viewDesc.dimension == RenderTextureViewDimension::TEXTURE_2D ? achieved.arraySize : 1);

        texture.width = ddsDesc.width;
        texture.height = ddsDesc.height;
        texture.mipLevels = viewDesc.mipLevels;
        texture.viewDimension = viewDesc.dimension;

        struct Slice
        {
            uint32_t width;
            uint32_t height;
            uint32_t depth;
            uint32_t srcOffset;
            uint32_t dstOffset;
            uint32_t srcRowPitch;
            uint32_t dstRowPitch;
            uint32_t rowCount;
        };

        std::vector<Slice> slices;
        uint32_t curSrcOffset = 0;
        uint32_t curDstOffset = 0;

        for (uint32_t arraySlice = 0; arraySlice < desc.arraySize; arraySlice++)
        {
            for (uint32_t mipSlice = 0; mipSlice < ddsDesc.numMips; mipSlice++)
            {
                auto& slice = slices.emplace_back();

                slice.width = std::max(1u, ddsDesc.width >> mipSlice);
                slice.height = std::max(1u, ddsDesc.height >> mipSlice);
                slice.depth = std::max(1u, ddsDesc.depth >> mipSlice);
                slice.srcOffset = curSrcOffset;
                slice.dstOffset = curDstOffset;
                uint32_t rowPitch = ((slice.width + ddsDesc.blockWidth - 1) / ddsDesc.blockWidth) * ddsDesc.bitsPerPixelOrBlock;
                slice.srcRowPitch = (rowPitch + 7) / 8;
                slice.dstRowPitch = (slice.srcRowPitch + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
                slice.rowCount = (slice.height + ddsDesc.blockHeight - 1) / ddsDesc.blockHeight;

                curSrcOffset += slice.srcRowPitch * slice.rowCount * slice.depth;
                curDstOffset += (slice.dstRowPitch * slice.rowCount * slice.depth + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);
            }
        }

        auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(curDstOffset));
        uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

        for (auto& slice : slices)
        {
            const uint8_t* srcData = data + ddsDesc.headerSize + slice.srcOffset;
            uint8_t* dstData = mappedMemory + slice.dstOffset;

            if (slice.srcRowPitch == slice.dstRowPitch)
            {
                memcpy(dstData, srcData, slice.srcRowPitch * slice.rowCount * slice.depth);
            }
            else
            {
                for (size_t i = 0; i < slice.rowCount * slice.depth; i++)
                {
                    memcpy(dstData, srcData, slice.srcRowPitch);
                    srcData += slice.srcRowPitch;
                    dstData += slice.dstRowPitch;
                }
            }
        }

        uploadBuffer->unmap();

        ExecuteCopyCommandList([&]
            {
                g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.texture, RenderTextureLayout::COPY_DEST));

                for (size_t i = 0; i < slices.size(); i++)
                {
                    auto& slice = slices[i];

                    g_copyCommandList->copyTextureRegion(
                        RenderTextureCopyLocation::Subresource(texture.texture, i % desc.mipLevels, i / desc.mipLevels),
                        RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), desc.format, slice.width, slice.height, slice.depth, (slice.dstRowPitch * 8) / ddsDesc.bitsPerPixelOrBlock * ddsDesc.blockWidth, slice.dstOffset));
                }
            });

        return true;
    }
    else
    {
        int width, height;
        void* stbImage = stbi_load_from_memory(data, dataSize, &width, &height, nullptr, 4);

        if (stbImage != nullptr)
        {
            const RenderTextureDesc desc = RenderTextureDesc::Texture2D(width, height, 1, RenderFormat::R8G8B8A8_UNORM);
            RenderTextureDesc achieved = desc;
            texture.textureHolder = CreateTextureChecked(desc, "stb-texture", &achieved);
            texture.texture = texture.textureHolder.get();
            texture.viewDimension = RenderTextureViewDimension::TEXTURE_2D;
            texture.layout = RenderTextureLayout::COPY_DEST;

            texture.descriptorIndex = g_textureDescriptorAllocator.allocate();
            SetTextureDescriptor(texture.descriptorIndex, texture.texture, achieved.width, achieved.height, RenderTextureLayout::SHADER_READ,
                nullptr, achieved.mipLevels);

            uint32_t rowPitch = (width * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
            uint32_t slicePitch = rowPitch * height;

            auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(slicePitch));
            uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

            if (rowPitch == (width * 4))
            {
                memcpy(mappedMemory, stbImage, slicePitch);
            }
            else
            {
                auto data = reinterpret_cast<const uint8_t*>(stbImage);

                for (size_t i = 0; i < height; i++)
                {
                    memcpy(mappedMemory, data, width * 4);
                    data += width * 4;
                    mappedMemory += rowPitch;
                }
            }

            uploadBuffer->unmap();

            stbi_image_free(stbImage);

            ExecuteCopyCommandList([&]
                {
                    g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.texture, RenderTextureLayout::COPY_DEST));

                    g_copyCommandList->copyTextureRegion(
                        RenderTextureCopyLocation::Subresource(texture.texture, 0),
                        RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), RenderFormat::R8G8B8A8_UNORM, width, height, 1, rowPitch / 4, 0));
                });

            return true;
        }
    }

    return false;
}

std::unique_ptr<GuestTexture> LoadTexture(const uint8_t* data, size_t dataSize, RenderComponentMapping componentMapping)
{
    GuestTexture texture(ResourceType::Texture);

    if (LoadTexture(texture, data, dataSize, componentMapping))
        return std::make_unique<GuestTexture>(std::move(texture));

    return nullptr;
}

static void DiffPatchTexture(GuestTexture& texture, uint8_t* data, uint32_t dataSize)
{
    auto header = reinterpret_cast<BlockCompressionDiffPatchHeader*>(g_buttonBcDiff.get());
    auto entries = reinterpret_cast<BlockCompressionDiffPatchEntry*>(g_buttonBcDiff.get() + header->entriesOffset);
    auto end = entries + header->entryCount;
    
    auto hash = XXH3_64bits(data, dataSize);

    auto findResult = std::lower_bound(entries, end, hash, [](BlockCompressionDiffPatchEntry& lhs, XXH64_hash_t rhs)
    {
        return lhs.hash < rhs;
    });

    if (findResult != end && findResult->hash == hash)
    {
        auto patch = reinterpret_cast<BlockCompressionDiffPatch*>(g_buttonBcDiff.get() + findResult->patchesOffset);

        for (size_t i = 0; i < findResult->patchCount; i++)
        {
            assert(patch->destinationOffset + patch->patchBytesSize <= dataSize);
            memcpy(data + patch->destinationOffset, g_buttonBcDiff.get() + patch->patchBytesOffset, patch->patchBytesSize);
            ++patch;
        }

        GuestTexture patchedTexture(ResourceType::Texture);

        if (LoadTexture(patchedTexture, data, dataSize, {}))
            texture.patchedTexture = std::make_unique<GuestTexture>(std::move(patchedTexture));
    }
}

static void MakePictureData(GuestMyTexture* pictureData, uint8_t* data, uint32_t dataSize)
{
    if (data != nullptr)
    {
        GuestTexture texture(ResourceType::Texture);

        if (LoadTexture(texture, data, dataSize, {}))
        {
#ifdef _DEBUG
            if (pictureData->str1.size.get() > 0)
                texture.texture->setName(fmt::format("Texture {}", pictureData->str1.c_str()));
#endif
            DiffPatchTexture(texture, data, dataSize);

            pictureData->texture = g_memory.MapVirtual(g_userHeap.AllocPhysical<GuestTexture>(std::move(texture)));
            pictureData->width = texture.width;
            pictureData->height = texture.height;
        }
    }
}

void IndexBufferLengthMidAsmHook(PPCRegister& r3)
{
    r3.u64 *= 2;
}

void SetShadowResolutionMidAsmHook(PPCRegister& r11)
{
    auto res = (int32_t)Config::ShadowResolution.Value;

    if (res > 0)
        r11.u64 = res;
}

static void SetResolution(be<uint32_t>* device)
{
    Video::ComputeViewportDimensions();

    uint32_t width = uint32_t(round(Video::s_viewportWidth * Config::ResolutionScale));
    uint32_t height = uint32_t(round(Video::s_viewportHeight * Config::ResolutionScale));
    device[46] = width == 0 ? 880 : width;
    device[47] = height == 0 ? 720 : height;
}

static GuestShader* g_movieVertexShader;
static GuestShader* g_moviePixelShaderHD;
static GuestShader* g_moviePixelShaderSD;

static int ScreenShaderInit(be<uint32_t>* a1)
{
    if (g_movieVertexShader == nullptr)
    {
        g_movieVertexShader = g_userHeap.AllocPhysical<GuestShader>(ResourceType::VertexShader);
    }

    if (g_moviePixelShaderHD == nullptr)
    {
        g_moviePixelShaderHD = g_userHeap.AllocPhysical<GuestShader>(ResourceType::PixelShader);
    }

    if (g_moviePixelShaderSD == nullptr)
    {
        g_moviePixelShaderSD = g_userHeap.AllocPhysical<GuestShader>(ResourceType::PixelShader);
    }

    g_moviePixelShaderHD->AddRef();
    g_moviePixelShaderSD->AddRef();
    g_movieVertexShader->AddRef();

    a1[0x12] = g_memory.MapVirtual(g_movieVertexShader);
    a1[0x51] = g_memory.MapVirtual(g_moviePixelShaderHD);
    a1[0x52] = g_memory.MapVirtual(g_moviePixelShaderSD);

    return 0;
}

// Needed for correct clearing of index buffer
static bool IsSet() {
    return true;
}

void MovieRendererMidAsmHook(PPCRegister& r3)
{
    auto device = reinterpret_cast<GuestDevice*>(g_memory.Translate(r3.u32));

    // Force linear filtering & clamp addressing
    for (size_t i = 0; i < 3; i++)
    {
        device->samplerStates[i].data[0] = (device->samplerStates[i].data[0].get() & ~0x7fc00) | 0x24800;
        device->samplerStates[i].data[3] = (device->samplerStates[i].data[3].get() & ~0x1f80000) | 0x1280000;
    }

    device->dirtyFlags[3] = device->dirtyFlags[3].get() | 0xe0000000ull;
}

// Normally, we could delay setting IsMadeOne, but the game relies on that flag
// being present to handle load priority. To work around that, we can prevent
// IsMadeAll from being set until the compilation is finished. Time for a custom flag!
enum
{
    eDatabaseDataFlags_CompilingPipelines = 0x80
};

// This is passed to pipeline compilation threads to keep the loading screen busy until 
// all of them are finished. A shared pointer makes sure the destructor is called only once.
//struct PipelineTaskToken
//{
//    PipelineTaskType type{};
//    boost::shared_ptr<Hedgehog::Database::CDatabaseData> databaseData;
//
//    PipelineTaskToken() : databaseData()
//    {
//    }
//
//    PipelineTaskToken(const PipelineTaskToken&) = delete;
//
//    PipelineTaskToken(PipelineTaskToken&& other)
//        : type(std::exchange(other.type, PipelineTaskType::Null))
//        , databaseData(std::exchange(other.databaseData, nullptr))
//    {
//    }
//
//    ~PipelineTaskToken()
//    {
//        if (type != PipelineTaskType::Null)
//        {
//            if (databaseData.get() != nullptr)
//                databaseData->m_Flags &= ~eDatabaseDataFlags_CompilingPipelines;
//
//            if ((--g_compilingPipelineTaskCount) == 0)
//                g_compilingPipelineTaskCount.notify_one();
//        }
//    }
//};

//struct PipelineStateQueueItem
//{
//    XXH64_hash_t pipelineHash;
//    PipelineState pipelineState;
//    std::shared_ptr<PipelineTaskToken> token;
//#ifdef ASYNC_PSO_DEBUG
//    std::string pipelineName;
//#endif
//};

//static moodycamel::BlockingConcurrentQueue<PipelineStateQueueItem> g_pipelineStateQueue;

#if defined(__SWITCH__)
// [Switch] The pipeline compiler threads' queue, restored without the loading-time precompilation (Unleashed's
// Hedgehog database hooks, commented out above): pipelines the render thread wants but does not wait for, such as
// specialized variants of a state that give the same image as the state's own pipeline. The render thread keeps
// drawing with that one until the variant arrives through RenderCommandType::AddPipeline (ProcAddPipeline, which
// drops a duplicate and starts a new SwitchPipelineLookupCache generation).
struct SwitchPipelineQueueItem
{
    XXH64_hash_t pipelineHash;
    PipelineState pipelineState;
};

static moodycamel::BlockingConcurrentQueue<SwitchPipelineQueueItem> g_pipelineStateQueue;

// Render thread only: the variants already handed to the compiler threads (each is built once).
static ankerl::unordered_dense::set<XXH64_hash_t> g_pipelineVariantsRequested;

// Hands the pipeline of a sanitized state (of hash `hash`) to the compiler threads, once. Render thread only.
static void RequestPipelineVariant(const PipelineState& sanitizedState, XXH64_hash_t hash)
{
    if (g_pipelineVariantsRequested.emplace(hash).second)
    {
        g_pipelineStateQueue.enqueue(SwitchPipelineQueueItem{ hash, sanitizedState });
        g_rendererStats.pipelineVariantsRequested++;
    }
}

// The pipeline of a sanitized state with `bits` ORed into its spec constants, if it exists. Otherwise nullptr, and the
// compiler threads build it (once, at their low priority). Render thread only. Anything the choice of `bits` reads
// besides the raw state must join the SwitchPipelineLookupCache key.
[[maybe_unused]] static RenderPipeline* FindOrRequestPipelineVariant(const PipelineState& sanitizedState, uint32_t bits)
{
    PipelineState variant = sanitizedState;
    variant.specConstants |= bits;

    const XXH64_hash_t hash = XXH3_64bits(&variant, sizeof(variant));
    auto findResult = g_pipelines.find(hash);
    if (findResult != g_pipelines.end() && findResult->second != nullptr)
        return findResult->second.get();

    RequestPipelineVariant(variant, hash);
    return nullptr;
}
#endif

static void CompilePipeline(XXH64_hash_t pipelineHash, const PipelineState& pipelineState
#ifdef ASYNC_PSO_DEBUG
    , const std::string& pipelineName
#endif
)
{
    auto pipeline = CreateGraphicsPipeline(pipelineState);
#ifdef ASYNC_PSO_DEBUG
    pipeline->setName(pipelineName);
#endif

    // Will get dropped in render thread if a different thread already managed to compile this.
    RenderCommand cmd;
    cmd.type = RenderCommandType::AddPipeline;
    cmd.addPipeline.hash = pipelineHash;
    cmd.addPipeline.pipeline = pipeline.release();
    g_renderQueue.enqueue(cmd);
}

static void PipelineCompilerThread()
{
#if defined(__SWITCH__)
    SwitchSetCurrentThreadPriority(0x3B); // bulk compute: preemptive slot

    // Started during static initialisation; blocks until the render thread asks for a pipeline. Creating one reads
    // only the state and the shaders (host objects), so no guest context is needed.
    bool threadSetUp = false;
    while (true)
    {
        SwitchPipelineQueueItem queueItem;
        g_pipelineStateQueue.wait_dequeue(queueItem);

        if (!threadSetUp && g_switchRendererConfigured.load(std::memory_order_acquire))
        {
            threadSetUp = true;
            os::switch_cpu_profiler::RegisterCurrentThread("pipeline compiler");

            // SwitchHostThreadCores: prefer core 2, away from the game's main thread (core 0) and the render thread
            // (core 1). 0x3B is below every guest thread and the audio threads, which preempt it there.
            if (g_switchRenderer.hostThreadCores)
                SwitchRendererSetThreadCore(2);
        }

        CompilePipeline(queueItem.pipelineHash, queueItem.pipelineState
#ifdef ASYNC_PSO_DEBUG
            , fmt::format("VARIANT {:X}", queueItem.pipelineHash)
#endif
        );

        std::this_thread::yield();
    }
#else
#ifdef _WIN32
    int threadPriority = THREAD_PRIORITY_LOWEST;
    SetThreadPriority(GetCurrentThread(), threadPriority);
    GuestThread::SetThreadName(GetCurrentThreadId(), "Pipeline Compiler Thread");
#endif

    std::unique_ptr<GuestThreadContext> ctx;

//    while (true)
//    {
//        PipelineStateQueueItem queueItem;
//        g_pipelineStateQueue.wait_dequeue(queueItem);
//
//        if (ctx == nullptr)
//            ctx = std::make_unique<GuestThreadContext>(0);
//
//#ifdef _WIN32
//        int newThreadPriority = threadPriority;
//
//        bool loading = *SWA::SGlobals::ms_IsLoading;
//        if (loading)
//            newThreadPriority = THREAD_PRIORITY_HIGHEST;
//        else
//            newThreadPriority = THREAD_PRIORITY_LOWEST;
//
//        if (newThreadPriority != threadPriority)
//        {
//            SetThreadPriority(GetCurrentThread(), newThreadPriority);
//            threadPriority = newThreadPriority;
//        }
//#endif
//
//        CompilePipeline(queueItem.pipelineHash, queueItem.pipelineState
//#ifdef ASYNC_PSO_DEBUG
//            , queueItem.pipelineName.c_str()
//#endif
//        );
//
//        std::this_thread::yield();
//    }
#endif
}

static std::vector<std::unique_ptr<std::thread>> g_pipelineCompilerThreads = []()
    {
        size_t threadCount = std::max(2u, (std::thread::hardware_concurrency() * 2) / 3);

        std::vector<std::unique_ptr<std::thread>> threads(threadCount);
        for (auto& thread : threads)
            thread = std::make_unique<std::thread>(PipelineCompilerThread);

        return threads;
    }();

static constexpr uint32_t MODEL_DATA_VFTABLE = 0x82073A44;
static constexpr uint32_t TERRAIN_MODEL_DATA_VFTABLE = 0x8211D25C;
static constexpr uint32_t PARTICLE_MATERIAL_VFTABLE = 0x8211F198;

// Allocate the shared pointer only when new compilations are happening.
// If nothing was compiled, the local "token" variable will get destructed with RAII instead.
struct PipelineTaskTokenPair
{
//    PipelineTaskToken token;
//    std::shared_ptr<PipelineTaskToken> sharedToken;
};

// Having this separate, because I don't want to lock a mutex in the render thread before
// every single draw. Might be worth profiling to see if it actually has an impact and merge them.
static xxHashMap<PipelineState> g_asyncPipelineStates;

//static void EnqueueGraphicsPipelineCompilation(
//    const PipelineState& pipelineState,
//    PipelineTaskTokenPair& tokenPair,
//    const char* name,
//    bool isPrecompiledPipeline = false)
//{
//    XXH64_hash_t hash = XXH3_64bits(&pipelineState, sizeof(pipelineState));
//    bool shouldCompile = g_asyncPipelineStates.emplace(hash, pipelineState).second;
//
//    if (shouldCompile)
//    {
//        bool loading = *SWA::SGlobals::ms_IsLoading;
//        if (!loading && isPrecompiledPipeline)
//        {
//            // We can just compile here during the logos.
//            CompilePipeline(hash, pipelineState
//#ifdef ASYNC_PSO_DEBUG
//                , fmt::format("CACHE {} {:X}", name, hash)
//#endif
//            );
//        }
//        else
//        {
//            if (tokenPair.sharedToken == nullptr && tokenPair.token.type != PipelineTaskType::Null)
//                tokenPair.sharedToken = std::make_shared<PipelineTaskToken>(std::move(tokenPair.token));
//
//            PipelineStateQueueItem queueItem;
//            queueItem.pipelineHash = hash;
//            queueItem.pipelineState = pipelineState;
//            queueItem.token = tokenPair.sharedToken;
//#ifdef ASYNC_PSO_DEBUG
//            queueItem.pipelineName = fmt::format("ASYNC {} {:X}", name, hash);
//#endif
//            g_pipelineStateQueue.enqueue(queueItem);
//        }
//    }
//
//#ifdef PSO_CACHING_CLEANUP
//    if (shouldCompile && isPrecompiledPipeline)
//    {
//        std::lock_guard lock(g_pipelineCacheMutex);
//        g_pipelineStatesToCache.emplace(hash, pipelineState);
//    }
//#endif
//
//#ifdef PSO_CACHING
//    if (!isPrecompiledPipeline)
//    {
//        std::lock_guard lock(g_pipelineCacheMutex);
//        g_pipelineStatesToCache.erase(hash);
//    }
//#endif
//}

struct CompilationArgs
{
    PipelineTaskTokenPair tokenPair;
    bool noGI{};
    bool hasMoreThanOneBone{};
    bool velocityMapQuickStep{};
    bool objectIcon{};
};

enum class MeshLayer
{
    Opaque,
    Transparent,
    PunchThrough,
    Special
};

struct Mesh
{
    uint32_t vertexSize{};
    uint32_t morphTargetVertexSize{};
    GuestVertexDeclaration* vertexDeclaration{};
//    Hedgehog::Mirage::CMaterialData* material{};
    MeshLayer layer{};
    bool morphModel{};
};

//static void CompileMeshPipeline(const Mesh& mesh, CompilationArgs& args)
//{
//    if (mesh.material == nullptr || mesh.material->m_spShaderListData.get() == nullptr)
//        return;
//
//    auto& shaderList = mesh.material->m_spShaderListData;
//
//    bool isFur = !mesh.morphModel && !args.instancing &&
//        strstr(shaderList->m_TypeAndName.c_str(), "Fur") != nullptr;
//
//    bool isSky = !mesh.morphModel && !args.instancing &&
//        strstr(shaderList->m_TypeAndName.c_str(), "Sky") != nullptr;
//
//    bool isSonicMouth = !mesh.morphModel && !args.instancing &&
//        strcmp(mesh.material->m_TypeAndName.c_str() + 2, "sonic_gm_mouth_duble") == 0 &&
//        strcmp(shaderList->m_TypeAndName.c_str() + 3, "SonicSkin_dspf[b]") == 0;
//
//    bool compiledOutsideMainFramebuffer = !args.instancing && !isFur && !isSky;
//
//    bool constTexCoord;
//    if (args.instancing)
//    {
//        constTexCoord = false;
//    }
//    else
//    {
//        constTexCoord = true;
//        if (mesh.material->m_spTexsetData.get() != nullptr)
//        {
//            for (size_t i = 1; i < mesh.material->m_spTexsetData->m_TextureList.size(); i++)
//            {
//                if (mesh.material->m_spTexsetData->m_TextureList[i]->m_TexcoordIndex !=
//                    mesh.material->m_spTexsetData->m_TextureList[0]->m_TexcoordIndex)
//                {
//                    constTexCoord = false;
//                    break;
//                }
//            }
//        }
//    }
//
//    // Shadow pipeline.
//    // if (compiledOutsideMainFramebuffer && (mesh.layer == MeshLayer::Opaque || mesh.layer == MeshLayer::PunchThrough))
//    // {
//    //     PipelineState pipelineState{};
//
//    //     if (mesh.layer == MeshLayer::PunchThrough)
//    //     {
//    //         pipelineState.vertexShader = FindShaderCacheEntry(0xDD4FA7BB53876300)->guestShader;
//    //         pipelineState.pixelShader = FindShaderCacheEntry(0xE2ECA594590DDE8B)->guestShader;
//    //     }
//    //     else
//    //     {
//    //         pipelineState.vertexShader = FindShaderCacheEntry(0x8E4BB23465BD909E)->guestShader;
//    //     }
//
//    //     pipelineState.vertexDeclaration = mesh.vertexDeclaration;
//    //     pipelineState.cullMode = mesh.material->m_DoubleSided ? RenderCullMode::NONE : RenderCullMode::BACK;
//    //     pipelineState.zFunc = RenderComparisonFunction::LESS_EQUAL;
//
//    //     if (g_capabilities.dynamicDepthBias)
//    //     {
//    //         // Put common depth bias values for reducing unnecessary calls.
//    //         if (g_backend == Backend::D3D12)
//    //         {
//    //             pipelineState.depthBias = COMMON_DEPTH_BIAS_VALUE;
//    //             pipelineState.slopeScaledDepthBias = COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE;
//    //         }
//    //     }
//    //     else
//    //     {
//    //         pipelineState.depthBias = (1 << 24) * (*reinterpret_cast<be<float>*>(g_memory.Translate(0x83302760)));
//    //         pipelineState.slopeScaledDepthBias = *reinterpret_cast<be<float>*>(g_memory.Translate(0x83302764));
//    //     }
//
//    //     pipelineState.colorWriteEnable = 0;
//    //     pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
//    //     pipelineState.vertexStrides[0] = mesh.vertexSize;
//    //     pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;
//
//    //     if (mesh.layer == MeshLayer::PunchThrough)
//    //         pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
//
//    //     const char* name = (mesh.layer == MeshLayer::PunchThrough ? "MakeShadowMapTransparent" : "MakeShadowMap");
//    //     SanitizePipelineState(pipelineState);
//    //     EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, name);
//
//    //     // Morph models have 4 targets where unused targets default to the first vertex stream.
//    //     if (mesh.morphModel)
//    //     {
//    //         for (size_t i = 0; i < 5; i++)
//    //         {
//    //             for (size_t j = 0; j < 4; j++)
//    //                 pipelineState.vertexStrides[j + 1] = i > j ? mesh.morphTargetVertexSize : mesh.vertexSize;
//
//    //             SanitizePipelineState(pipelineState);
//    //             EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, name);
//    //         }
//    //     }
//    // }
//
//    // // Motion blur pipeline. We could normally do the player here only, but apparently Werehog enemies also have object blur.
//    // // TODO: Do punch through meshes get rendered?
//    // if (!mesh.morphModel && compiledOutsideMainFramebuffer && args.hasMoreThanOneBone && mesh.layer == MeshLayer::Opaque)
//    // {
//    //     PipelineState pipelineState{};
//    //     pipelineState.vertexShader = FindShaderCacheEntry(0x4620B236DC38100C)->guestShader;
//    //     pipelineState.pixelShader = FindShaderCacheEntry(0xBBDB735BEACC8F41)->guestShader;
//    //     pipelineState.vertexDeclaration = mesh.vertexDeclaration;
//    //     pipelineState.cullMode = RenderCullMode::NONE;
//    //     pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL;
//    //     pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
//    //     pipelineState.vertexStrides[0] = mesh.vertexSize;
//    //     pipelineState.renderTargetFormat = RenderFormat::R8G8B8A8_UNORM;
//    //     pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;
//    //     pipelineState.specConstants = SPEC_CONSTANT_REVERSE_Z;
//
//    //     SanitizePipelineState(pipelineState);
//    //     EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, "FxVelocityMap");
//
//    //     if (args.velocityMapQuickStep)
//    //     {
//    //         pipelineState.vertexShader = FindShaderCacheEntry(0x99DC3F27E402700D)->guestShader;
//    //         SanitizePipelineState(pipelineState);
//    //         EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, "FxVelocityMapQuickStep");
//    //     }
//    // }
//
//    uint32_t defaultStr = args.instancing ? 0x820C8734 : 0x8202DDBC; // "instancing" for instancing, "default" for regular
//    guest_stack_var<Hedgehog::Base::CStringSymbol> defaultSymbol(reinterpret_cast<const char*>(g_memory.Translate(defaultStr)));
//    auto defaultFindResult = shaderList->m_PixelShaderPermutations.find(*defaultSymbol);
//    if (defaultFindResult == shaderList->m_PixelShaderPermutations.end())
//        return;
//
//    uint32_t pixelShaderSubPermutationsToCompile = 0;
//    if (constTexCoord) pixelShaderSubPermutationsToCompile |= 0x1;
//    if (args.noGI) pixelShaderSubPermutationsToCompile |= 0x2;
//
//    if ((defaultFindResult->second.m_SubPermutations.get() & (1 << pixelShaderSubPermutationsToCompile)) == 0) pixelShaderSubPermutationsToCompile &= ~0x1;
//    if ((defaultFindResult->second.m_SubPermutations.get() & (1 << pixelShaderSubPermutationsToCompile)) == 0) pixelShaderSubPermutationsToCompile &= ~0x2;
//
//    uint32_t noneStr = mesh.morphModel ? 0x820D72F0 : 0x8200D938; // "p" for morph, "none" for regular
//    guest_stack_var<Hedgehog::Base::CStringSymbol> noneSymbol(reinterpret_cast<const char*>(g_memory.Translate(noneStr)));
//    auto noneFindResult = defaultFindResult->second.m_VertexShaderPermutations.find(*noneSymbol);
//    if (noneFindResult == defaultFindResult->second.m_VertexShaderPermutations.end())
//        return;
//
//    uint32_t vertexShaderSubPermutationsToCompile = 0;
//    if (constTexCoord) vertexShaderSubPermutationsToCompile |= 0x1;
//
//    if ((noneFindResult->second->m_SubPermutations.get() & (1 << vertexShaderSubPermutationsToCompile)) == 0)
//        vertexShaderSubPermutationsToCompile &= ~0x1;
//
//    auto vertexDeclaration = mesh.vertexDeclaration;
//    bool instancing = args.instancing || isFur;
//
//    if (instancing)
//    {
//        GuestVertexElement vertexElements[64];
//        memcpy(vertexElements, mesh.vertexDeclaration->vertexElements.get(), (mesh.vertexDeclaration->vertexElementCount - 1) * sizeof(GuestVertexElement));
//
//        if (args.instancing)
//        {
//            vertexElements[mesh.vertexDeclaration->vertexElementCount - 1] = { 1, 0, 0x2A23B9, 0, 5, 4 };
//            vertexElements[mesh.vertexDeclaration->vertexElementCount] = { 1, 12, 0x2C2159, 0, 5, 5 };
//            vertexElements[mesh.vertexDeclaration->vertexElementCount + 1] = { 1, 16, 0x2C2159, 0, 5, 6 };
//            vertexElements[mesh.vertexDeclaration->vertexElementCount + 2] = { 1, 20, 0x182886, 0, 10, 1 };
//            vertexElements[mesh.vertexDeclaration->vertexElementCount + 3] = { 2, 0, 0x2C82A1, 0, 0, 1 };
//            vertexElements[mesh.vertexDeclaration->vertexElementCount + 4] = D3DDECL_END();
//        }
//        else if (isFur)
//        {
//            vertexElements[mesh.vertexDeclaration->vertexElementCount - 1] = { 1, 0, 0x2C82A1, 0, 0, 1 };
//            vertexElements[mesh.vertexDeclaration->vertexElementCount] = { 2, 0, 0x2C83A4, 0, 0, 2 };
//            vertexElements[mesh.vertexDeclaration->vertexElementCount + 1] = D3DDECL_END();
//        }
//
//        vertexDeclaration = CreateVertexDeclarationWithoutAddRef(vertexElements);
//    }
//
//    for (auto& [pixelShaderSubPermutations, pixelShader] : defaultFindResult->second.m_PixelShaders)
//    {
//        if (pixelShader.get() == nullptr || (pixelShaderSubPermutations & 0x3) != pixelShaderSubPermutationsToCompile)
//            continue;
//
//        for (auto& [vertexShaderSubPermutations, vertexShader] : noneFindResult->second->m_VertexShaders)
//        {
//            if (vertexShader.get() == nullptr || (vertexShaderSubPermutations & 0x1) != vertexShaderSubPermutationsToCompile)
//                continue;
//
//            PipelineState pipelineState{};
//            pipelineState.vertexShader = reinterpret_cast<GuestShader*>(vertexShader->m_spCode->m_pD3DVertexShader.get());
//            pipelineState.pixelShader = reinterpret_cast<GuestShader*>(pixelShader->m_spCode->m_pD3DPixelShader.get());
//            pipelineState.vertexDeclaration = vertexDeclaration;
//            pipelineState.instancing = instancing;
//            pipelineState.zWriteEnable = !isSky && mesh.layer != MeshLayer::Transparent;
//            pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
//            pipelineState.destBlend = mesh.material->m_Additive ? RenderBlend::ONE : RenderBlend::INV_SRC_ALPHA;
//            pipelineState.cullMode = mesh.material->m_DoubleSided ? RenderCullMode::NONE : RenderCullMode::BACK;
//            pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL; // Reverse Z
//            pipelineState.alphaBlendEnable = mesh.layer == MeshLayer::Transparent || mesh.layer == MeshLayer::Special;
//            pipelineState.srcBlendAlpha = RenderBlend::SRC_ALPHA;
//            pipelineState.destBlendAlpha = RenderBlend::INV_SRC_ALPHA;
//            pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
//            pipelineState.vertexStrides[0] = mesh.vertexSize;
//
//            if (args.instancing)
//            {
//                pipelineState.vertexStrides[1] = 24;
//                pipelineState.vertexStrides[2] = 4;
//            }
//            else if (isFur)
//            {
//                pipelineState.vertexStrides[1] = 4;
//                pipelineState.vertexStrides[2] = 4;
//            }
//
//            pipelineState.renderTargetFormat = RenderFormat::R16G16B16A16_FLOAT;
//            pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT_S8_UINT;
//            pipelineState.sampleCount = Config::AntiAliasing != EAntiAliasing::None ? int32_t(Config::AntiAliasing.Value) : 1;
//
//            if (pipelineState.vertexDeclaration->hasR11G11B10Normal)
//                pipelineState.specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;
//
//            if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
//                pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;
//
//            if (mesh.layer == MeshLayer::PunchThrough)
//            {
//                if (Config::AntiAliasing != EAntiAliasing::None && Config::TransparencyAntiAliasing)
//                {
//                    pipelineState.enableAlphaToCoverage = true;
//                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
//                }
//                else
//                {
//                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
//                }
//            }
//
//            if (!isSky)
//                pipelineState.specConstants |= SPEC_CONSTANT_REVERSE_Z;
//
//            auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate)
//                {
//                    SanitizePipelineState(pipelineStateToCreate);
//                    EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, args.tokenPair, shaderList->m_TypeAndName.c_str() + 3);
//
//                    // Morph models have 4 targets where unused targets default to the first vertex stream.
//                    if (mesh.morphModel)
//                    {
//                        for (size_t i = 0; i < 5; i++)
//                        {
//                            for (size_t j = 0; j < 4; j++)
//                                pipelineStateToCreate.vertexStrides[j + 1] = i > j ? mesh.morphTargetVertexSize : mesh.vertexSize;
//
//                            SanitizePipelineState(pipelineStateToCreate);
//                            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, args.tokenPair, shaderList->m_TypeAndName.c_str() + 3);
//                        }
//                    }
//                };
//
//            createGraphicsPipeline(pipelineState);
//
//            // We cannot rely on this being accurate during loading as SceneEffect.prm.xml gets loaded a bit later.
//            bool planarReflectionEnabled = *reinterpret_cast<bool*>(g_memory.Translate(0x832FA0D8));
//            bool loading = *SWA::SGlobals::ms_IsLoading;
//            bool compileNoMsaaPipeline = pipelineState.sampleCount != 1 && (loading || planarReflectionEnabled);
//
//            auto noMsaaPipeline = pipelineState;
//            noMsaaPipeline.sampleCount = 1;
//            noMsaaPipeline.enableAlphaToCoverage = false;
//
//            if ((noMsaaPipeline.specConstants & SPEC_CONSTANT_ALPHA_TO_COVERAGE) != 0)
//            {
//                noMsaaPipeline.specConstants &= ~SPEC_CONSTANT_ALPHA_TO_COVERAGE;
//                noMsaaPipeline.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
//            }
//
//            if (compileNoMsaaPipeline)
//            {
//                // Planar reflections don't use MSAA.
//                createGraphicsPipeline(noMsaaPipeline);
//            }
//
//            if (args.objectIcon)
//            {
//                // Object icons get rendered to a SDR buffer without MSAA.
//                auto iconPipelineState = noMsaaPipeline;
//                iconPipelineState.renderTargetFormat = RenderFormat::R8G8B8A8_UNORM;
//                createGraphicsPipeline(iconPipelineState);
//            }
//
//            if (isSonicMouth)
//            {
//                // Sonic's mouth switches between "SonicSkin_dspf[b]" or "SonicSkinNodeInvX_dspf[b]" depending on the view angle.
//                auto mouthPipelineState = pipelineState;
//                mouthPipelineState.vertexShader = FindShaderCacheEntry(0x689AA3140AB9EBAA)->guestShader;
//                createGraphicsPipeline(mouthPipelineState);
//
//                if (compileNoMsaaPipeline)
//                {
//                    auto noMsaaMouthPipelineState = noMsaaPipeline;
//                    noMsaaMouthPipelineState.vertexShader = mouthPipelineState.vertexShader;
//                    createGraphicsPipeline(noMsaaMouthPipelineState);
//                }
//            }
//        }
//    }
//}

//static void CompileMeshPipeline(Hedgehog::Mirage::CMeshData* mesh, MeshLayer layer, CompilationArgs& args)
//{
//    CompileMeshPipeline(Mesh
//        {
//            mesh->m_VertexSize,
//            0,
//            reinterpret_cast<GuestVertexDeclaration*>(mesh->m_VertexDeclarationPtr.m_pD3DVertexDeclaration.get()),
//            mesh->m_spMaterial.get(),
//            layer,
//            false
//        }, args);
//}

//static void CompileMeshPipeline(Hedgehog::Mirage::CMorphModelData* morphModel, Hedgehog::Mirage::CMeshIndexData* mesh, MeshLayer layer, CompilationArgs& args)
//{
//    CompileMeshPipeline(Mesh
//        {
//            morphModel->m_VertexSize,
//            morphModel->m_MorphTargetVertexSize,
//            reinterpret_cast<GuestVertexDeclaration*>(morphModel->m_VertexDeclarationPtr.m_pD3DVertexDeclaration.get()),
//            mesh->m_spMaterial.get(),
//            layer,
//            true
//        }, args);
//}

//template<typename T>
//static void CompileMeshPipelines(const T& modelData, CompilationArgs& args)
//{
//    for (auto& meshGroup : modelData.m_NodeGroupModels)
//    {
//        for (auto& mesh : meshGroup->m_OpaqueMeshes)
//        {
//            CompileMeshPipeline(mesh.get(), MeshLayer::Opaque, args);
//
//            if (args.noGI) // For models that can be shown transparent (eg. medals)
//                CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);
//        }
//
//        for (auto& mesh : meshGroup->m_TransparentMeshes)
//            CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);
//
//        for (auto& mesh : meshGroup->m_PunchThroughMeshes)
//            CompileMeshPipeline(mesh.get(), MeshLayer::PunchThrough, args);
//
//        for (auto& specialMeshGroup : meshGroup->m_SpecialMeshGroups)
//        {
//            for (auto& mesh : specialMeshGroup)
//                CompileMeshPipeline(mesh.get(), MeshLayer::Special, args); // TODO: Are there layer types other than water in this game??
//        }
//    }
//
//    for (auto& mesh : modelData.m_OpaqueMeshes)
//    {
//        CompileMeshPipeline(mesh.get(), MeshLayer::Opaque, args);
//
//        if (args.noGI)
//            CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);
//    }
//
//    for (auto& mesh : modelData.m_TransparentMeshes)
//        CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);
//
//    for (auto& mesh : modelData.m_PunchThroughMeshes)
//        CompileMeshPipeline(mesh.get(), MeshLayer::PunchThrough, args);
//
//    if constexpr (std::is_same_v<T, Hedgehog::Mirage::CModelData>)
//    {
//        for (auto& morphModel : modelData.m_MorphModels)
//        {
//            for (auto& mesh : morphModel->m_OpaqueMeshList)
//                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::Opaque, args);
//
//            for (auto& mesh : morphModel->m_TransparentMeshList)
//                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::Transparent, args);
//
//            for (auto& mesh : morphModel->m_PunchThroughMeshList)
//                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::PunchThrough, args);
//        }
//    }
//}

//static void CompileParticleMaterialPipeline(const Hedgehog::Sparkle::CParticleMaterial& material, PipelineTaskTokenPair& tokenPair)
//{
//    auto& shaderList = material.m_spShaderListData;
//    if (shaderList.get() == nullptr)
//        return;
//
//    guest_stack_var<Hedgehog::Base::CStringSymbol> defaultSymbol(reinterpret_cast<const char*>(g_memory.Translate(0x8202DDBC)));
//    auto defaultFindResult = shaderList->m_PixelShaderPermutations.find(*defaultSymbol);
//    if (defaultFindResult == shaderList->m_PixelShaderPermutations.end())
//        return;
//
//    guest_stack_var<Hedgehog::Base::CStringSymbol> noneSymbol(reinterpret_cast<const char*>(g_memory.Translate(0x8200D938)));
//    auto noneFindResult = defaultFindResult->second.m_VertexShaderPermutations.find(*noneSymbol);
//    if (noneFindResult == defaultFindResult->second.m_VertexShaderPermutations.end())
//        return;
//
//    // All the particle models in the game come with the unoptimized format, so we can assume it.
//    uint8_t unoptimizedVertexElements[144] =
//    {
//        0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x00, 0x00, 0x00,
//        0x00, 0x00, 0x00, 0x0C, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x03, 0x00, 0x00,
//        0x00, 0x00, 0x00, 0x18, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x06, 0x00, 0x00,
//        0x00, 0x00, 0x00, 0x24, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x07, 0x00, 0x00,
//        0x00, 0x00, 0x00, 0x30, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x00, 0x00,
//        0x00, 0x00, 0x00, 0x38, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x01, 0x00,
//        0x00, 0x00, 0x00, 0x40, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x02, 0x00,
//        0x00, 0x00, 0x00, 0x48, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x03, 0x00,
//        0x00, 0x00, 0x00, 0x50, 0x00, 0x1A, 0x23, 0xA6, 0x00, 0x0A, 0x00, 0x00,
//        0x00, 0x00, 0x00, 0x60, 0x00, 0x1A, 0x23, 0x86, 0x00, 0x02, 0x00, 0x00,
//        0x00, 0x00, 0x00, 0x64, 0x00, 0x1A, 0x20, 0x86, 0x00, 0x01, 0x00, 0x00,
//        0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
//    };
//
//    auto unoptimizedVertexDeclaration = CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(unoptimizedVertexElements));
//    auto sparkleVertexDeclaration = CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(g_memory.Translate(0x8211F540)));
//
//    bool isMeshShader = strstr(shaderList->m_TypeAndName.c_str(), "Mesh") != nullptr;
//
//    PipelineState pipelineState{};
//    pipelineState.vertexShader = reinterpret_cast<GuestShader*>(noneFindResult->second->m_VertexShaders.begin()->second->m_spCode->m_pD3DVertexShader.get());
//    pipelineState.pixelShader = reinterpret_cast<GuestShader*>(defaultFindResult->second.m_PixelShaders.begin()->second->m_spCode->m_pD3DPixelShader.get());
//    pipelineState.vertexDeclaration = isMeshShader ? unoptimizedVertexDeclaration : sparkleVertexDeclaration;
//    pipelineState.zWriteEnable = false;
//    pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL;
//    pipelineState.alphaBlendEnable = true;
//    pipelineState.srcBlendAlpha = RenderBlend::SRC_ALPHA;
//    pipelineState.destBlendAlpha = RenderBlend::INV_SRC_ALPHA;
//    pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
//    pipelineState.vertexStrides[0] = isMeshShader ? 104 : 28;
//    pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT_S8_UINT;
//    pipelineState.specConstants = SPEC_CONSTANT_REVERSE_Z;
//
//    if (pipelineState.vertexDeclaration->hasR11G11B10Normal)
//        pipelineState.specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;
//
//    switch (material.m_BlendMode.get())
//    {
//    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Zero:
//        pipelineState.srcBlend = RenderBlend::ZERO;
//        pipelineState.destBlend = RenderBlend::ZERO;
//        break;
//    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Typical:
//        pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
//        pipelineState.destBlend = RenderBlend::INV_SRC_ALPHA;
//        break;
//    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Add:
//        pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
//        pipelineState.destBlend = RenderBlend::ONE;
//        break;
//    default:
//        pipelineState.srcBlend = RenderBlend::ONE;
//        pipelineState.destBlend = RenderBlend::ONE;
//        break;
//    }
//
//    auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate)
//        {
//            SanitizePipelineState(pipelineStateToCreate);
//            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, tokenPair, shaderList->m_TypeAndName.c_str() + 3);
//        };
//
//    // Mesh particles can use both cull modes. Quad particles are only NONE.
//    RenderCullMode cullModes[] = { RenderCullMode::NONE, RenderCullMode::BACK };
//    uint32_t cullModeCount = isMeshShader ? std::size(cullModes) : 1;
//    RenderFormat renderTargetFormats[] = { RenderFormat::R16G16B16A16_FLOAT, RenderFormat::R8G8B8A8_UNORM };
//
//    for (size_t i = 0; i < cullModeCount; i++)
//    {
//        pipelineState.cullMode = cullModes[i];
//
//        for (auto renderTargetFormat : renderTargetFormats)
//        {
//            pipelineState.renderTargetFormat = renderTargetFormat;
//
//            if (renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT)
//                pipelineState.sampleCount = Config::AntiAliasing != EAntiAliasing::None ? int32_t(Config::AntiAliasing.Value) : 1;
//            else
//                pipelineState.sampleCount = 1;
//
//            createGraphicsPipeline(pipelineState);
//
//            // Always compile no MSAA variant for particles, as the planar
//            // reflection variable isn't reliable at this time of compilation.
//            bool compileNoMsaaPipeline = pipelineState.sampleCount != 1;
//
//            auto noMsaaPipelineState = pipelineState;
//            noMsaaPipelineState.sampleCount = 1;
//
//            if (compileNoMsaaPipeline)
//                createGraphicsPipeline(noMsaaPipelineState);
//
//            if (!isMeshShader)
//            {
//                // Previous compilation was for locus particles. This one will be for quads.
//                auto quadPipelineState = pipelineState;
//                quadPipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
//                createGraphicsPipeline(quadPipelineState);
//
//                if (compileNoMsaaPipeline)
//                {
//                    auto noMsaaQuadPipelineState = noMsaaPipelineState;
//                    noMsaaQuadPipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
//                    createGraphicsPipeline(noMsaaQuadPipelineState);
//                }
//            }
//        }
//    }
//}

static std::thread::id g_mainThreadId = std::this_thread::get_id();

// SWA::CGameModeStage::ExitLoading
// PPC_FUNC_IMPL(__imp__sub_825369A0);
// PPC_FUNC(sub_825369A0)
// {
//     assert(std::this_thread::get_id() == g_mainThreadId);
//
//     // Wait for pipeline compilations to finish.
//     uint32_t value;
//     while ((value = g_compilingPipelineTaskCount.load()) != 0)
//     {
//         // Pump SDL events to prevent the OS
//         // from thinking the process is unresponsive.
//         SDL_PumpEvents();
//         SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
//
//         g_compilingPipelineTaskCount.wait(value);
//     }
//
//     __imp__sub_825369A0(ctx, base);
// }

// CModelData::CheckMadeAll
// PPC_FUNC_IMPL(__imp__sub_82E2EFB0);
// PPC_FUNC(sub_82E2EFB0)
// {   
//     if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
//     {
//         ctx.r3.u64 = 0;
//     }
//     else
//     {
//         __imp__sub_82E2EFB0(ctx, base);
//     }
// }

// CTerrainModelData::CheckMadeAll
// PPC_FUNC_IMPL(__imp__sub_82E243D8);
// PPC_FUNC(sub_82E243D8)
// {   
//     if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
//     {
//         ctx.r3.u64 = 0;
//     }
//     else
//     {
//         __imp__sub_82E243D8(ctx, base);
//     }
// }

// CParticleMaterial::CheckMadeAll
// PPC_FUNC_IMPL(__imp__sub_82E87598);
// PPC_FUNC(sub_82E87598)
// {   
//     if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
//     {
//         ctx.r3.u64 = 0;
//     }
//     else
//     {
//         __imp__sub_82E87598(ctx, base);
//     }
// }

//void GetDatabaseDataMidAsmHook(PPCRegister& r1, PPCRegister& r4)
//{
//    auto& databaseData = *reinterpret_cast<boost::shared_ptr<Hedgehog::Database::CDatabaseData>*>(
//        g_memory.Translate(r1.u32 + 0x58));
//
//    if (!databaseData->IsMadeOne() && r4.u32 != NULL)
//    {
//        if (databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE)
//        {
//            // Ignore particle models, the materials they point at don't actually
//            // get used and give the threads unnecessary work.
//            bool isParticleModel = *reinterpret_cast<be<uint32_t>*>(g_memory.Translate(r4.u32 + 4)) != 5 &&
//                strncmp(databaseData->m_TypeAndName.c_str() + 2, "eff_", 4) == 0;
//
//            if (isParticleModel)
//                return;
//
//            // Adabat water is broken in original game, which they tried to fix by partially including the files in the update,
//            // which then finally fixed for real in the DLC. This confuses the async PSO compiler and causes a hang if the DLC is missing.
//            // We'll just ignore it.
//            bool isAdabatWater = strcmp(databaseData->m_TypeAndName.c_str() + 2, "evl_sea_obj_st_waterCircle") == 0;
//            if (isAdabatWater)
//                return;
//        }
//
//        databaseData->m_Flags |= eDatabaseDataFlags_CompilingPipelines;
//        EnqueuePipelineTask(PipelineTaskType::DatabaseData, databaseData);
//    }
//}

//static bool CheckMadeAll(Hedgehog::Mirage::CMeshData* meshData)
//{
//    if (!meshData->IsMadeOne())
//        return false;
//
//    if (meshData->m_spMaterial.get() != nullptr)
//    {
//        if (!meshData->m_spMaterial->IsMadeOne())
//            return false;
//
//        if (meshData->m_spMaterial->m_spTexsetData.get() != nullptr)
//        {
//            if (!meshData->m_spMaterial->m_spTexsetData->IsMadeOne())
//                return false;
//
//            for (auto& texture : meshData->m_spMaterial->m_spTexsetData->m_TextureList)
//            {
//                if (!texture->IsMadeOne())
//                    return false;
//            }
//        }
//    }
//
//    return true;
//}

template<typename T>
static bool CheckMadeAll(const T& modelData)
{
    if (!modelData.IsMadeOne())
        return false;

    for (auto& meshGroup : modelData.m_NodeGroupModels)
    {
        for (auto& mesh : meshGroup->m_OpaqueMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }     

        for (auto& mesh : meshGroup->m_TransparentMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }    

        for (auto& mesh : meshGroup->m_PunchThroughMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }

        for (auto& specialMeshGroup : meshGroup->m_SpecialMeshGroups)
        {
            for (auto& mesh : specialMeshGroup)
            {
                if (!CheckMadeAll(mesh.get()))
                    return false;
            }
        }
    }

    for (auto& mesh : modelData.m_OpaqueMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    for (auto& mesh : modelData.m_TransparentMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    for (auto& mesh : modelData.m_PunchThroughMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    return true;
}

//static void PipelineTaskConsumerThread()
//{
//#ifdef _WIN32
//    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
//    GuestThread::SetThreadName(GetCurrentThreadId(), "Pipeline Task Consumer Thread");
//#endif
//
//    std::vector<PipelineTask> localPipelineTaskQueue;
//    std::unique_ptr<GuestThreadContext> ctx;
//
//    while (true)
//    {
//        // Wait for tasks to arrive.
//        uint32_t pendingPipelineTaskCount;
//        while ((pendingPipelineTaskCount = g_pendingPipelineTaskCount.load()) == 0)
//            g_pendingPipelineTaskCount.wait(pendingPipelineTaskCount);
//
//        if (ctx == nullptr)
//            ctx = std::make_unique<GuestThreadContext>(0);
//
//        {
//            std::lock_guard lock(g_pipelineTaskMutex);
//            localPipelineTaskQueue.insert(localPipelineTaskQueue.end(), g_pipelineTaskQueue.begin(), g_pipelineTaskQueue.end());
//            g_pipelineTaskQueue.clear();
//        }
//
//        bool allHandled = true;
//
//        for (auto& [type, databaseData] : localPipelineTaskQueue)
//        {
//            switch (type)
//            {
//            case PipelineTaskType::DatabaseData:
//            {
//                bool ready = false;
//
//                if (databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE)
//                    ready = CheckMadeAll(*reinterpret_cast<Hedgehog::Mirage::CModelData*>(databaseData.get()));
//                else
//                    ready = databaseData->IsMadeOne();
//
//                if (ready || databaseData.unique())
//                {
//                    if (databaseData->m_pVftable.ptr == TERRAIN_MODEL_DATA_VFTABLE)
//                    {
//                        CompilationArgs args{};
//                        args.tokenPair.token.type = type;
//                        args.tokenPair.token.databaseData = databaseData;
//                        args.instancing = strncmp(databaseData->m_TypeAndName.c_str() + 3, "ins", 3) == 0;
//                        CompileMeshPipelines(*reinterpret_cast<Hedgehog::Mirage::CTerrainModelData*>(databaseData.get()), args);
//                    }
//                    else if (databaseData->m_pVftable.ptr == PARTICLE_MATERIAL_VFTABLE)
//                    {
//                        PipelineTaskTokenPair tokenPair;
//                        tokenPair.token.type = type;
//                        tokenPair.token.databaseData = databaseData;
//                        CompileParticleMaterialPipeline(*reinterpret_cast<Hedgehog::Sparkle::CParticleMaterial*>(databaseData.get()), tokenPair);
//                    }
//                    else
//                    {
//                        assert(databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE);
//
//                        auto modelData = reinterpret_cast<Hedgehog::Mirage::CModelData*>(databaseData.get());
//
//                        CompilationArgs args{};
//                        args.tokenPair.token.type = type;
//                        args.tokenPair.token.databaseData = databaseData;
//                        args.noGI = true;
//                        args.hasMoreThanOneBone = modelData->m_NodeNum > 1;
//                        args.velocityMapQuickStep = strcmp(databaseData->m_TypeAndName.c_str() + 2, "SonicRoot") == 0;
//
//                        // Check for the on screen items, eg. rings going to HUD.
//                        auto items = reinterpret_cast<xpointer<const char>*>(g_memory.Translate(0x832A8DD0));
//                        for (size_t i = 0; i < 50; i++)
//                        {
//                            if (strcmp(databaseData->m_TypeAndName.c_str() + 2, (*items).get()) == 0)
//                            {
//                                args.objectIcon = true;
//                                break;
//                            }
//                            items += 7;
//                        }
//
//                        CompileMeshPipelines(*modelData, args);
//                    }
//
//                    type = PipelineTaskType::Null;
//                    databaseData = nullptr;
//
//                    --g_pendingPipelineTaskCount;
//                }
//                else
//                {
//                    allHandled = false;
//                }
//
//                break;
//            }
//
//            case PipelineTaskType::PrecompilePipelines:
//            {
//                // Deliberately leaving the type null to account for the enqueue
//                // call not incrementing the compiling pipeline task counter.
//                PipelineTaskTokenPair tokenPair;
//
//                for (auto vertexElements : g_vertexDeclarationCache)
//                    CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(vertexElements));
//
//                for (auto pipelineState : g_pipelineStateCache)
//                {
//                    // The hashes were reinterpret casted to pointers in the cache.
//                    pipelineState.vertexShader = FindShaderCacheEntry(reinterpret_cast<XXH64_hash_t>(pipelineState.vertexShader))->guestShader;
//
//                    if (pipelineState.pixelShader != nullptr)
//                        pipelineState.pixelShader = FindShaderCacheEntry(reinterpret_cast<XXH64_hash_t>(pipelineState.pixelShader))->guestShader;
//
//                    {
//                        std::lock_guard lock(g_vertexDeclarationMutex);
//                        pipelineState.vertexDeclaration = g_vertexDeclarations[reinterpret_cast<XXH64_hash_t>(pipelineState.vertexDeclaration)];
//                    }
//
//                    if (!g_capabilities.triangleFan && pipelineState.primitiveTopology == RenderPrimitiveTopology::TRIANGLE_FAN)
//                        pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
//
//                    // Zero out depth bias for Vulkan, we only store common values for D3D12.
//                    if (g_capabilities.dynamicDepthBias && g_backend != Backend::D3D12)
//                    {
//                        pipelineState.depthBias = 0;
//                        pipelineState.slopeScaledDepthBias = 0.0f;
//                    }
//
//                    if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
//                        pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;
//
//                    auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate, const char* name)
//                        {
//                            SanitizePipelineState(pipelineStateToCreate);
//                            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, tokenPair, name, true);
//                        };
//
//                    // Compile both MSAA and non MSAA variants to work with reflection maps. The render formats are an assumption but it should hold true.
//                    if (Config::AntiAliasing != EAntiAliasing::None &&
//                        pipelineState.renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT &&
//                        pipelineState.depthStencilFormat == RenderFormat::D32_FLOAT_S8_UINT)
//                    {
//                        auto msaaPipelineState = pipelineState;
//                        msaaPipelineState.sampleCount = int32_t(Config::AntiAliasing.Value);
//
//                        if (Config::TransparencyAntiAliasing && (msaaPipelineState.specConstants & SPEC_CONSTANT_ALPHA_TEST) != 0)
//                        {
//                            msaaPipelineState.enableAlphaToCoverage = true;
//                            msaaPipelineState.specConstants &= ~SPEC_CONSTANT_ALPHA_TEST;
//                            msaaPipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
//                        }
//
//                        createGraphicsPipeline(msaaPipelineState, "Precompiled Pipeline MSAA");
//                    }
//
//                    if (pipelineState.pixelShader != nullptr &&
//                        pipelineState.pixelShader->shaderCacheEntry != nullptr)
//                    {
//                        XXH64_hash_t hash = pipelineState.pixelShader->shaderCacheEntry->hash;
//
//                        // Compile the custom gaussian blur shaders that we pass to the game.
//                        if (hash == 0x4294510C775F4EE8)
//                        {
//                            for (auto& shader : g_gaussianBlurShaders)
//                            {
//                                auto newPipelineState = pipelineState;
//                                newPipelineState.pixelShader = shader.get();
//                                createGraphicsPipeline(newPipelineState, "Precompiled Gaussian Blur Pipeline");
//                            }
//                        }
//                        // Compile enhanced motion blur shader.
//                        else if (hash == 0x6B9732B4CD7E7740)
//                        {
//                            auto newPipelineState = pipelineState;
//                            newPipelineState.pixelShader = g_enhancedMotionBlurShader.get();
//                            createGraphicsPipeline(newPipelineState, "Precompiled Enhanced Motion Blur Pipeline");
//                        }
//                    }
//
//                    createGraphicsPipeline(pipelineState, "Precompiled Pipeline");
//
//                    // Compile the CSD filter shader that we pass to the game when point filtering is used.
//                    if (pipelineState.pixelShader == g_csdShader)
//                    {
//                        pipelineState.pixelShader = g_csdFilterShader.get();
//                        createGraphicsPipeline(pipelineState, "Precompiled CSD Filter Pipeline");
//                    }
//                }
//
//                type = PipelineTaskType::Null;
//                --g_pendingPipelineTaskCount;
//
//                break;
//            }
//
//            case PipelineTaskType::RecompilePipelines:
//            {
//                PipelineTaskTokenPair tokenPair;
//                tokenPair.token.type = type;
//
//                auto asyncPipelines = g_asyncPipelineStates.values();
//
//                for (auto& [hash, pipelineState] : asyncPipelines)
//                {
//                    bool alphaTest = (pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) != 0;
//                    bool msaa = pipelineState.sampleCount != 1 || (pipelineState.renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT && pipelineState.depthStencilFormat == RenderFormat::D32_FLOAT_S8_UINT);
//
//                    pipelineState.sampleCount = 1;
//                    pipelineState.enableAlphaToCoverage = false;
//                    pipelineState.specConstants &= ~(SPEC_CONSTANT_BICUBIC_GI_FILTER | SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE);
//
//                    if (msaa && Config::AntiAliasing != EAntiAliasing::None)
//                    {
//                        pipelineState.sampleCount = int32_t(Config::AntiAliasing.Value);
//
//                        if (alphaTest)
//                        {
//                            if (Config::TransparencyAntiAliasing)
//                            {
//                                pipelineState.enableAlphaToCoverage = true;
//                                pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
//                            }
//                            else
//                            {
//                                pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
//                            }
//                        }
//                    }
//                    else if (alphaTest)
//                    {
//                        pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
//                    }
//
//                    if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
//                        pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;
//
//                    SanitizePipelineState(pipelineState);
//                    EnqueueGraphicsPipelineCompilation(pipelineState, tokenPair, "Recompiled Pipeline State");
//                }
//
//                type = PipelineTaskType::Null;
//                --g_pendingPipelineTaskCount;
//
//                break;
//            }
//            }
//        }
//
//        if (allHandled)
//            localPipelineTaskQueue.clear();
//
//        std::this_thread::yield();
//    }
//}

//static std::thread g_pipelineTaskConsumerThread(PipelineTaskConsumerThread);

#ifdef ASYNC_PSO_DEBUG

// PPC_FUNC_IMPL(__imp__sub_82E33330);
// PPC_FUNC(sub_82E33330)
// {
//     auto vertexShaderCode = reinterpret_cast<Hedgehog::Mirage::CVertexShaderCodeData*>(g_memory.Translate(ctx.r4.u32));
//     __imp__sub_82E33330(ctx, base);
//     reinterpret_cast<GuestShader*>(vertexShaderCode->m_pD3DVertexShader.get())->name = vertexShaderCode->m_TypeAndName.c_str() + 3;
// }

// PPC_FUNC_IMPL(__imp__sub_82E328D8);
// PPC_FUNC(sub_82E328D8)
// {
//     auto pixelShaderCode = reinterpret_cast<Hedgehog::Mirage::CPixelShaderCodeData*>(g_memory.Translate(ctx.r4.u32));
//     __imp__sub_82E328D8(ctx, base);
//     reinterpret_cast<GuestShader*>(pixelShaderCode->m_pD3DPixelShader.get())->name = pixelShaderCode->m_TypeAndName.c_str() + 2;
// }

#endif

#ifdef PSO_CACHING
class SDLEventListenerForPSOCaching : public SDLEventListener
{
public:
    bool OnSDLEvent(SDL_Event* event) override 
    {
        if (event->type != SDL_QUIT)
            return false;

        std::lock_guard lock(g_pipelineCacheMutex);
        if (g_pipelineStatesToCache.empty())
            return false;

        FILE* f = fopen("send_this_file_to_skyth.txt", "ab");
        if (f != nullptr)
        {
            ankerl::unordered_dense::set<GuestVertexDeclaration*> vertexDeclarations;
            xxHashMap<PipelineState> pipelineStatesToCache;

            for (auto& [hash, pipelineState] : g_pipelineStatesToCache)
            {
                if (pipelineState.vertexShader->shaderCacheEntry == nullptr ||
                    (pipelineState.pixelShader != nullptr && pipelineState.pixelShader->shaderCacheEntry == nullptr))
                {
                    continue;
                }

                vertexDeclarations.emplace(pipelineState.vertexDeclaration);

                // Mask out the config options.
                pipelineState.sampleCount = 1;
                pipelineState.enableAlphaToCoverage = false;

                if ((pipelineState.specConstants & SPEC_CONSTANT_ALPHA_TO_COVERAGE) != 0)
                {
                    pipelineState.specConstants &= ~SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                }

                pipelineStatesToCache.emplace(XXH3_64bits(&pipelineState, sizeof(pipelineState)), pipelineState);
            }

            for (auto vertexDeclaration : vertexDeclarations)
            {
                fmt::print(f, "static uint8_t g_vertexElements_{:016X}[] = {{", vertexDeclaration->hash);

                auto bytes = reinterpret_cast<uint8_t*>(vertexDeclaration->vertexElements.get());
                for (size_t i = 0; i < vertexDeclaration->vertexElementCount * sizeof(GuestVertexElement); i++)
                    fmt::print(f, "0x{:X},", bytes[i]);

                fmt::println(f, "}};");
            }

            for (auto& [pipelineHash, pipelineState] : pipelineStatesToCache)
            {
                fmt::println(f, "{{ "
                    "reinterpret_cast<GuestShader*>(0x{:X}),"
                    "reinterpret_cast<GuestShader*>(0x{:X}),"
                    "reinterpret_cast<GuestVertexDeclaration*>(0x{:X}),"
                    "{},"
                    "{},"
                    "{},"
                    "{},"
                    "RenderBlend::{},"
                    "RenderBlend::{},"
                    "RenderCullMode::{},"
                    "RenderFrontFace::{},"
                    "RenderComparisonFunction::{},"
                    "RenderComparisonFunction::{},"
                    "RenderStencilOp::{},"
                    "RenderStencilOp::{},"
                    "RenderStencilOp::{},"
                    "RenderComparisonFunction::{},"
                    "RenderStencilOp::{},"
                    "RenderStencilOp::{},"
                    "RenderStencilOp::{},"
                    "{},"
                    "{},"
                    "{},"
                    "{},"
                    "RenderBlendOperation::{},"
                    "{},"
                    "{},"
                    "RenderBlend::{},"
                    "RenderBlend::{},"
                    "RenderBlendOperation::{},"
                    "0x{:X},"
                    "RenderPrimitiveTopology::{},"
                    "{{ {},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{} }},"
                    "RenderFormat::{},"
                    "RenderFormat::{},"
                    "{},"
                    "{},"
                    "{},"
                    "0x{:X} }},",
                    pipelineState.vertexShader->shaderCacheEntry->hash,
                    pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->shaderCacheEntry->hash : 0,
                    pipelineState.vertexDeclaration->hash,
                    pipelineState.zEnable,
                    pipelineState.zWriteEnable,
                    pipelineState.stencilEnable,
                    pipelineState.stencilTwoSided,
                    magic_enum::enum_name(pipelineState.srcBlend),
                    magic_enum::enum_name(pipelineState.destBlend),
                    magic_enum::enum_name(pipelineState.cullMode),
                    magic_enum::enum_name(pipelineState.frontFace),
                    magic_enum::enum_name(pipelineState.zFunc),
                    magic_enum::enum_name(pipelineState.stencilFunc),
                    magic_enum::enum_name(pipelineState.stencilFail),
                    magic_enum::enum_name(pipelineState.stencilZFail),
                    magic_enum::enum_name(pipelineState.stencilPass),
                    magic_enum::enum_name(pipelineState.stencilFuncCCW),
                    magic_enum::enum_name(pipelineState.stencilFailCCW),
                    magic_enum::enum_name(pipelineState.stencilZFailCCW),
                    magic_enum::enum_name(pipelineState.stencilPassCCW),
                    pipelineState.stencilMask,
                    pipelineState.stencilWriteMask,
                    pipelineState.stencilRef,
                    pipelineState.alphaBlendEnable,
                    magic_enum::enum_name(pipelineState.blendOp),
                    pipelineState.slopeScaledDepthBias,
                    pipelineState.depthBias,
                    magic_enum::enum_name(pipelineState.srcBlendAlpha),
                    magic_enum::enum_name(pipelineState.destBlendAlpha),
                    magic_enum::enum_name(pipelineState.blendOpAlpha),
                    pipelineState.colorWriteEnable,
                    magic_enum::enum_name(pipelineState.primitiveTopology),
                    pipelineState.vertexStrides[0],
                    pipelineState.vertexStrides[1],
                    pipelineState.vertexStrides[2],
                    pipelineState.vertexStrides[3],
                    pipelineState.vertexStrides[4],
                    pipelineState.vertexStrides[5],
                    pipelineState.vertexStrides[6],
                    pipelineState.vertexStrides[7],
                    pipelineState.vertexStrides[8],
                    pipelineState.vertexStrides[9],
                    pipelineState.vertexStrides[10],
                    pipelineState.vertexStrides[11],
                    pipelineState.vertexStrides[12],
                    pipelineState.vertexStrides[13],
                    pipelineState.vertexStrides[14],
                    pipelineState.vertexStrides[15],
                    magic_enum::enum_name(pipelineState.renderTargetFormat),
                    magic_enum::enum_name(pipelineState.depthStencilFormat),
                    pipelineState.sampleCount,
                    pipelineState.enableAlphaToCoverage,
                    pipelineState.enableConditionalSurvey,
                    pipelineState.specConstants);
            }

            fclose(f);
        }

        return false;
    }
};
SDLEventListenerForPSOCaching g_sdlEventListenerForPSOCaching;
#endif

void VideoConfigValueChangedCallback(IConfigDef* config)
{
    // Config options that require internal resolution resize
    g_needsResize |=
        config == &Config::AspectRatio ||
        config == &Config::ResolutionScale ||
        config == &Config::AntiAliasing ||
        config == &Config::ShadowResolution;

    if (g_needsResize)
        Video::ComputeViewportDimensions();
        
    // Config options that require pipeline recompilation
    bool shouldRecompile =
        config == &Config::AntiAliasing ||
        config == &Config::TransparencyAntiAliasing;

//    if (shouldRecompile)
//        EnqueuePipelineTask(PipelineTaskType::RecompilePipelines, {});
}

// There is a bug on AMD where restart indices cause incorrect culling and prevent some triangles from being rendered.
// This seems to happen on both Windows AMD drivers and Mesa. Converting restart indices to degenerate triangles fixes it.
static void ConvertToDegenerateTriangles(uint16_t* indices, uint32_t indexCount, uint16_t*& newIndices, uint32_t& newIndexCount)
{
    newIndices = reinterpret_cast<uint16_t*>(g_userHeap.Alloc(indexCount * sizeof(uint16_t) * 3));
    newIndexCount = 0;

    bool stripStart = true;
    uint32_t stripSize = 0;
    uint16_t lastIndex = 0;

    for (uint32_t i = 0; i < indexCount; i++)
    {
        uint16_t index = indices[i];
        if (index == 0xFFFF)
        {
            if ((stripSize % 2) != 0)
                newIndices[newIndexCount++] = lastIndex;

            stripStart = true;
            stripSize = 0;
        }
        else 
        {
            if (stripStart && newIndexCount != 0)
            {
                newIndices[newIndexCount++] = lastIndex;
                newIndices[newIndexCount++] = index;
            }

            newIndices[newIndexCount++] = index;
            stripStart = false;
            ++stripSize;
            lastIndex = index;
        }
    }
}

struct MeshResource
{
    MARATHON_INSERT_PADDING(0x4);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

static std::vector<uint16_t*> g_newIndicesToFree;

// Hedgehog::Mirage::CMeshData::Make
// PPC_FUNC_IMPL(__imp__sub_82E44AF8);
// PPC_FUNC(sub_82E44AF8)
// {
//     uint16_t* newIndicesToFree = nullptr;

//     auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
//     if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
//     {
//         auto meshResource = reinterpret_cast<MeshResource*>(base + ctx.r4.u32);

//         if (meshResource->indexCount != 0)
//         {
//             uint16_t* newIndices;
//             uint32_t newIndexCount;

//             ConvertToDegenerateTriangles(
//                 reinterpret_cast<uint16_t*>(base + meshResource->indices),
//                 meshResource->indexCount,
//                 newIndices,
//                 newIndexCount);

//             meshResource->indexCount = newIndexCount;
//             meshResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);

//             if (PPC_LOAD_U32(0x83396E98) != NULL)
//             {
//                 // If index buffers are getting merged, new indices need to survive until the merge happens.
//                 g_newIndicesToFree.push_back(newIndices);
//             }
//             else 
//             {
//                 // Otherwise, we can free it immediately.
//                 newIndicesToFree = newIndices;
//             }
//         }
//     }

//     __imp__sub_82E44AF8(ctx, base);

//     if (newIndicesToFree != nullptr)
//         g_userHeap.Free(newIndicesToFree);
// }

// Hedgehog::Mirage::CShareVertexBuffer::Reset
// PPC_FUNC_IMPL(__imp__sub_82E250D0);
// PPC_FUNC(sub_82E250D0)
// {
//     __imp__sub_82E250D0(ctx, base);

//     for (auto newIndicesToFree : g_newIndicesToFree)
//         g_userHeap.Free(newIndicesToFree);

//     g_newIndicesToFree.clear();
// }

struct LightAndIndexBufferResourceV1
{
    MARATHON_INSERT_PADDING(0x4);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

// Hedgehog::Mirage::CLightAndIndexBufferData::MakeV1
// PPC_FUNC_IMPL(__imp__sub_82E3AFC8);
// PPC_FUNC(sub_82E3AFC8)
// {
//     uint16_t* newIndices = nullptr;

//     auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
//     if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
//     {
//         auto lightAndIndexBufferResource = reinterpret_cast<LightAndIndexBufferResourceV1*>(base + ctx.r4.u32);

//         if (lightAndIndexBufferResource->indexCount != 0)
//         {
//             uint32_t newIndexCount;

//             ConvertToDegenerateTriangles(
//                 reinterpret_cast<uint16_t*>(base + lightAndIndexBufferResource->indices),
//                 lightAndIndexBufferResource->indexCount,
//                 newIndices,
//                 newIndexCount);

//             lightAndIndexBufferResource->indexCount = newIndexCount;
//             lightAndIndexBufferResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);
//         }
//     }

//     __imp__sub_82E3AFC8(ctx, base);

//     if (newIndices != nullptr)
//         g_userHeap.Free(newIndices);
// }

struct LightAndIndexBufferResourceV5
{
    MARATHON_INSERT_PADDING(0x8);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

// Hedgehog::Mirage::CLightAndIndexBufferData::MakeV5
// PPC_FUNC_IMPL(__imp__sub_82E3B1C0);
// PPC_FUNC(sub_82E3B1C0)
// {
//     uint16_t* newIndices = nullptr;

//     auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
//     if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
//     {
//         auto lightAndIndexBufferResource = reinterpret_cast<LightAndIndexBufferResourceV5*>(base + ctx.r4.u32);

//         if (lightAndIndexBufferResource->indexCount != 0)
//         {
//             uint32_t newIndexCount;

//             ConvertToDegenerateTriangles(
//                 reinterpret_cast<uint16_t*>(base + lightAndIndexBufferResource->indices),
//                 lightAndIndexBufferResource->indexCount,
//                 newIndices,
//                 newIndexCount);

//             lightAndIndexBufferResource->indexCount = newIndexCount;
//             lightAndIndexBufferResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);
//         }
//     }

//     __imp__sub_82E3B1C0(ctx, base);

//     if (newIndices != nullptr)
//         g_userHeap.Free(newIndices);
// }

#if defined(__SWITCH__)
// [Switch] Hand-over of the device between threads. Marathon's D3DDevice_AcquireThreadOwnership (sub_8253EB38) stores
// the calling thread's id at device+10760 and ReleaseThreadOwnership (sub_8253EB78) clears it; the port stubs both
// and still leaves that field alone. Their callers: the main thread's loading loop (sub_82743498: release, resume
// the loading threads, ..., acquire), the loading thread's frame loop (sub_82744840: acquire, draw and present the
// loading screen, release) and a message handler (sub_825B2400). The hooks move the D3D thread along:
// - release, on the D3D thread: its batch is flushed and it stops being the D3D thread, so the next one takes over
//   with all of its commands in the queue;
// - acquire: the thread becomes the D3D thread when no thread is. When one still is (it never released), nothing
//   changes and this thread's commands keep going directly to the queue, as before.
// Every change makes the state filter forget everything, and is a full barrier in the queue: each side waits until
// the render thread has processed a fence sent behind all of its commands (on release through the producer its
// batches went through, on acquire through its own). moodycamel orders the items of one producer only, so without
// it the new D3D thread's first commands could be processed before the previous one's last (a state the filter then
// believes the render thread has, or a draw before the other thread's BeginCommandList), and with
// SwitchRenderQueueToken a thread's batches (the token) could overtake its direct commands (its own producer) or be
// overtaken by them. With it, each thread's commands stay in the order it sent them, and all of the old D3D thread's
// are processed before any of the new one's. It costs a wait for the render thread to catch up at each change.
// Diagnostics on stderr: the changes, refused acquires, releases by other threads, and the commands and presents of
// threads that did not own the device while batching was on.
static uint32_t g_d3dThreadAcquires = 0;
static uint32_t g_d3dThreadReleases = 0;
static uint32_t g_d3dThreadRefusedAcquires = 0;
static uint32_t g_d3dThreadOtherReleases = 0;
static uint32_t g_d3dThreadLogLines = 0;

static const char* RenderCommandTypeName(uint32_t type)
{
    switch (RenderCommandType(type))
    {
    case RenderCommandType::SetRenderState: return "SetRenderState";
    case RenderCommandType::DestructResource: return "DestructResource";
    case RenderCommandType::UnlockTextureRect: return "UnlockTextureRect";
    case RenderCommandType::DrawImGui: return "DrawImGui";
    case RenderCommandType::ExecuteCommandList: return "ExecuteCommandList";
    case RenderCommandType::BeginCommandList: return "BeginCommandList";
    case RenderCommandType::StretchRect: return "StretchRect";
    case RenderCommandType::SetRenderTarget: return "SetRenderTarget";
    case RenderCommandType::SetDepthStencilSurface: return "SetDepthStencilSurface";
    case RenderCommandType::ExecutePendingStretchRectCommands: return "ExecutePendingStretchRectCommands";
    case RenderCommandType::Clear: return "Clear";
    case RenderCommandType::SetViewport: return "SetViewport";
    case RenderCommandType::SetTexture: return "SetTexture";
    case RenderCommandType::SetScissorRect: return "SetScissorRect";
    case RenderCommandType::SetSamplerState: return "SetSamplerState";
    case RenderCommandType::SetBooleans: return "SetBooleans";
    case RenderCommandType::SetVertexShaderConstants: return "SetVertexShaderConstants";
    case RenderCommandType::SetPixelShaderConstants: return "SetPixelShaderConstants";
    case RenderCommandType::DrawPrimitive: return "DrawPrimitive";
    case RenderCommandType::DrawIndexedPrimitive: return "DrawIndexedPrimitive";
    case RenderCommandType::DrawPrimitiveUP: return "DrawPrimitiveUP";
    case RenderCommandType::SetVertexDeclaration: return "SetVertexDeclaration";
    case RenderCommandType::SetVertexShader: return "SetVertexShader";
    case RenderCommandType::SetStreamSource: return "SetStreamSource";
    case RenderCommandType::SetIndices: return "SetIndices";
    case RenderCommandType::SetPixelShader: return "SetPixelShader";
    case RenderCommandType::SetConditionalSurvey: return "SetConditionalSurvey";
    case RenderCommandType::SetConditionalRendering: return "SetConditionalRendering";
    case RenderCommandType::SetClipPlane: return "SetClipPlane";
    default: return "other";
    }
}

// One line per event, the first 64 of them and then every 1024th (in case the game hands the device over every frame).
static void LogD3DThreadEvent(const char* event)
{
    const uint32_t line = g_d3dThreadLogLines++;
    if (line >= 64 && (line % 1024) != 0)
        return;

    char text[1024];
    int length = snprintf(text, sizeof(text), "D3D thread: %s (thread %lx; %u acquired, %u released, %u acquires refused, "
        "%u releases by other threads; presents by other threads %u; commands by other threads:",
        event, (unsigned long)(CurrentThreadTlsRegion() & 0xFFFFFFFF), g_d3dThreadAcquires, g_d3dThreadReleases,
        g_d3dThreadRefusedAcquires, g_d3dThreadOtherReleases, g_directPresents.load(std::memory_order_relaxed));

    for (uint32_t i = 0; i < std::size(g_directRenderCommands) && length > 0 && size_t(length) < sizeof(text) - 64; i++)
    {
        const uint32_t count = g_directRenderCommands[i].load(std::memory_order_relaxed);
        if (count != 0)
            length += snprintf(text + length, sizeof(text) - length, " %s %u", RenderCommandTypeName(i), count);
    }

    fprintf(stderr, "%s).\n", text);
}

// Whether a change of D3D thread has to be a barrier in the queue (see above): whenever the D3D thread batches or
// filters its states.
static bool D3DThreadChangeNeedsFence()
{
    return g_switchRenderer.batchRenderCommands || g_switchRenderer.skipRedundantRenderStates ||
        g_switchRenderer.skipRedundantSamplerStates;
}

// D3DDevice_AcquireThreadOwnership
PPC_FUNC(sub_8253EB38)
{
    std::lock_guard lock(g_d3dThreadMutex);

    const uintptr_t region = CurrentThreadTlsRegion();
    const uintptr_t owner = g_presentThreadTlsRegion.load(std::memory_order_acquire);
    if (owner == region)
        return;

    if (owner != 0)
    {
        // Another thread still owns the device: this thread's commands keep going directly to the queue.
        g_d3dThreadRefusedAcquires++;
        LogD3DThreadEvent("acquire refused, another thread still owns the device");
        return;
    }

    // Everything this thread sent before (directly, through its own producer) is processed before its batches.
    if (D3DThreadChangeNeedsFence())
        SyncWithRenderThread(false);

    g_presentThreadId = std::this_thread::get_id();
    g_presentThreadTlsRegion.store(region, std::memory_order_release);
    g_stateFilterEpoch.fetch_add(1, std::memory_order_acq_rel);
    g_d3dThreadAcquires++;
    LogD3DThreadEvent("acquired");
}

// D3DDevice_ReleaseThreadOwnership
PPC_FUNC(sub_8253EB78)
{
    std::lock_guard lock(g_d3dThreadMutex);

    if (!IsPresentThread())
    {
        g_d3dThreadOtherReleases++;
        LogD3DThreadEvent("release by a thread that does not own the device, ignored");
        return;
    }

    FlushDeferredRenderCommands();

    // Everything this thread sent is processed before the next D3D thread's commands and this thread's direct ones.
    if (D3DThreadChangeNeedsFence())
        SyncWithRenderThread(g_switchRenderer.renderQueueToken);

    g_presentThreadTlsRegion.store(0, std::memory_order_release);
    g_stateFilterEpoch.fetch_add(1, std::memory_order_acq_rel);
    g_d3dThreadReleases++;
    LogD3DThreadEvent("released");
}

// The game's own Present (the installer calls Video::Present directly): SwitchPresentOnRenderThread starts with it.
static void GuestPresent()
{
    g_gamePresenting.store(true, std::memory_order_release);
    Video::Present();
}

// For the stall watchdog's dumps (os::switch_stall_watch). Atomics and plain counters only: it must not block.
static void ReportRendererState(std::string& out)
{
    char text[512];
    const int length = snprintf(text, sizeof(text), "render queue ~%zu commands (%llu taken so far), pipeline queue ~%zu, "
        "D3D thread %lx, present tails %u sent / %u done, batch buffers %u free, command list %s",
        g_renderQueue.size_approx(), (unsigned long long)g_renderCommandsTaken.load(std::memory_order_relaxed),
        g_pipelineStateQueue.size_approx(),
        (unsigned long)(g_presentThreadTlsRegion.load(std::memory_order_relaxed) & 0xFFFFFFFF),
        g_presentTailsSent.load(std::memory_order_relaxed), g_presentTailsDone.load(std::memory_order_relaxed),
        g_renderCommandBatchPool.tail.load(std::memory_order_relaxed) - g_renderCommandBatchPool.head.load(std::memory_order_relaxed),
        g_executedCommandList.load(std::memory_order_relaxed) ? "executed" : "not executed yet");

    if (length > 0)
        out.append(text, std::min<size_t>(size_t(length), sizeof(text) - 1));
}
#endif

GUEST_FUNCTION_HOOK(sub_8253EC98, CreateDevice);

GUEST_FUNCTION_HOOK(sub_8253AE98, DestructResource);

GUEST_FUNCTION_HOOK(sub_8253A740, LockTextureRect);
GUEST_FUNCTION_HOOK(sub_82538D30, UnlockTextureRect);

GUEST_FUNCTION_HOOK(sub_8253B5D0, LockVertexBuffer);
GUEST_FUNCTION_HOOK(sub_8253B630, UnlockVertexBuffer);
// GUEST_FUNCTION_HOOK(sub_82BE61D0, GetVertexBufferDesc);

GUEST_FUNCTION_HOOK(sub_8253B6F0, LockIndexBuffer);
GUEST_FUNCTION_HOOK(sub_8253B750, UnlockIndexBuffer);
// GUEST_FUNCTION_HOOK(sub_82BE6200, GetIndexBufferDesc);

GUEST_FUNCTION_HOOK(sub_8253AB20, GetSurfaceDesc);

GUEST_FUNCTION_HOOK(sub_825471F8, GetVertexDeclaration);
// GUEST_FUNCTION_HOOK(sub_82BE0530, HashVertexDeclaration);

#if defined(__SWITCH__)
GUEST_FUNCTION_HOOK(sub_825586B0, GuestPresent);
#else
GUEST_FUNCTION_HOOK(sub_825586B0, Video::Present);
#endif
GUEST_FUNCTION_HOOK(sub_82543B58, GetBackBuffer);
GUEST_FUNCTION_HOOK(sub_82543BA0, GetDepthStencil);

GUEST_FUNCTION_HOOK(sub_8253A8D8, CreateTexture);
GUEST_FUNCTION_HOOK(sub_8253B508, CreateVertexBuffer);
GUEST_FUNCTION_HOOK(sub_8253B640, CreateIndexBuffer);
GUEST_FUNCTION_HOOK(sub_8253A9F8, CreateSurface);

GUEST_FUNCTION_HOOK(sub_825575B8, StretchRect);

GUEST_FUNCTION_HOOK(sub_82543EE0, SetRenderTarget);
GUEST_FUNCTION_HOOK(sub_825444F0, SetRenderTarget);
GUEST_FUNCTION_HOOK(sub_82544210, SetDepthStencilSurface);

GUEST_FUNCTION_HOOK(sub_82555B30, Clear);

GUEST_FUNCTION_HOOK(sub_825436F0, SetViewport);

GUEST_FUNCTION_HOOK(sub_8253AC40, SetTexture);
GUEST_FUNCTION_HOOK(sub_82543628, SetScissorRect);

GUEST_FUNCTION_HOOK(sub_826FEC28, DrawPrimitive);
GUEST_FUNCTION_HOOK(sub_826FF030, DrawIndexedPrimitive);
GUEST_FUNCTION_HOOK(sub_826FE5C0, DrawPrimitiveUP);

GUEST_FUNCTION_HOOK(sub_82547118, CreateVertexDeclaration);
GUEST_FUNCTION_HOOK(sub_825470F8, SetVertexDeclaration);

GUEST_FUNCTION_HOOK(sub_82548700, CreateVertexShader);
GUEST_FUNCTION_HOOK(sub_82546EE0, SetVertexShader);

GUEST_FUNCTION_HOOK(sub_82543918, SetStreamSource);
GUEST_FUNCTION_HOOK(sub_82543AC8, SetIndices);

GUEST_FUNCTION_HOOK(sub_82548608, CreatePixelShader);
GUEST_FUNCTION_HOOK(sub_82546BD8, SetPixelShader);

GUEST_FUNCTION_HOOK(sub_82636BF8, BeginConditionalSurvey);
GUEST_FUNCTION_HOOK(sub_82636C08, EndConditionalSurvey);
GUEST_FUNCTION_HOOK(sub_82636C10, BeginConditionalRendering);
GUEST_FUNCTION_HOOK(sub_82636C18, EndConditionalRendering);

GUEST_FUNCTION_HOOK(sub_8253B760, IsSet);

GUEST_FUNCTION_HOOK(sub_82543CF0, SetClipPlane);

GUEST_FUNCTION_HOOK(sub_82541A78, SetRenderState<D3DRS_ZENABLE>);
GUEST_FUNCTION_HOOK(sub_82541AC0, SetRenderState<D3DRS_ZWRITEENABLE>);
GUEST_FUNCTION_HOOK(sub_82541460, SetRenderState<D3DRS_ALPHATESTENABLE>);
GUEST_FUNCTION_HOOK(sub_825415C0, SetRenderState<D3DRS_SRCBLEND>);
GUEST_FUNCTION_HOOK(sub_82541650, SetRenderState<D3DRS_DESTBLEND>);
GUEST_FUNCTION_HOOK(sub_82541400, SetRenderState<D3DRS_CULLMODE>);
GUEST_FUNCTION_HOOK(sub_82541AF0, SetRenderState<D3DRS_ZFUNC>);
GUEST_FUNCTION_HOOK(sub_825418C8, SetRenderState<D3DRS_ALPHAREF>);
GUEST_FUNCTION_HOOK(sub_825414A0, SetRenderState<D3DRS_ALPHABLENDENABLE>);
GUEST_FUNCTION_HOOK(sub_82541530, SetRenderState<D3DRS_BLENDOP>);
GUEST_FUNCTION_HOOK(sub_82543ED0, SetRenderState<D3DRS_SCISSORTESTENABLE>);
GUEST_FUNCTION_HOOK(sub_82541E90, SetRenderState<D3DRS_SLOPESCALEDEPTHBIAS>);
GUEST_FUNCTION_HOOK(sub_82541F58, SetRenderState<D3DRS_DEPTHBIAS>);
GUEST_FUNCTION_HOOK(sub_82541750, SetRenderState<D3DRS_SRCBLENDALPHA>);
GUEST_FUNCTION_HOOK(sub_825417C0, SetRenderState<D3DRS_DESTBLENDALPHA>);
GUEST_FUNCTION_HOOK(sub_825416E0, SetRenderState<D3DRS_BLENDOPALPHA>);
GUEST_FUNCTION_HOOK(sub_82542050, SetRenderState<D3DRS_COLORWRITEENABLE>);
GUEST_FUNCTION_HOOK(sub_82541B30, SetRenderState<D3DRS_STENCILENABLE>);
GUEST_FUNCTION_HOOK(sub_82541B78, SetRenderState<D3DRS_TWOSIDEDSTENCILMODE>);
GUEST_FUNCTION_HOOK(sub_82541BE8, SetRenderState<D3DRS_STENCILFAIL>);
GUEST_FUNCTION_HOOK(sub_82541C28, SetRenderState<D3DRS_STENCILZFAIL>);
GUEST_FUNCTION_HOOK(sub_82541C68, SetRenderState<D3DRS_STENCILPASS>);
GUEST_FUNCTION_HOOK(sub_82541BB8, SetRenderState<D3DRS_STENCILFUNC>);
GUEST_FUNCTION_HOOK(sub_82541D78, SetRenderState<D3DRS_STENCILREF>);
GUEST_FUNCTION_HOOK(sub_82541D98, SetRenderState<D3DRS_STENCILMASK>);
GUEST_FUNCTION_HOOK(sub_82541DB8, SetRenderState<D3DRS_STENCILWRITEMASK>);
GUEST_FUNCTION_HOOK(sub_82541CC8, SetRenderState<D3DRS_CCW_STENCILFAIL>);
GUEST_FUNCTION_HOOK(sub_82541D08, SetRenderState<D3DRS_CCW_STENCILZFAIL>);
GUEST_FUNCTION_HOOK(sub_82541D48, SetRenderState<D3DRS_CCW_STENCILPASS>);
GUEST_FUNCTION_HOOK(sub_82541C98, SetRenderState<D3DRS_CCW_STENCILFUNC>);
GUEST_FUNCTION_HOOK(sub_82541E38, SetRenderState<D3DRS_CLIPPLANEENABLE>);

int GetType(GuestResource* resource)
{
    if (resource->type == ResourceType::Texture) return 3;
    if (resource->type == ResourceType::VolumeTexture) return 17;
    if (resource->type == ResourceType::ArrayTexture) return 19;

    LOGF_WARNING("unknown resource type {:d}!", (int32_t)resource->type);
    __builtin_trap();
    return 0;
}

GUEST_FUNCTION_HOOK(sub_8253AE08, GetType);

// Game asks about the size of surface to check if it needs to be tiled.
// Because EDRAM has only 10MB, if size is more than 1024, then it enables tiling.
// We return 0 to always disable tiling.
int SurfaceSize(uint32_t width, uint32_t height, uint32_t format, uint32_t multisampleLevel)
{
    return 0;
}

GUEST_FUNCTION_HOOK(sub_82538D60, SurfaceSize);
GUEST_FUNCTION_HOOK(sub_82656B68, MakePictureData);
GUEST_FUNCTION_HOOK(sub_82656DB8, MakePictureData);

// GUEST_FUNCTION_HOOK(sub_82E9EE38, SetResolution);

GUEST_FUNCTION_HOOK(sub_82736178, ScreenShaderInit);

#if !defined(__SWITCH__)
GUEST_FUNCTION_STUB(sub_8253EB38); // D3DDevice_AcquireThreadOwnership ([Switch] hooked above)
GUEST_FUNCTION_STUB(sub_8253EB78); // D3DDevice_ReleaseThreadOwnership ([Switch] hooked above)
#endif
GUEST_FUNCTION_STUB(sub_82543BE0); // SetGammaRamp
GUEST_FUNCTION_STUB(sub_82543C68); // SetGammaRamp
GUEST_FUNCTION_STUB(sub_82547278); // Set shader allocation
GUEST_FUNCTION_STUB(sub_8272FAD0);
GUEST_FUNCTION_STUB(sub_82558E00);
GUEST_FUNCTION_STUB(sub_82559928);
GUEST_FUNCTION_STUB(sub_82559C18);
GUEST_FUNCTION_STUB(sub_82700C18); // D3DXFilterTexture
GUEST_FUNCTION_STUB(sub_8253EAE0);
GUEST_FUNCTION_STUB(sub_8254D598); // BeginConditional
GUEST_FUNCTION_STUB(sub_8254D7B0); // BeginConditional
GUEST_FUNCTION_STUB(sub_8254D9D0); // BeginConditional
GUEST_FUNCTION_STUB(sub_8254DB90); // BeginConditional
GUEST_FUNCTION_STUB(sub_8254DD40); // SetScreenExtentQueryMode

struct Rect
{
    be<uint32_t> x1;
    be<uint32_t> y1;
    be<uint32_t> x2;
    be<uint32_t> y2;
};

struct RESOLVE_PARAMS
{
    be<uint32_t> format;
    be<uint32_t> unk;
    be<uint32_t> format2;
};

int D3DDevice_BeginTiling(GuestDevice* device, uint32_t flags, uint32_t count, Rect* pTileRects, be<float>* pClearColor, float clearZ, uint32_t clearStencil)
{
    Clear(device, 0x3F, 0, pClearColor, clearZ, clearStencil);

    return 0;
}

GUEST_FUNCTION_HOOK(sub_82558F88, D3DDevice_BeginTiling);

int D3DDevice_EndTiling(GuestDevice* device, uint32_t flags, Rect* pResolveRects, GuestTexture* pDestTexture, be<float>* pClearColor, float clearZ, uint32_t clearStencil, RESOLVE_PARAMS* resolveParams)
{
    if (pDestTexture)
    {
        StretchRect(device, flags, 0, pDestTexture, 0, 0, 0);
    }

    return 0;
}

GUEST_FUNCTION_HOOK(sub_82559480, D3DDevice_EndTiling);

int D3DDevice_BeginShaderConstantF4(GuestDevice* device, uint32_t isPixelShader, uint32_t startRegister, be<uint32_t>* cachedConstantData, be<uint32_t>* writeCombinedConstantData, uint32_t vectorCount)
{
    uint32_t* constants;
    be<uint64_t>* dirtyFlags;

    if (isPixelShader)
    {
        constants = &device->pixelShaderFloatConstants[startRegister * 4];
        dirtyFlags = &device->dirtyFlags[1];
    }
    else
    {
        constants = &device->vertexShaderFloatConstants[startRegister * 4];
        dirtyFlags = &device->dirtyFlags[0];
    }

    const uint32_t addr = g_memory.MapVirtual(constants);
    *cachedConstantData = addr;
    *writeCombinedConstantData = addr;

    const uint32_t startBit = startRegister >> 2;
    const uint32_t endBit = (startRegister + vectorCount - 1) >> 2;
    const uint64_t dirtyFlag = ~0ull << startBit >> startBit >> (63 - endBit) << (63 - endBit);
    *dirtyFlags = dirtyFlags->get() | dirtyFlag;

    return 0;
}

GUEST_FUNCTION_HOOK(sub_825466E8, D3DDevice_BeginShaderConstantF4);
