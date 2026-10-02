#if defined(__SWITCH__)

#include <gpu/video.h>
#include <os/logger.h>
#include <os/switch_perf_init.h>
#include <user/config.h>
#include <user/paths.h>

#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if __has_include(<switch_build_id.h>)
#include <switch_build_id.h>
#endif
#ifndef MARATHON_RECOMP_SWITCH_DRIVER_ID
#define MARATHON_RECOMP_SWITCH_DRIVER_ID ""
#endif
#ifndef MARATHON_RECOMP_SWITCH_SHADER_ID
#define MARATHON_RECOMP_SWITCH_SHADER_ID ""
#endif
#ifndef MARATHON_RECOMP_SWITCH_BUILD_ID
#define MARATHON_RECOMP_SWITCH_BUILD_ID ""
#endif

extern const char* g_versionString; // version.cpp

// [Switch] Renderer area (docs/SWITCH-PERFORMANCE.md). The options are read here once, right after the
// configuration is loaded; gpu/video.cpp reads g_switchRenderer on its hot paths.
SwitchRendererOptions g_switchRenderer;
std::atomic<bool> g_switchRendererConfigured{ false };

void SwitchRendererSetThreadCore(int32_t core)
{
    uint64_t processCores = 0;
    if (R_FAILED(svcGetInfo(&processCores, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)) || processCores == 0)
        processCores = 0x7;

    // The affinity mask stays every core of the process: only the core the scheduler prefers changes.
    if ((processCores & (uint64_t(1) << core)) != 0)
        svcSetThreadCoreMask(threadGetCurHandle(), core, uint32_t(processCores));
}

// Driver switches, set before the Vulkan instance is created (Video::CreateHostDevice runs after this). The
// per-key ones first, so that SwitchMesaEnvironment overrides them; plume's frame generation setup appends its
// own NVK_DEBUG option afterwards, so it keeps working with a user-set NVK_DEBUG.
static void ApplyDriverEnvironment()
{
    // NVK_LINK_VARYINGS defaults to on in the driver, so the default changes nothing.
    setenv("NVK_LINK_VARYINGS", Config::SwitchLinkVaryings ? "1" : "0", 1);

    // SwitchZcullGreater: the main pass is reverse Z (see config_renderer.inl); the driver's default is "less".
    if (Config::SwitchZcullGreater)
        setenv("NVK_ZCULL", "greater", 1);

    // SwitchOperandReuse: NAK's Maxwell operand reuse (opt-in in the driver).
    if (Config::SwitchOperandReuse)
        setenv("NAK_DEBUG", "reuse", 1);

    // SwitchMesaEnvironment = "NAME=value;NAME=value".
    const std::string environment = Config::SwitchMesaEnvironment;
    size_t start = 0;
    while (start < environment.size())
    {
        size_t end = environment.find(';', start);
        if (end == std::string::npos)
            end = environment.size();

        const std::string entry = environment.substr(start, end - start);
        const size_t equals = entry.find('=');
        if (equals != std::string::npos && equals > 0)
        {
            const std::string name = entry.substr(0, equals);
            const std::string value = entry.substr(equals + 1);
            setenv(name.c_str(), value.c_str(), 1);
            fprintf(stderr, "Mesa environment: %s=%s\n", name.c_str(), value.c_str());
        }

        start = end + 1;
    }
}

// SwitchPipelineCache: plume's persistent VkPipelineCache (plume-switch-perf.patch), set up before the device exists.
// The file's key also covers the driver's IDs and pipeline cache UUID, but on Horizon those need not change between
// builds of the driver, which is linked into the NRO: the tag is the SHA-256 of the driver library the build linked
// (switch_build_id.h), so the cache survives rebuilds of everything else. Entries of changed shaders or states are
// simply not found (the driver keys its entries by everything a pipeline is made of and checks its own header).
// Mesa's own disk cache stays off (main.cpp): it only duplicated this one.
static void ConfigurePipelineCache()
{
    std::error_code ec;
    const auto cacheDirectory = GetUserPath() / "cache";
    std::filesystem::create_directories(cacheDirectory, ec);

    std::string buildTag;
    if (MARATHON_RECOMP_SWITCH_DRIVER_ID[0] != '\0')
    {
        buildTag = std::string("driver|") + MARATHON_RECOMP_SWITCH_DRIVER_ID;
        // Pipelines of shaders an older translator produced can never be found again: a new shader cache starts
        // a new pipeline cache instead of carrying them.
        if (MARATHON_RECOMP_SWITCH_SHADER_ID[0] != '\0')
            buildTag += std::string("|shaders|") + MARATHON_RECOMP_SWITCH_SHADER_ID;
    }
    else
    {
        // No driver hash (a build configured without it): the build itself.
        buildTag = std::string("build|") + g_versionString + "|" + MARATHON_RECOMP_SWITCH_BUILD_ID + "|" + __DATE__ " " __TIME__;
    }

    plume::ConfigureVulkanPipelineCache((cacheDirectory / "pipelines.bin").string(), buildTag);
}

