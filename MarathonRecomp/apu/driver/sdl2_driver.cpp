#include <apu/audio.h>
#include <cpu/guest_thread.h>
#include <kernel/heap.h>
#include <os/logger.h>
#include <ui/game_window.h>

#if defined(__SWITCH__)
extern "C" void SwitchSetCurrentThreadPriority(int priority);
#endif
#include <user/config.h>

#include <atomic>
#include <memory>

#if defined(__SWITCH__)
#include <pthread.h>
#include <switch.h>
#include <arm_neon.h>
#include <apu/audio_switch.h>
#include <os/switch_cpu_profiler.h>
#endif

static PPCFunc* g_clientCallback{};
static uint32_t g_clientCallbackParam{}; // pointer in guest memory
static SDL_AudioDeviceID g_audioDevice{};
static bool g_audioDeviceReady{};
static bool g_downMixToStereo;

#if defined(__SWITCH__)
// [Switch] SwitchAudioOut: the game's audio goes to the console through audout, fed straight from the pump
// thread (0x2B), instead of through SDL.
//
// SDL's Switch backend plays from its own thread, started with SDL_THREAD_PRIORITY_TIME_CRITICAL, which
// devkitPro's SDL maps to Horizon priority 0x3B: the time-sliced band every guest thread spins in. After
// queuing a buffer its play loop waits until that buffer is reported *playing*; a thread starved for longer
// than a buffer finds it already *done* and waits forever, and from then on nothing is played (Unleashed hit
// exactly this on hardware). audout keeps its own queue in the audio service: an underrun is a short gap and
// playback resumes with the next block.
//
// The samples are the SDL path's, bit for bit: the same stereo frames (XAudioSubmitFrame computes them once
// for both paths), converted to S16 by the instructions SDL's stream converter runs on them.
static constexpr uint32_t AUDOUT_BUFFER_COUNT = 16;
static constexpr size_t AUDOUT_BLOCK_BYTES = XAUDIO_NUM_SAMPLES * 2 * sizeof(int16_t);
static constexpr size_t AUDOUT_BUFFER_BYTES = 0x1000; // audout wants 0x1000-aligned buffers and sizes
static_assert(AUDOUT_BLOCK_BYTES <= AUDOUT_BUFFER_BYTES);

static AudioOutBuffer g_audoutBuffers[AUDOUT_BUFFER_COUNT];
static bool g_audoutQueuedFlags[AUDOUT_BUFFER_COUNT];
static uint8_t* g_audoutMemory = nullptr;
static uint32_t g_audoutQueued = 0;
static uint64_t g_audoutAppended = 0;
static bool g_audoutReady = false;
static bool g_audoutStarted = false;

static void CreateAudoutDevice()
{
    Result rc = audoutInitialize();
    if (R_FAILED(rc))
    {
        LOGFN_ERROR("audoutInitialize failed: 0x{:X}", rc);
        return;
    }

    rc = audoutStartAudioOut();
    if (R_FAILED(rc))
    {
        LOGFN_ERROR("audoutStartAudioOut failed: 0x{:X}", rc);
        audoutExit();
        return;
    }

    g_audoutMemory = static_cast<uint8_t*>(aligned_alloc(AUDOUT_BUFFER_BYTES, AUDOUT_BUFFER_COUNT * AUDOUT_BUFFER_BYTES));
    if (g_audoutMemory == nullptr)
    {
        LOGN_ERROR("Could not allocate the audout buffers.");
        audoutStopAudioOut();
        audoutExit();
        return;
    }

    memset(g_audoutMemory, 0, AUDOUT_BUFFER_COUNT * AUDOUT_BUFFER_BYTES);
    for (uint32_t i = 0; i < AUDOUT_BUFFER_COUNT; i++)
    {
        g_audoutBuffers[i].next = nullptr;
        g_audoutBuffers[i].buffer = g_audoutMemory + i * AUDOUT_BUFFER_BYTES;
        g_audoutBuffers[i].buffer_size = AUDOUT_BUFFER_BYTES;
        g_audoutBuffers[i].data_size = AUDOUT_BLOCK_BYTES;
        g_audoutBuffers[i].data_offset = 0;
        g_audoutQueuedFlags[i] = false;
    }

    // audout is stereo, 48 kHz, 16-bit: what SDL's device was for the stereo channel configuration.
    g_downMixToStereo = true;
    g_audoutReady = true;

    fprintf(stderr, "Switch audio: audout %u Hz, %u channels, %u-sample blocks fed by the pump thread\n",
        audoutGetSampleRate(), audoutGetChannelCount(), uint32_t(XAUDIO_NUM_SAMPLES));
}

// Takes back every block the service has played. Pump thread only.
static void ReclaimAudoutBuffers()
{
    while (g_audoutQueued != 0)
    {
        AudioOutBuffer* released = nullptr;
        uint32_t releasedCount = 0;
        if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &releasedCount)) || releasedCount == 0 || released == nullptr)
            break;

        const size_t index = size_t(released - g_audoutBuffers);
        if (index < AUDOUT_BUFFER_COUNT && g_audoutQueuedFlags[index])
        {
            g_audoutQueuedFlags[index] = false;
            g_audoutQueued--;
        }
    }
}

// F32 to S16 exactly as the SDL path converted these frames. devkitPro's SDL (2.28.5) only has the NEON
// converter, SDL_Convert_F32_to_S16_NEON. Its audio stream converts each device callback (1024 stereo frames,
// 2048 floats) in a 16-byte aligned work buffer, so every sample goes through its 8-sample vector loop: clamp
// with fmax/fmin to [-1, 1], multiply by 32767, fcvtzs, narrow. This is that loop, instruction for instruction
// (its scalar head and tail, which give -32768 rather than -32767 at -1.0, never run for these buffers).
static void ConvertFramesToS16(const float* in, int16_t* out, size_t count)
{
    const float32x4_t one = vdupq_n_f32(1.0f);
    const float32x4_t negone = vdupq_n_f32(-1.0f);
    const float32x4_t mulby32767 = vdupq_n_f32(32767.0f);

    for (size_t i = 0; i < count; i += 8)
    {
        const int32x4_t ints1 = vcvtq_s32_f32(vmulq_f32(vminq_f32(vmaxq_f32(negone, vld1q_f32(in + i)), one), mulby32767));
        const int32x4_t ints2 = vcvtq_s32_f32(vmulq_f32(vminq_f32(vmaxq_f32(negone, vld1q_f32(in + i + 4)), one), mulby32767));
        vst1q_s16(out + i, vcombine_s16(vmovn_s32(ints1), vmovn_s32(ints2)));
    }
}

static_assert((2 * XAUDIO_NUM_SAMPLES) % 8 == 0);

// Called by the game's mixer, on the pump thread.
static void SubmitAudoutFrames(const float* frames)
{
    uint32_t index = 0;
    while (index < AUDOUT_BUFFER_COUNT && g_audoutQueuedFlags[index])
        index++;

    if (index == AUDOUT_BUFFER_COUNT)
        return; // Every block is queued (the pump never lets that happen).

    ConvertFramesToS16(frames, static_cast<int16_t*>(g_audoutBuffers[index].buffer), 2 * XAUDIO_NUM_SAMPLES);

    g_audoutBuffers[index].data_size = AUDOUT_BLOCK_BYTES;
    g_audoutBuffers[index].data_offset = 0;
    if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&g_audoutBuffers[index])))
    {
        g_audoutQueuedFlags[index] = true;
        g_audoutQueued++;
        g_audoutAppended++;
        g_audoutStarted = true;
    }
}
#endif