void SwitchPerfInitRenderer()
{
    auto& options = g_switchRenderer;
    options.sparseConstantCopies = Config::SwitchSparseConstantCopies;
    options.compactTextureHeap = Config::SwitchCompactTextureHeap;
    options.singleCopyTriangle = Config::SwitchSingleCopyTriangle;
    options.hostThreadCores = Config::SwitchHostThreadCores;
    options.pipelineLookupCache = Config::SwitchPipelineLookupCache;
    options.samplerCache = Config::SwitchSamplerCache;
    options.frameLimiterSleep = Config::SwitchFrameLimiterSleep;
    options.surveyPlainStore = Config::SwitchSurveyPlainStore;
    options.gammaPushConstants = Config::SwitchGammaPushConstants;
    options.queryResetPerQuery = Config::SwitchQueryResetPerQuery;

    // Stage 2 (command hand-over). The batch options only apply to batches, and the idle-thread batches need the
    // larger ones (their capacity).
    options.batchRenderCommands = Config::SwitchBatchRenderCommands;
    options.batchSeveralDraws = options.batchRenderCommands && Config::SwitchBatchSeveralDraws;
    options.largerCommandBatches = options.batchRenderCommands && Config::SwitchLargerCommandBatches;
    options.renderQueueToken = options.batchRenderCommands && Config::SwitchRenderQueueToken;
    options.zeroCopyBatches = options.batchRenderCommands && Config::SwitchZeroCopyBatches;
    options.idleRenderThreadBatches = options.largerCommandBatches && Config::SwitchIdleRenderThreadBatches;
    options.skipRedundantRenderStates = Config::SwitchSkipRedundantRenderStates;
    options.skipRedundantSamplerStates = Config::SwitchSkipRedundantSamplerStates;
    options.presentOnRenderThread = Config::SwitchPresentOnRenderThread;
    options.presentWithoutRecordWait = options.presentOnRenderThread && Config::SwitchPresentWithoutRecordWait;
    options.deferredBufferUnlocks = Config::SwitchDeferredBufferUnlocks;

    // Stage 3 (pipelines and shader constants). Skipping pixel constants and keeping the vertex constants over a
    // copy rest on the fragment stage analysis and on the lazy pushes of the uniform buffer path.
    options.pipelineCache = Config::SwitchPipelineCache;
    options.pipelineCacheSaveDuringPlay = options.pipelineCache && Config::SwitchPipelineCacheSaveDuringPlay;
    options.depthOnlyWithoutPixelShader = Config::SwitchDepthOnlyWithoutPixelShader;
    options.trimPixelOutputs = Config::SwitchTrimPixelOutputs;
    options.trimVertexOutputs = Config::SwitchTrimVertexOutputs;
    options.constantsUbo = Config::SwitchConstantsUBO;
    options.trimConstantUploads = Config::SwitchTrimConstantUploads;
    options.skipUnusedPixelConstants = Config::SwitchSkipUnusedPixelConstants;
    options.copyKeepsVertexConstants = options.constantsUbo && Config::SwitchCopyKeepsVertexConstants;

    // Stage 4 (resolves, clears, barriers and framebuffers). The exact coverage proof serves the coverage hand-overs
    // and the skipped clears; each option stands on its own.
    options.resolveStats = Config::SwitchResolveStats;
    options.preciseBarriers = Config::SwitchPreciseBarriers;
    options.preciseSurveyBarriers = options.preciseBarriers && Config::SwitchPreciseSurveyBarriers;
    options.zcull = Config::SwitchZcull;
    options.skipNoOpDraws = Config::SwitchSkipNoOpDraws;
    options.eagerSampleTransitions = Config::SwitchEagerSampleTransitions;
    options.lazyResolves = Config::SwitchLazyResolves;
    options.resolveHandOver = Config::SwitchResolveHandOver;
    options.keepResolvesPending = Config::SwitchKeepResolvesPending;
    options.coverageHandOver = Config::SwitchCoverageHandOver;
    options.exactCoverage = Config::SwitchExactCoverage;
    options.skipOverwrittenClears = Config::SwitchSkipOverwrittenClears;
    options.skipRestoreDraws = Config::SwitchSkipRestoreDraws;
    options.depthArrayTexturesD32 = Config::SwitchDepthArrayTexturesD32;
    options.depthTexturesD32 = Config::SwitchDepthTexturesD32;
    // Follows the Present rule of the port's longer pending copies (CascadeAtPresent), which lazy resolves turn on.
    options.cascadeAdoption = Config::SwitchCascadeAdoption && options.lazyResolves;
    options.stableFramebuffers = Config::SwitchStableFramebuffers;
    options.readOnlyDepthSampling = Config::SwitchReadOnlyDepthSampling;
    options.uniformStencilClears = Config::SwitchUniformStencilClears;

    // Stage 5 (the translated shaders' specialization bits). The gather specialization only applies to the gathers, the
    // sinking only to the early-out (or the transparent-pixel skip), and never while verifying (the shaders run their
    // previous code then anyway). The indexed constants only differ from the uniform buffers.
    options.textureSizeConstants = Config::SwitchTextureSizeConstants;
    options.verifyTextureSizes = Config::SwitchVerifyTextureSizes;
    options.shadowGather = Config::SwitchShadowGather || Config::SwitchVerifyShadowGather;
    options.verifyShadowGather = Config::SwitchVerifyShadowGather;
    options.shadowGatherSpecialization = options.shadowGather && Config::SwitchShadowGatherSpecialization;
    options.alphaTestEarlyOut = Config::SwitchAlphaTestEarlyOut;
    options.alphaTestSink = Config::SwitchAlphaTestSink && !options.verifyShadowGather && !options.verifyTextureSizes;
    options.skipTransparentPixels = Config::SwitchSkipTransparentPixels;
    options.vertexSwapSpecialization = Config::SwitchVertexSwapSpecialization;
    options.clipDistanceSpecialization = Config::SwitchClipDistanceSpecialization;
    options.surveySlots = Config::SwitchSurveySlots;
    options.indexedConstantsFromMemory = options.constantsUbo && Config::SwitchIndexedConstantsFromMemory;

    // Stage 6 (measurement). The draw profiler needs the pass profiler's passes.
    options.gpuDrawProfiler = Config::SwitchGpuDrawProfiler;
    options.gpuSlowFrameMs = uint32_t(std::max<int32_t>(0, Config::SwitchGpuSlowFrameMs));
    options.gpuPassProfiler = Config::SwitchGpuPassProfiler || options.gpuDrawProfiler || options.gpuSlowFrameMs != 0;
    options.frameLog = Config::SwitchFrameLog;
    options.showProfiler = Config::SwitchShowProfiler;

    // Before the device exists: plume reads both when it records barriers and creates depth targets.
    plume::SetVulkanPreciseBarriers(options.preciseBarriers);
    plume::SetSwitchZcullDepthTargets(options.zcull);

    // The resolve statistics, the profilers' reports and the frame log go to stderr.log only, so these keys open it
    // (gpuPassProfiler includes SwitchGpuDrawProfiler). EnableStderrLog does nothing when an earlier area has opened it
    // already (SwitchPerfInitDiagnostics runs first, before the other areas print their init lines).
    if (options.resolveStats || options.gpuPassProfiler || options.frameLog)
        os::logger::EnableStderrLog();

    if (options.pipelineCache)
        ConfigurePipelineCache();

    ApplyDriverEnvironment();

    auto onOff = [](bool value) { return value ? "on" : "off"; };
    fprintf(stderr, "Switch renderer: sparse constant copies %s, compact texture heap %s, single copy triangle %s, "
        "host thread cores %s, pipeline lookup cache %s, sampler cache %s, frame limiter sleep %s, survey plain store %s, "
        "gamma push constants %s, query reset per query %s, link varyings %s.\n",
        onOff(options.sparseConstantCopies), onOff(options.compactTextureHeap), onOff(options.singleCopyTriangle),
        onOff(options.hostThreadCores), onOff(options.pipelineLookupCache), onOff(options.samplerCache),
        onOff(options.frameLimiterSleep), onOff(options.surveyPlainStore), onOff(options.gammaPushConstants),
        onOff(options.queryResetPerQuery), onOff(Config::SwitchLinkVaryings));
    fprintf(stderr, "Switch renderer: batch render commands %s (several draws %s, larger batches %s, queue token %s, "
        "zero copy %s, idle render thread batches %s), skip redundant render states %s, skip redundant sampler states %s, "
        "present on render thread %s (without the record wait %s), deferred buffer unlocks %s.\n",
        onOff(options.batchRenderCommands), onOff(options.batchSeveralDraws), onOff(options.largerCommandBatches),
        onOff(options.renderQueueToken), onOff(options.zeroCopyBatches), onOff(options.idleRenderThreadBatches),
        onOff(options.skipRedundantRenderStates), onOff(options.skipRedundantSamplerStates),
        onOff(options.presentOnRenderThread), onOff(options.presentWithoutRecordWait), onOff(options.deferredBufferUnlocks));
    fprintf(stderr, "Switch renderer: pipeline cache %s (saved during play %s), depth-only draws without pixel shader %s, "
        "unused colour channels trimmed %s, unread vertex outputs trimmed %s, constants in uniform buffers %s, "
        "constant uploads trimmed %s, unused pixel constants skipped %s, copies keep vertex constants %s.\n",
        onOff(options.pipelineCache), onOff(options.pipelineCacheSaveDuringPlay), onOff(options.depthOnlyWithoutPixelShader),
        onOff(options.trimPixelOutputs), onOff(options.trimVertexOutputs), onOff(options.constantsUbo),
        onOff(options.trimConstantUploads), onOff(options.skipUnusedPixelConstants), onOff(options.copyKeepsVertexConstants));
    fprintf(stderr, "Switch renderer: resolve stats %s, precise barriers %s, zcull %s, skip no-op draws %s, eager sample transitions %s, "
        "lazy resolves %s, resolve hand-over %s, keep resolves pending %s, coverage hand-over %s, exact coverage %s, "
        "skip overwritten clears %s, skip restore draws %s, depth array textures D32 %s, stable framebuffers %s, "
        "read-only depth sampling %s, uniform stencil clears %s, cascade adoption %s.\n",
        onOff(options.resolveStats), onOff(options.preciseBarriers), onOff(options.zcull), onOff(options.skipNoOpDraws),
        onOff(options.eagerSampleTransitions), onOff(options.lazyResolves), onOff(options.resolveHandOver),
        onOff(options.keepResolvesPending), onOff(options.coverageHandOver), onOff(options.exactCoverage),
        onOff(options.skipOverwrittenClears), onOff(options.skipRestoreDraws), onOff(options.depthArrayTexturesD32),
        onOff(options.stableFramebuffers), onOff(options.readOnlyDepthSampling), onOff(options.uniformStencilClears),
        onOff(options.cascadeAdoption));
    fprintf(stderr, "Switch renderer: texture size constants %s%s, shadow gather %s%s (specialization %s), alpha test early-out %s, "
        "alpha test sink %s, skip transparent pixels %s, vertex swap specialization %s, clip distance specialization %s, "
        "survey slots %s, indexed constants from memory %s.\n",
        onOff(options.textureSizeConstants), options.verifyTextureSizes ? " (verify mode)" : "",
        onOff(options.shadowGather), options.verifyShadowGather ? " (verify mode)" : "", onOff(options.shadowGatherSpecialization),
        onOff(options.alphaTestEarlyOut), onOff(options.alphaTestSink), onOff(options.skipTransparentPixels),
        onOff(options.vertexSwapSpecialization), onOff(options.clipDistanceSpecialization), onOff(options.surveySlots),
        onOff(options.indexedConstantsFromMemory));
    fprintf(stderr, "Switch renderer: GPU pass profiler %s, GPU draw profiler %s, frame log %s, profiler window %s.\n",
        onOff(options.gpuPassProfiler), onOff(options.gpuDrawProfiler), onOff(options.frameLog), onOff(options.showProfiler));

    g_switchRendererConfigured.store(true, std::memory_order_release);
}

#endif