static void CreateAudioDevice()
{
    if (g_audioDevice != NULL)
    {
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
        g_audioDeviceReady = false;
    }

    bool surround = Config::ChannelConfiguration == EChannelConfiguration::Surround;
    int allowedChanges = surround ? SDL_AUDIO_ALLOW_CHANNELS_CHANGE : 0;

    SDL_AudioSpec desired{}, obtained{};
    desired.freq = XAUDIO_SAMPLES_HZ;
    desired.format = AUDIO_F32SYS;
    desired.channels = surround ? XAUDIO_NUM_CHANNELS : 2;
#if defined(__SWITCH__)
    // Give audren device-side headroom; a single 256-sample period is too tight
    // for the shared cores and underruns below the SDL queue.
    desired.samples = XAUDIO_NUM_SAMPLES * 4;
#else
    desired.samples = XAUDIO_NUM_SAMPLES;
#endif
    g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, allowedChanges);

    if (g_audioDevice && obtained.channels != 2 && obtained.channels != XAUDIO_NUM_CHANNELS) // This check may fail only when surround sound is enabled.
    {
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
        obtained = {};
        g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    }

    if (!g_audioDevice)
    {
        LOGFN_ERROR("Failed to open audio device: {}", SDL_GetError());
        g_downMixToStereo = true;
        g_audioDeviceReady = false;
        return;
    }

    g_audioDeviceReady = true;
    g_downMixToStereo = (obtained.channels == 2);
}

void XAudioInitializeSystem()
{
#ifdef _WIN32
    // Force wasapi on Windows.
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif

#if defined(__SWITCH__)
    XAudioSetGuestCallbacksEnabled(false);

    // [Switch] SwitchAudioOut = false, or audout failing to start, keeps the SDL path. So does the surround
    // channel configuration: SDL then opens a 6-channel device, which audout (stereo) cannot reproduce.
    if (g_switchAudioOut && Config::ChannelConfiguration == EChannelConfiguration::Stereo)
    {
        CreateAudoutDevice();
        if (g_audoutReady)
            return;
    }
#endif

    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_NAME, "Marathon Recompiled");

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        LOGFN_ERROR("Failed to init audio subsystem: {}", SDL_GetError());
        return;
    }

    CreateAudioDevice();
}

#if defined(__SWITCH__)
static pthread_t g_audioThread{};
static bool g_audioThreadCreated{};
#else
static std::unique_ptr<std::thread> g_audioThread;
#endif
static std::atomic<bool> g_audioThreadShouldExit;
#if defined(__SWITCH__)
static std::atomic<bool> g_audioGuestCallbacksEnabled;
#endif

void XAudioSetGuestCallbacksEnabled(bool enabled)
{
#if defined(__SWITCH__)
    g_audioGuestCallbacksEnabled.store(enabled, std::memory_order_release);
#else
    (void)enabled;
#endif
}

static bool AreGuestCallbacksEnabled()
{
#if defined(__SWITCH__)
    return g_audioGuestCallbacksEnabled.load(std::memory_order_acquire);
#else
    return true;
#endif
}

#if defined(__SWITCH__)
static void* AudioThread(void*)
#else
static void AudioThread()
#endif
{
    using namespace std::chrono_literals;

    std::unique_ptr<GuestThreadContext> ctx;

    size_t channels = g_downMixToStereo ? 2 : XAUDIO_NUM_CHANNELS;

#if defined(__SWITCH__)
    // Audio must preempt the spinning guest worker threads (0x3B) to hold cadence.
    SwitchSetCurrentThreadPriority(0x2B);
    // [Switch] SwitchAudioThreadCores: pinned to the XMA decoders' core, off the game's main core.
    SwitchAudioSetCurrentThreadCore();
    os::switch_cpu_profiler::RegisterCurrentThread("audio pump");

    // audout needs a few blocks queued to ride out a late tick; the queue is topped up to this many
    // (16 ms) whenever it runs low, and never grows past MAX_LATENCY blocks.
    constexpr uint32_t TARGET_QUEUED_BLOCKS = 3;

    // Absolute-deadline pacing: advance the deadline by one interval per tick so
    // the mixer runs at a steady rate. Snapping to the wall-clock grid jitters on
    // the shared cores and drains the SDL queue into an audible stutter.
    constexpr auto PUMP_INTERVAL = std::chrono::nanoseconds(1000000000ll * XAUDIO_NUM_SAMPLES / XAUDIO_SAMPLES_HZ);
    auto pumpDeadline = std::chrono::steady_clock::now();
#endif

    while (!g_audioThreadShouldExit.load(std::memory_order_acquire))
    {
        constexpr size_t MAX_LATENCY = 10;
#if defined(__SWITCH__)
        if (g_audoutReady)
        {
            ReclaimAudoutBuffers();

            if (g_audoutStarted && g_audoutQueued == 0)
                g_switchAudioUnderruns.fetch_add(1, std::memory_order_relaxed);

            // One block per tick, as with SDL; more only to rebuild the cushion after a late tick or a gap.
            const uint32_t blocksToRender = g_audoutQueued >= TARGET_QUEUED_BLOCKS ? 1 : TARGET_QUEUED_BLOCKS - g_audoutQueued;

            for (uint32_t block = 0; block < blocksToRender; block++)
            {
                if (g_audoutQueued > MAX_LATENCY)
                    break;

                if (!AreGuestCallbacksEnabled() || g_clientCallback == nullptr)
                    break;

                if (ctx == nullptr)
                    ctx = std::make_unique<GuestThreadContext>(0);

                // A catch-up block: let the XMA decoders (same priority, not time-sliced) refill their rings
                // after the previous mixer call, as they would have between two ticks. The yield hands them this
                // core (their only one with SwitchAudioThreadCores); without the key they may also run on another.
                if (block != 0)
                    std::this_thread::yield();

                const uint64_t appendedBefore = g_audoutAppended;
                ctx->ppcContext.r3.u32 = g_clientCallbackParam;
                g_clientCallback(ctx->ppcContext, g_memory.base);

                // A mixer call that submitted nothing: no catch-up this tick.
                if (g_audoutAppended == appendedBefore)
                    break;
            }
        }
        else
#endif
        {
            uint32_t queuedAudioSize = g_audioDevice ? SDL_GetQueuedAudioSize(g_audioDevice) : 0;
            const size_t callbackAudioSize = channels * XAUDIO_NUM_SAMPLES * sizeof(float);

            if ((queuedAudioSize / callbackAudioSize) <= MAX_LATENCY)
            {
                if (AreGuestCallbacksEnabled() && g_clientCallback != nullptr)
                {
                    if (ctx == nullptr)
                        ctx = std::make_unique<GuestThreadContext>(0);

                    ctx->ppcContext.r3.u32 = g_clientCallbackParam;
                    g_clientCallback(ctx->ppcContext, g_memory.base);
                }
            }
        }

#if defined(__SWITCH__)
        pumpDeadline += PUMP_INTERVAL;
        auto now = std::chrono::steady_clock::now();
        if (now >= pumpDeadline)
            pumpDeadline = now + PUMP_INTERVAL;
        else
            std::this_thread::sleep_until(pumpDeadline);
#else
        auto now = std::chrono::steady_clock::now();
        constexpr auto INTERVAL = 1000000000ns * XAUDIO_NUM_SAMPLES / XAUDIO_SAMPLES_HZ;
        auto next = now + (INTERVAL - now.time_since_epoch() % INTERVAL);

        std::this_thread::sleep_for(std::chrono::floor<std::chrono::milliseconds>(next - now));

        while (std::chrono::steady_clock::now() < next)
            std::this_thread::yield();
#endif
    }

#if defined(__SWITCH__)
    os::switch_cpu_profiler::UnregisterCurrentThread();
    return nullptr;
#endif
}

static void CreateAudioThread()
{
#if defined(__SWITCH__)
    if (!g_audoutReady)
#endif
    SDL_PauseAudioDevice(g_audioDevice, 0);
    g_audioThreadShouldExit = false;
#if defined(__SWITCH__)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    constexpr auto AUDIO_THREAD_STACK_SIZE = 2 * 1024 * 1024;
    const auto stackResult = pthread_attr_setstacksize(&attr, AUDIO_THREAD_STACK_SIZE);
    if (stackResult != 0)
        LOGFN_ERROR("Switch XAudio pthread_attr_setstacksize failed: 0x{:X}", stackResult);

    const auto createResult = pthread_create(&g_audioThread, &attr, AudioThread, nullptr);
    pthread_attr_destroy(&attr);
    if (createResult != 0)
    {
        LOGFN_ERROR("Switch XAudio pthread_create failed: 0x{:X}", createResult);
        return;
    }

    g_audioThreadCreated = true;
#else
    g_audioThread = std::make_unique<std::thread>(AudioThread);
#endif
}

static void JoinAudioThread()
{
    g_audioThreadShouldExit = true;
#if defined(__SWITCH__)
    if (g_audioThreadCreated)
    {
        pthread_join(g_audioThread, nullptr);
        g_audioThreadCreated = false;
    }
#else
    if (g_audioThread != nullptr && g_audioThread->joinable())
        g_audioThread->join();
#endif
}

void XAudioRegisterClient(PPCFunc* callback, uint32_t param)
{
    auto* pClientParam = static_cast<uint32_t*>(g_userHeap.Alloc(sizeof(param)));
    ByteSwapInplace(param);
    *pClientParam = param;
    g_clientCallbackParam = g_memory.MapVirtual(pClientParam);
    g_clientCallback = callback;

    CreateAudioThread();
}

void XAudioSubmitFrame(void* samples)
{
    auto floatSamples = reinterpret_cast<be<float>*>(samples);
    auto volume = Config::MasterVolume.Value;

    if (Config::MuteOnFocusLost && !GameWindow::s_isFocused)
        volume = 0.0f;

    if (g_downMixToStereo)
    {
        // 0: left 1.0f, right 0.0f
        // 1: left 0.0f, right 1.0f
        // 2: left 0.75f, right 0.75f
        // 3: left 0.0f, right 0.0f
        // 4: left 1.0f, right 0.0f
        // 5: left 0.0f, right 1.0f

        std::array<float, 2 * XAUDIO_NUM_SAMPLES> audioFrames;

        for (size_t i = 0; i < XAUDIO_NUM_SAMPLES; i++)
        {
            float ch0 = floatSamples[0 * XAUDIO_NUM_SAMPLES + i];
            float ch1 = floatSamples[1 * XAUDIO_NUM_SAMPLES + i];
            float ch2 = floatSamples[2 * XAUDIO_NUM_SAMPLES + i];
            float ch3 = floatSamples[3 * XAUDIO_NUM_SAMPLES + i];
            float ch4 = floatSamples[4 * XAUDIO_NUM_SAMPLES + i];
            float ch5 = floatSamples[5 * XAUDIO_NUM_SAMPLES + i];

            float samp0 = (ch0 + ch2 * 0.75f + ch4) * volume;
            float samp1 = (ch1 + ch2 * 0.75f + ch5) * volume;

            audioFrames[i * 2 + 0] = isnan(samp0) ? 0.0f : samp0;
            audioFrames[i * 2 + 1] = isnan(samp1) ? 0.0f : samp1;
        }

#if defined(__SWITCH__)
        // [Switch] SwitchAudioOut: these same frames, converted as SDL converted them.
        if (g_audoutReady)
        {
            SubmitAudoutFrames(audioFrames.data());
            return;
        }
#endif

        if (g_audioDevice)
            SDL_QueueAudio(g_audioDevice, &audioFrames, sizeof(audioFrames));
    }
    else
    {
        std::array<float, XAUDIO_NUM_CHANNELS * XAUDIO_NUM_SAMPLES> audioFrames;

        for (size_t i = 0; i < XAUDIO_NUM_SAMPLES; i++)
        {
            for (size_t j = 0; j < XAUDIO_NUM_CHANNELS; j++)
            {
                float samp = floatSamples[j * XAUDIO_NUM_SAMPLES + i] * volume;
                audioFrames[i * 2 + j] = isnan(samp) ? 0.0f : samp;
            }
        }

        if (g_audioDevice)
            SDL_QueueAudio(g_audioDevice, &audioFrames, sizeof(audioFrames));
    }
}

void XAudioConfigValueChangedCallback(IConfigDef* configDef)
{
    if (configDef == &Config::ChannelConfiguration)
    {
        JoinAudioThread();

#if defined(__SWITCH__)
        // audout stays open (the setting needs a restart to switch paths).
        if (!g_audoutReady)
#endif
        CreateAudioDevice();
        CreateAudioThread();
    }
}
