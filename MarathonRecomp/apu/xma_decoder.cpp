/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2024 Xenia Canary. All rights reserved.                          *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

// Almost all decoding code is from Xenia Canary, so leave the copyright here

#include "xma_decoder.h"
#include <atomic>
#if defined(__SWITCH__)
#include <chrono>
#include <os/logger.h>
#include <apu/audio_switch.h>
#include <os/switch_cpu_profiler.h>
extern "C" void SwitchSetCurrentThreadPriority(int priority);
#endif

// #define ENABLE_DEBUG_XMA_DECODER

#ifdef ENABLE_DEBUG_XMA_DECODER
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...)
#endif

constexpr uint32_t kOutputBytesPerBlock = 256;
constexpr uint32_t kBitsPerPacketHeader = 32;
constexpr uint32_t kBitsPerPacket = kBytesPerPacket * 8;
constexpr uint32_t kMaxFrameLength = 0x7FFF;
constexpr uint32_t kBitsPerFrameHeader = 15;
constexpr uint32_t kMaxFrameSizeinBits = 0x4000 - kBitsPerPacketHeader;

// The decoded PCM is published by the offset that exposes it: the decoder stores outputBufferWriteOffset (and a
// full ring's outputBufferValid = 0) with release after writing the samples, and the guest-side calls load them
// with acquire before handing out pointers to the samples. Same values in the same order as plain accesses. The
// decoder and the guest's threads can run on different cores (libnx gives every thread the process's core mask, with
// or without [Switch] SwitchAudioThreadCores), and this keeps a reader from seeing an offset before the samples it
// covers.
static inline uint32_t LoadAcquire(uint32_t &value) {
    return std::atomic_ref<uint32_t>(value).load(std::memory_order_acquire);
}

static inline void StoreRelease(uint32_t &value, uint32_t newValue) {
    std::atomic_ref<uint32_t>(value).store(newValue, std::memory_order_release);
}

uint32_t XMAPlaybackGetFrameOffsetFromPacketHeader(uint32_t header) {
    uint32_t result = 0;

    if (header != 0x7FFF) {
        return ((header >> 11) & 0x7FFF) + 32;
    }

    return result;
}

int16_t GetPacketNumber(size_t size, size_t bitOffset) {
    if (bitOffset < kBitsPerPacketHeader) {
        return -1;
    }

    if (bitOffset >= (size << 3)) {
        return -1;
    }

    size_t byteOffset = bitOffset >> 3;
    size_t packetNumber = byteOffset / kBytesPerPacket;

    return (int16_t)packetNumber;
}

static uint32_t GetPacketFrameOffset(const uint8_t *packet) {
    uint32_t val = (uint16_t)(((packet[0] & 0x3) << 13) | (packet[1] << 5) | (packet[2] >> 3));
    return val + 32;
}

static uint8_t GetPacketFrameCount(const uint8_t *packet) {
    return packet[0] >> 2;
}

static bool IsPacketXma2Type(const uint8_t *packet) {
    return (packet[2] & 0x7) == 1;
}

struct kPacketInfo {
    uint8_t frameCount;
    uint8_t currentFrame;
    uint32_t currentFrameSize;

    const bool IsLastFrameInPacket() const {
        return currentFrame == frameCount - 1;
    }
};

kPacketInfo GetPacketInfo(uint8_t *packet, uint32_t frameOffset) {
    kPacketInfo packetInfo = {};

    const uint32_t firstFrameOffset = GetPacketFrameOffset(packet);
    BitStream stream(packet, kBitsPerPacket);
    stream.SetOffset(firstFrameOffset);

    // Handling of split frame
    if (frameOffset < firstFrameOffset) {
        packetInfo.currentFrame = 0;
        packetInfo.currentFrameSize = firstFrameOffset - frameOffset;
    }

    while (true) {
        if (stream.BitsRemaining() < kBitsPerFrameHeader) {
            break;
        }

        const uint64_t frameSize = stream.Peek(kBitsPerFrameHeader);
        if (frameSize == 0 || frameSize == kMaxFrameLength) {
            break;
        }

        if (stream.offset_bits() == frameOffset) {
            packetInfo.currentFrame = packetInfo.frameCount;
            packetInfo.currentFrameSize = (uint32_t)frameSize;
        }

        packetInfo.frameCount++;

        if (frameSize > stream.BitsRemaining()) {
            // Last frame.
            break;
        }

        stream.Advance(frameSize - 1);

        // Read the trailing bit to see if frames follow
        if (stream.Read(1) == 0) {
            break;
        }
    }

    if (IsPacketXma2Type(packet)) {
        const uint8_t headerFrameCount = GetPacketFrameCount(packet);
        if (headerFrameCount > packetInfo.frameCount) {
            // The final frame header may be split across the packet boundary,
            // so the scanner cannot peek its complete 15-bit size yet.
            if (packetInfo.currentFrameSize == 0) {
                packetInfo.currentFrame = packetInfo.frameCount;
            }
            packetInfo.frameCount = headerFrameCount;
        }
    }

    return packetInfo;
}

struct PacketHandle {
    uint8_t bufferIndex = 0;
    uint32_t packetIndex = 0;
    bool valid = false;
};

PacketHandle GetPacketHandle(XmaPlaybackStream *playback, uint32_t packetIndex,
                             uint32_t currentInputPacketCount) {
    PacketHandle result{};
    uint8_t bufferIndex = playback->currentBuffer;

    if (packetIndex >= currentInputPacketCount) {
        bufferIndex ^= 1;
        packetIndex -= currentInputPacketCount;
    }

    if (!playback->IsInputBufferValid(bufferIndex)) {
        return result;
    }

    const uint32_t bufferAddress = playback->GetInputBufferAddress(bufferIndex);

    if (!bufferAddress ||
        packetIndex >= playback->GetInputBufferPacketCount(bufferIndex)) {
        // This should never occur, but there is always a chance
        debug_printf("XmaContext: requested packet is not present in its valid buffer!\n");
        return result;
    }

    result.bufferIndex = bufferIndex;
    result.packetIndex = packetIndex;
    result.valid = true;
    return result;
}

uint8_t *GetNextPacket(XmaPlaybackStream *playback, uint32_t nextPacketIndex,
                       uint32_t currentInputPacketCount) {
    const PacketHandle handle = GetPacketHandle(
        playback, nextPacketIndex, currentInputPacketCount);
    if (!handle.valid) {
        return nullptr;
    }

    const uint32_t bufferAddress = playback->GetInputBufferAddress(handle.bufferIndex);
    return (uint8_t *)g_memory.Translate(bufferAddress) +
        handle.packetIndex * kBytesPerPacket;
}

uint8_t GetPacketSkipCount(const uint8_t *packet) { return packet[3]; }

uint32_t GetAmountOfBitsToRead(const uint32_t remainingStreamBits, const uint32_t frameSize) {
    return std::min(remainingStreamBits, frameSize);
}

template <typename T> T clamp_float(T value, T minValue, T maxValue) {
    float clampedToMin = std::isgreater(value, minValue) ? value : minValue;
    return std::isless(clampedToMin, maxValue) ? clampedToMin : maxValue;
}

uint32_t GetNextPacketReadOffset(uint8_t *buffer, uint32_t nextPacketIndex,
                                 uint32_t currentInputPacketCount) {
    while (nextPacketIndex < currentInputPacketCount) {
        uint8_t *nextPacket = buffer + (nextPacketIndex * kBytesPerPacket);
        const uint32_t packetFrameOffset = GetPacketFrameOffset(nextPacket);

        if (packetFrameOffset <= kMaxFrameSizeinBits) {
            return (nextPacketIndex * kBitsPerPacket) + packetFrameOffset;
        }

        nextPacketIndex++;
    }

    return kBitsPerPacketHeader;
}

uint32_t GetNextPacketReadOffset(XmaPlaybackStream *playback, uint32_t nextPacketIndex,
                                 uint32_t currentInputPacketCount) {
    const PacketHandle handle = GetPacketHandle(
        playback, nextPacketIndex, currentInputPacketCount);
    if (!handle.valid) {
        return kBitsPerPacketHeader;
    }

    const uint32_t bufferAddress = playback->GetInputBufferAddress(handle.bufferIndex);
    return GetNextPacketReadOffset(
        (uint8_t *)g_memory.Translate(bufferAddress), handle.packetIndex,
        playback->GetInputBufferPacketCount(handle.bufferIndex));
}

void SwapInputBuffer(XmaPlaybackStream *playback) {
    // No more frames.
    if (playback->currentBuffer == 0) {
        playback->inputBuffer1Valid = 0;
    } else {
        playback->inputBuffer2Valid = 0;
    }

    playback->currentBuffer ^= 1;
    playback->inputBufferReadOffset = kBitsPerPacketHeader;
}

void UpdateLoopStatus(XmaPlaybackStream *playback) {
    if (playback->numLoops == 0) {
        return;
    }

    const uint32_t loop_start = std::max(kBitsPerPacketHeader, playback->loopStartOffset);
    const uint32_t loop_end = std::max(kBitsPerPacketHeader, playback->loopEndOffset);

    if (playback->inputBufferReadOffset != loop_end) {
        return;
    }

    playback->inputBufferReadOffset = loop_start;

    if (playback->numLoops != 255) {
        playback->numLoops--;
    }
}

void Decode(XmaPlaybackStream *playback) {
    if (!playback->IsAnyInputBufferValid()) {
        return;
    }

    if (playback->currentFrameRemainingSubframes > 0) {
        return;
    }

    if (!playback->IsCurrentInputBufferValid()) {
        SwapInputBuffer(playback);
        if (!playback->IsCurrentInputBufferValid()) {
            return;
        }
    }

    uint8_t *currentInputBuffer = playback->GetCurrentInputBuffer();

    playback->inputBuffer.fill(0);

    UpdateLoopStatus(playback); // TODO

    const uint32_t currentInputSize = playback->GetCurrentInputBufferPacketCount() * kBytesPerPacket;
    uint32_t currentInputPacketCount = currentInputSize / kBytesPerPacket;
    int16_t packetIndex = GetPacketNumber(currentInputSize, playback->inputBufferReadOffset);

    if (packetIndex == -1) {
        return;
    }

    uint8_t *packet = currentInputBuffer + (packetIndex * kBytesPerPacket);

    const uint32_t firstFrameOffset = GetPacketFrameOffset(packet);
    uint32_t relativeOffset = playback->inputBufferReadOffset % kBitsPerPacket;

    // An offset before this packet's first frame points at the tail of a frame
    // that began in an earlier stream packet. Decoding that tail as a complete
    // frame produces a short, valid-looking block of garbage PCM.
    if (relativeOffset < firstFrameOffset) {
        playback->inputBufferReadOffset =
            (packetIndex * kBitsPerPacket) + firstFrameOffset;
        relativeOffset = firstFrameOffset;
    }

    const uint8_t skipCount = GetPacketSkipCount(packet);
    if (skipCount == 0xFF) {
        // No frame starts in this packet. Advance sequentially rather than
        // overflowing skipCount + 1 back to the same packet.
        const uint32_t nextPacketIndex = packetIndex + 1;
        uint32_t nextInputOffset = GetNextPacketReadOffset(
            playback, nextPacketIndex, currentInputPacketCount);
        if (nextPacketIndex >= currentInputPacketCount ||
            nextInputOffset == kBitsPerPacketHeader) {
            SwapInputBuffer(playback);
        }
        playback->inputBufferReadOffset = nextInputOffset;
        return;
    }

    kPacketInfo packetInfo = GetPacketInfo(packet, relativeOffset);
    const uint32_t packetToSkip = skipCount + 1;
    const uint32_t nextPacketIndex = packetIndex + packetToSkip;

    if (packetInfo.currentFrameSize == 0) {
        // The 15-bit frame header itself crosses the stream-packet boundary.
        const uint8_t *nextPacket = GetNextPacket(
            playback, nextPacketIndex, currentInputPacketCount);
        if (!nextPacket) {
            SwapInputBuffer(playback);
            return;
        }

        std::memcpy(playback->inputBuffer.data(),
                    packet + kBytesPerPacketHeader, kBytesPerPacketData);
        std::memcpy(playback->inputBuffer.data() + kBytesPerPacketData,
                    nextPacket + kBytesPerPacketHeader, kBytesPerPacketData);

        BitStream combined(playback->inputBuffer.data(),
            (kBitsPerPacket - kBitsPerPacketHeader) * 2);
        combined.SetOffset(relativeOffset - kBitsPerPacketHeader);
        const uint64_t frameSize = combined.Peek(kBitsPerFrameHeader);
        if (frameSize == 0 || frameSize == kMaxFrameLength) {
            SwapInputBuffer(playback);
            return;
        }
        packetInfo.currentFrameSize = static_cast<uint32_t>(frameSize);
    }

    BitStream stream = BitStream(currentInputBuffer, (packetIndex + 1) * kBitsPerPacket);
    stream.SetOffset(playback->inputBufferReadOffset);

    const uint64_t bitsToCopy = GetAmountOfBitsToRead((uint32_t)stream.BitsRemaining(),
                                                      packetInfo.currentFrameSize);

    if (bitsToCopy == 0) {
        SwapInputBuffer(playback);
        return;
    }

    if (packetInfo.IsLastFrameInPacket()) {
        // Frame is a split frame
        if (stream.BitsRemaining() < packetInfo.currentFrameSize) {
            const uint8_t *nextPacket = GetNextPacket(playback, nextPacketIndex, currentInputPacketCount);

            if (!nextPacket) {
                // Error path
                // Decoder probably should return error here
                // Not sure what error code should be returned
                // data->error_status = 4;
                __builtin_debugtrap();
                return;
            }

            // Copy next packet to buffer
            std::memcpy(playback->inputBuffer.data() + kBytesPerPacketData,
                        nextPacket + kBytesPerPacketHeader, kBytesPerPacketData);
        }
    }

    std::memcpy(playback->inputBuffer.data(), packet + kBytesPerPacketHeader, kBytesPerPacketData);

    stream = BitStream(playback->inputBuffer.data(), (kBitsPerPacket - kBitsPerPacketHeader) * 2);
    stream.SetOffset(relativeOffset - kBitsPerPacketHeader);

    playback->xmaFrame.fill(0);

    const uint32_t paddingStart = static_cast<uint8_t>(stream.Copy(playback->xmaFrame.data() + 1,
                                                                   packetInfo.currentFrameSize));

    std::fill(playback->rawFrame.begin(), playback->rawFrame.end(), 0);
    playback->av_packet_->data = playback->xmaFrame.data();
    playback->av_packet_->size = static_cast<int>(1 + ((paddingStart + packetInfo.currentFrameSize) / 8) +
                                                  (((paddingStart + packetInfo.currentFrameSize) % 8) ? 1 : 0));

    auto paddingEnd = playback->av_packet_->size * 8 - (8 + paddingStart + packetInfo.currentFrameSize);
    playback->xmaFrame[0] = ((paddingStart & 7) << 5) | ((paddingEnd & 7) << 2);

    const auto sendResult = avcodec_send_packet(playback->codec_ctx, playback->av_packet_);
    if (sendResult < 0) {
        debug_printf("Error sending packet for decoding: %s\n", av_err2str(sendResult));
    }

    const auto receiveResult = sendResult >= 0
        ? avcodec_receive_frame(playback->codec_ctx, playback->av_frame_)
        : sendResult;
    if (receiveResult < 0) {
        debug_printf("Error receiving frame from decoder: %s\n", av_err2str(receiveResult));
    }

    constexpr float scale = (1 << 15) - 1;
    auto out = reinterpret_cast<int16_t *>(playback->rawFrame.data());
    auto samples = reinterpret_cast<const uint8_t **>(&playback->av_frame_->data);

    uint32_t o = 0;
    const bool decodedFrameValid = receiveResult >= 0 &&
        playback->av_frame_->nb_samples >= kSamplesPerFrame &&
        playback->av_frame_->ch_layout.nb_channels ==
            static_cast<int>(playback->channelCount);

    if (decodedFrameValid) {
        for (uint32_t i = 0; i < kSamplesPerFrame; i++) {
            for (uint32_t j = 0; j < playback->av_frame_->ch_layout.nb_channels; j++) {
                // Select the appropriate array based on the current channel.
                auto in = reinterpret_cast<const float *>(samples[j]);

                // Raw samples sometimes aren't within [-1, 1]
                float scaledSample = clamp_float(in[i], -1.0f, 1.0f) * scale;

                // Convert the sample and output it in big endian.
                auto sample = static_cast<int16_t>(scaledSample);
                out[o++] = ByteSwap(sample);
            }
        }
    }

    if (decodedFrameValid) {
        const uint32_t totalSubframeBlocks = 4 * playback->channelCount;
        const uint32_t requestedSubframeSkip = playback->numSubframesToSkip;
        const uint32_t skippedSubframeBlocks = std::min(
            requestedSubframeSkip * playback->channelCount, totalSubframeBlocks);
        playback->currentFrameRemainingSubframes = static_cast<uint8_t>(
            totalSubframeBlocks - skippedSubframeBlocks);
        playback->numSubframesToSkip = 0;
    }

    if (!packetInfo.IsLastFrameInPacket()) {
        const uint32_t nextFrameOffset = (playback->inputBufferReadOffset + bitsToCopy) % kBitsPerPacket;

        playback->inputBufferReadOffset = (packetIndex * kBitsPerPacket) + nextFrameOffset;
        return;
    }

    uint32_t nextInputOffset = GetNextPacketReadOffset(
        playback, nextPacketIndex, currentInputPacketCount);

    if (nextPacketIndex >= currentInputPacketCount ||
        nextInputOffset == kBitsPerPacketHeader) {
        SwapInputBuffer(playback);
    }

    if (nextInputOffset == kBitsPerPacketHeader) {
        // We're at start of next buffer
        // Any frames in this packet decoder should go to the first frame in the packet.
        // If it doesn't have any frames, then it should immediately go to the next packet.
        if (playback->IsAnyInputBufferValid()) {
            nextInputOffset = GetPacketFrameOffset((uint8_t *)g_memory.Translate(
                    playback->GetCurrentInputBufferAddress()));

            if (nextInputOffset > kMaxFrameSizeinBits) {
                SwapInputBuffer(playback);
                return;
            }
        } else {
            // HACK
            SwapInputBuffer(playback);
        }
    }

    playback->inputBufferReadOffset = nextInputOffset;
}

void Consume(XmaPlaybackStream *playback) {
    if (!playback->currentFrameRemainingSubframes) {
        return;
    }

    const int8_t subframesToWrite = std::min((int8_t)playback->currentFrameRemainingSubframes,
                                             (int8_t)playback->subframes);

    const int8_t rawFrameReadOffset = ((kBytesPerFrameChannel / kOutputBytesPerBlock) * playback->channelCount)
                                      - playback->currentFrameRemainingSubframes;

    playback->outputRb.Write(playback->rawFrame.data() +
                             (kOutputBytesPerBlock * rawFrameReadOffset),
                             subframesToWrite * kOutputBytesPerBlock);
    playback->remainingSubframeBlocksInOutputBuffer -= subframesToWrite;
    playback->currentFrameRemainingSubframes -= subframesToWrite;
}

#if defined(__SWITCH__)
// [Switch] SwitchXmaEventWait: whether a decode pass could change anything now. Caller holds the stream's mutex.
// A pass writes only while the guest does not hold the modify lock and the ring is not marked full, when the
// ring has room for its minimum (computed as the pass computes it) and there is input left or a decoded frame
// still to copy out; otherwise it changes no state. A pass that started at wake generation `stalledGeneration`
// and changed nothing (a malformed or incomplete packet) would change nothing again until something else
// changes, which bumps the generation.
static bool DecoderHasWork(XmaPlaybackStream *playback, bool stalled, uint32_t stalledGeneration) {
    if (!playback->isRunning)
        return true;

    if (playback->isLocked.load() || !playback->outputBufferValid)
        return false;

    if (stalled && playback->decoderWakeGeneration == stalledGeneration)
        return false;

    if (!playback->IsAnyInputBufferValid() && playback->currentFrameRemainingSubframes == 0)
        return false;

    const size_t outputCapacity = playback->outputBufferBlockCount * kOutputBytesPerBlock;
    if (outputCapacity == 0)
        return false;

    RingBuffer ring(nullptr, outputCapacity);
    ring.set_read_offset(playback->outputBufferReadOffset * kOutputBytesPerBlock);
    ring.set_write_offset(playback->outputBufferWriteOffset * kOutputBytesPerBlock);
    return (int32_t)ring.write_count() / (int32_t)kOutputBytesPerBlock >= std::max<int32_t>(1, playback->subframes);
}

// Everything a pass can change that decides what the next pass does.
static std::array<uint32_t, 8> DecoderPassState(const XmaPlaybackStream *playback) {
    return { playback->inputBufferReadOffset, playback->currentFrameRemainingSubframes, playback->currentBuffer,
        playback->inputBuffer1Valid, playback->inputBuffer2Valid, playback->outputBufferWriteOffset,
        playback->outputBufferValid, playback->numLoops };
}
#endif

void DecoderThreadFunc(XmaPlaybackStream *playback) {
#if defined(__SWITCH__)
    // Audio-critical: preempt spinning guest threads (see runtime_switch.cpp).
    SwitchSetCurrentThreadPriority(0x2B);
    // [Switch] SwitchAudioThreadCores: pinned to the audio pump's core, off the game's main core.
    SwitchAudioSetCurrentThreadCore();
    os::switch_cpu_profiler::RegisterCurrentThread("xma decoder");

    // [Switch] SwitchXmaEventWait: the last pass changed nothing, and the wake generation it started at.
    bool stalled = false;
    uint32_t stalledGeneration = 0;
#endif
    while (playback->isRunning) {
        std::unique_lock<std::mutex> lock(playback->mutex);

        auto ready = [&] {
            return (!playback->isRunning || (playback->outputBufferValid == 1 &&
                                             playback->IsAnyInputBufferValid())) &&
                   !playback->isLocked.load();
        };
#if defined(__SWITCH__)
        (void)ready;
        if (g_switchXmaEventWait) {
            // [Switch] SwitchXmaEventWait: sleep until a pass can make progress instead of waking 500 times a
            // second. Every change that can allow one notifies (resume, submit, flush, destroy); the guest makes
            // all the others while it holds the modify lock, and its resume notifies. The timeout only bounds a
            // change made outside that protocol, which then gets the 2 ms poll's behaviour, later.
            if (!DecoderHasWork(playback, stalled, stalledGeneration)) {
                playback->decoderWaiting = true;
                playback->decoderCv.wait_for(lock, std::chrono::milliseconds(20),
                    [&] { return DecoderHasWork(playback, stalled, stalledGeneration); });
                playback->decoderWaiting = false;
            }
        } else {
            // Notify-driven wait, 2 ms liveness cap. No predicate on purpose: a
            // predicate makes wait_for return instantly while there is decodable
            // state, busy-spinning the decoder and starving the audio pump. The 2 ms
            // timeout covers a dropped notify_one (a bare cv.wait would deadlock).
            playback->cv.wait_for(lock, std::chrono::milliseconds(2));
        }
#else
        playback->cv.wait(lock, ready);
#endif

        if (!playback->outputBufferValid || playback->isLocked.load())
            continue;

        if (!playback->isRunning)
            break;

#if defined(__SWITCH__)
        const uint32_t passGeneration = playback->decoderWakeGeneration;
        const auto passState = DecoderPassState(playback);
#endif
        playback->decoderWorking.store(true, std::memory_order_release);
        lock.unlock();

        // Consume() writes at most `subframes` blocks per pass, independent of
        // channel count. Requiring a full stereo frame's worth of free space
        // can unnecessarily stall the decoder and expose stale ring data.
        const int32_t minimumSubframeDecodeCount =
            std::max<int32_t>(1, playback->subframes);

        size_t outputCapacity = playback->outputBufferBlockCount * kOutputBytesPerBlock;

        const uint32_t outputReadOffset = playback->outputBufferReadOffset * kOutputBytesPerBlock;
        const uint32_t outputWriteOffset = playback->outputBufferWriteOffset * kOutputBytesPerBlock;

        playback->outputRb = RingBuffer((uint8_t *)g_memory.Translate(playback->outputBuffer),
                                        outputCapacity);
        playback->outputRb.set_read_offset(outputReadOffset);
        playback->outputRb.set_write_offset(outputWriteOffset);
        playback->remainingSubframeBlocksInOutputBuffer = (int32_t)playback->outputRb.write_count()
                                                          / kOutputBytesPerBlock;

        if (minimumSubframeDecodeCount > playback->remainingSubframeBlocksInOutputBuffer) {
            playback->bAllowedToDecode = false;
            lock.lock();
            playback->decoderWorking.store(false, std::memory_order_release);
            playback->cv.notify_all();
#if defined(__SWITCH__)
            stalled = true;
            stalledGeneration = passGeneration;
#endif
            continue;
        }

        while (playback->remainingSubframeBlocksInOutputBuffer >= minimumSubframeDecodeCount) {
            const uint32_t preDecodeOffset = playback->inputBufferReadOffset;
            const uint8_t preDecodeRemainingSubframes =
                playback->currentFrameRemainingSubframes;

            Decode(playback);
            Consume(playback);

            if (!playback->IsAnyInputBufferValid()) {
                break;
            }

            // Avoid spinning forever if a malformed or temporarily incomplete
            // boundary packet could neither advance nor produce PCM.
            if (preDecodeOffset == playback->inputBufferReadOffset &&
                preDecodeRemainingSubframes ==
                    playback->currentFrameRemainingSubframes) {
                break;
            }
        }

        StoreRelease(playback->outputBufferWriteOffset, playback->outputRb.write_offset() / kOutputBytesPerBlock);
        playback->bAllowedToDecode = false;

        // Equal read/write offsets are ambiguous in this ring: they can mean
        // either empty or full. Only publish a full ring after Decode has
        // consumed every tracked free subframe block.
        if (playback->remainingSubframeBlocksInOutputBuffer == 0) {
            StoreRelease(playback->outputBufferValid, 0);
        }

        lock.lock();
        playback->decoderWorking.store(false, std::memory_order_release);
        playback->cv.notify_all();
#if defined(__SWITCH__)
        stalled = DecoderPassState(playback) == passState;
        stalledGeneration = passGeneration;
#endif
    }

#if defined(__SWITCH__)
    os::switch_cpu_profiler::UnregisterCurrentThread();
#endif
}

uint32_t XMAPlaybackCreate(uint32_t streams, XMAPLAYBACKINIT *init, uint32_t flags, be<uint32_t> *outPlayback) {
    if (streams == 0 || init == nullptr || outPlayback == nullptr) {
        return 0x80070057;
    }

    const auto xmaPlayback = g_userHeap.AllocPhysical<XmaPlayback>(streams, init);
    if (xmaPlayback == nullptr) {
        return 0x8007000E;
    }

    for (uint32_t streamIndex = 0; streamIndex < streams; streamIndex++) {
        auto *stream = xmaPlayback->GetStream(streamIndex);
#if defined(__SWITCH__)
        // Runtime-created std::thread is unreliable on this toolchain; use pthreads.
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 512 * 1024);
        const int rc = pthread_create(&stream->decoderThread, &attr,
            [](void* arg) -> void* {
                DecoderThreadFunc(static_cast<XmaPlaybackStream*>(arg));
                return nullptr;
            },
            stream);
        pthread_attr_destroy(&attr);
        stream->decoderThreadCreated = (rc == 0);
        if (rc != 0)
            LOGFN_ERROR("!!! XMA decoder pthread_create failed: stream={} error=0x{:X}",
                streamIndex, rc);
#else
        stream->decoderThread = std::thread(DecoderThreadFunc, stream);
#endif
    }
    *outPlayback = g_memory.MapVirtual(xmaPlayback);

    return 0;
}

uint32_t XMAPlaybackRequestModifyLock(XmaPlayback *playback) {
    for (auto &stream : playback->streams) {
        std::lock_guard<std::mutex> lock(stream->mutex);
        stream->isLocked = true;
    }

    return 0;
}

uint32_t XMAPlaybackWaitUntilModifyLockObtained(XmaPlayback *playback) {
    for (auto &stream : playback->streams) {
        std::unique_lock<std::mutex> lock(stream->mutex);
        stream->cv.wait(lock, [&stream] {
            return stream->isLocked.load(std::memory_order_acquire) &&
                !stream->decoderWorking.load(std::memory_order_acquire);
        });
    }

    return 0;
}

uint32_t XMAPlaybackQueryReadyForMoreData(XmaPlayback *playback, uint32_t stream) {
    const auto context = playback->GetStream(stream);
    return context && (context->inputBuffer1Valid == 0 || context->inputBuffer2Valid == 0);
}

uint32_t XMAPlaybackIsIdle(XmaPlayback *playback, uint32_t stream) {
    const auto context = playback->GetStream(stream);
    return context && context->inputBuffer1Valid == 0 && context->inputBuffer2Valid == 0;
}

uint32_t XMAPlaybackQueryContextsAllocated(XmaPlayback *playback) {
    if (!playback) {
        return 0;
    }

    return static_cast<uint32_t>(playback->streams.size());
}

uint32_t XMAPlaybackResumePlayback(XmaPlayback *playback) {
    for (auto &stream : playback->streams) {
        std::lock_guard<std::mutex> lock(stream->mutex);
        stream->isLocked = false;
        stream->cv.notify_one();
#if defined(__SWITCH__)
        stream->NotifyDecoderLocked();
#endif
    }

    return 0;
}

uint32_t XMAPlaybackQueryInputDataPending(XmaPlayback *playback, uint32_t stream, uint32_t data) {
    const auto context = playback->GetStream(stream);
    if (!context) {
        return 0;
    }

    if (context->inputBuffer1Valid && context->inputBuffer1 == data) {
        return 1;
    }

    if (context->inputBuffer2Valid && context->inputBuffer2 == data) {
        return 1;
    }

    return 0;
}

uint32_t XMAPlaybackGetErrorBits(XmaPlayback *playback, uint32_t stream) {
    return 0;
}

uint32_t XMAPlaybackSubmitData(XmaPlayback *playback, uint32_t stream, uint32_t data, uint32_t dataSize) {
    auto context = playback->GetStream(stream);
    if (!context) {
        return 0x80070057;
    }

    if (!context->isLocked) {
        return 1;
    }

    std::lock_guard<std::mutex> lock(context->mutex);
    uint32_t packetCount = dataSize >> 11;

    uint32_t validBuffer = context->inputBuffer1Valid | (context->inputBuffer2Valid << 1);

    if (context->inputBuffer1Valid == 1) {
        if (context->inputBuffer2Valid == 1) {
            return 0x80070005;
        }
        context->inputBuffer2 = data;
        context->inputBuffer2Size = packetCount & 0xFFF;

        context->inputBuffer2Valid = 1;
    } else {
        context->inputBuffer1 = data;
        context->inputBuffer1Size = packetCount & 0xFFF;

        context->inputBuffer1Valid = 1;
    }

    if (!validBuffer) {
        uint8_t *currentInputBuffer = (uint8_t *)g_memory.Translate(data);
        uint32_t frameOffset = XMAPlaybackGetFrameOffsetFromPacketHeader(*currentInputBuffer);

        if (frameOffset) {
            context->inputBufferReadOffset = frameOffset & 0x3FFFFFF;
        }
    }

    context->bAllowedToDecode = true;
    context->cv.notify_one();
#if defined(__SWITCH__)
    context->NotifyDecoderLocked();
#endif
    return 0;
}

uint32_t XMAPlaybackQueryAvailableData(XmaPlayback *playback, uint32_t stream) {
    const auto context = playback->GetStream(stream);
    if (!context || !context->isLocked) {
        return 0;
    }

    uint32_t partialBytesRead = context->partialBytesRead;
    uint32_t writeBufferOffsetRead = context->outputBufferReadOffset & 0x1F;
    uint32_t offsetWrite = LoadAcquire(context->outputBufferWriteOffset);
    uint32_t sizeWrite = context->outputBufferBlockCount & 0x1F;

    uint32_t availableBytes = 0;
    uint32_t isValidWrite = LoadAcquire(context->outputBufferValid);

    if (partialBytesRead) {
        availableBytes = 256 - partialBytesRead;
        writeBufferOffsetRead++;
        isValidWrite = 1;
    }

    uint32_t availableBlocks = 0;
    if (offsetWrite <= writeBufferOffsetRead) {
        if (offsetWrite < writeBufferOffsetRead || !isValidWrite) {
            availableBlocks = sizeWrite - writeBufferOffsetRead;
        }
    } else {
        availableBlocks = offsetWrite - writeBufferOffsetRead;
    }

    uint32_t totalBytes = (availableBlocks << 8) + availableBytes;
    uint32_t bytesPerSample = context->channelCount;

    return totalBytes >> bytesPerSample;
}


uint32_t XMAPlaybackAccessDecodedData(XmaPlayback *playback, uint32_t stream, uint32_t **data) {
    const auto context = playback->GetStream(stream);
    if (!context || !context->isLocked)
        return 0;

    uint32_t partialBytesRead = context->partialBytesRead;
    uint32_t addr = reinterpret_cast<uint32_t>(
            context->outputBuffer +
            ((context->outputBufferReadOffset << 8) & 0x1F00) +
            partialBytesRead);
    ;
    *data = (uint32_t *)__builtin_bswap32(addr);

    uint32_t writeBufferOffsetRead = context->outputBufferReadOffset & 0x1F;
    uint32_t offsetWrite = LoadAcquire(context->outputBufferWriteOffset);
    uint32_t sizeWrite = context->outputBufferBlockCount & 0x1F;

    uint32_t availableBytes = 0;
    uint32_t isValidWrite = LoadAcquire(context->outputBufferValid);

    if (partialBytesRead) {
        availableBytes = 256 - partialBytesRead;
        writeBufferOffsetRead++;
        isValidWrite = 1;
    }

    uint32_t availableBlocks = 0;
    if (offsetWrite <= writeBufferOffsetRead) {
        if (offsetWrite < writeBufferOffsetRead || !isValidWrite) {
            availableBlocks = sizeWrite - writeBufferOffsetRead;
        }
    } else {
        availableBlocks = offsetWrite - writeBufferOffsetRead;
    }

    uint32_t totalBytes = (availableBlocks << 8) + availableBytes;
    uint32_t bytesPerSample = context->channelCount;

    return totalBytes >> bytesPerSample;
}

uint32_t XMAPlaybackConsumeDecodedData(XmaPlayback *playback, uint32_t stream, uint32_t maxSamples, uint32_t **data) {
    auto context = playback->GetStream(stream);
    if (!context || !context->isLocked) {
        return 0;
    }

    uint32_t totalBytes = 0;
    uint32_t partialBytesRead = context->partialBytesRead;
    bool wrappedAfterPartialBlock = false;
    uint32_t addr = reinterpret_cast<uint32_t>(
            context->outputBuffer +
            ((context->outputBufferReadOffset << 8) & 0x1F00) +
            partialBytesRead);

    *data = (uint32_t *)__builtin_bswap32(addr);
    uint32_t bytesPerSample = context->channelCount;
    uint32_t bytesDesired = maxSamples << bytesPerSample;
    if (partialBytesRead) {
        if (bytesDesired < 256 - partialBytesRead) {
            totalBytes = bytesDesired;
            context->partialBytesRead += bytesDesired;
            bytesDesired = 0;
        } else {
            totalBytes = 256 - partialBytesRead;
            bytesDesired -= 256 - partialBytesRead;
            context->partialBytesRead = 0;

            uint32_t writeIndex = (context->outputBufferReadOffset + 1) & 0x1F;

            if (writeIndex >= (context->outputBufferBlockCount & 0x1F)) {
                writeIndex = 0;
                wrappedAfterPartialBlock = true;
            }

            context->outputBufferReadOffset = writeIndex;
            context->outputBufferValid = 1;
        }
    }

    // A guest-visible read returns one pointer, so it must never span the end
    // of the physical ring. If completing a partial last block wrapped the
    // read offset, let the caller request the next contiguous range separately.
    if (wrappedAfterPartialBlock) {
        bytesDesired = 0;
    }

    uint32_t writeIndex = context->outputBufferReadOffset;
    uint32_t blocksToProcess = bytesDesired >> 8;
    uint32_t availableBlocks = 0;

    // Cap the returned contiguous range at the decoder's actual write offset.
    // Using the ring capacity here makes unwritten/stale blocks appear valid.
    uint32_t writeSize = LoadAcquire(context->outputBufferWriteOffset);
    if (writeSize <= writeIndex) {
        if (writeSize < writeIndex || !LoadAcquire(context->outputBufferValid)) {
            availableBlocks = (context->outputBufferBlockCount & 0x1F) - writeIndex;
        }
    } else {
        availableBlocks = writeSize - writeIndex;
    }

    if (blocksToProcess) {
        if (blocksToProcess > availableBlocks) {
            blocksToProcess = availableBlocks;
        }

        totalBytes += blocksToProcess << 8;
        availableBlocks -= blocksToProcess;
        writeIndex = (writeIndex + blocksToProcess) & 0x1F;

        if (writeIndex >= (context->outputBufferBlockCount & 0x1F)) {
            writeIndex = 0;
        }

        context->outputBufferReadOffset = writeIndex;
        context->outputBufferValid = 1;
    }

    uint32_t remainingBytes = bytesDesired & 0xFF;
    if (remainingBytes && availableBlocks) {
        totalBytes += remainingBytes;
        context->partialBytesRead = remainingBytes;
    }

    // playback->bAllowedToDecode = true;
    // playback->cv.notify_one();

    uint32_t samplesConsumed = totalBytes >> bytesPerSample;
    context->streamPosition += samplesConsumed;

    return samplesConsumed;
}

uint32_t XMAPlaybackQueryModifyLockObtained(XmaPlayback *playback) {
    debug_printf("XMAPlaybackQueryModifyLockObtained %x\n", playback);
    for (const auto &stream : playback->streams) {
        if (!stream->isLocked.load(std::memory_order_acquire) ||
            stream->decoderWorking.load(std::memory_order_acquire)) {
            return 0;
        }
    }

    return 1;
}

uint32_t XMAPlaybackDestroy(XmaPlayback *playback) {
    debug_printf("XMAPlaybackDestroy %x\n", playback);
    if (playback) {
        playback->~XmaPlayback();
        g_userHeap.Free(playback);
    }
    return 0;
}

uint32_t XMAPlaybackFlushData(XmaPlayback *playback, uint32_t streamIndex) {
    debug_printf("XMAPlaybackFlushData %x\n", playback);
    auto stream = playback->GetStream(streamIndex);
    if (!stream) {
        return 0x80070057;
    }

    std::lock_guard<std::mutex> lock(stream->mutex);
    stream->inputBuffer1Valid = 0;
    stream->inputBuffer2Valid = 0;
    stream->currentBuffer = 0;
    stream->inputBufferReadOffset = kBitsPerPacketHeader;
    stream->currentFrameRemainingSubframes = 0;
    stream->numSubframesToSkip = 0;
    stream->partialBytesRead = 0;
    stream->outputBufferReadOffset = 0;
    stream->outputBufferWriteOffset = 0;
    stream->outputBufferValid = 1;
    stream->bAllowedToDecode = false;
    std::memset(g_memory.Translate(stream->outputBuffer), 0,
        stream->outputBufferBlockCount * kOutputBytesPerBlock);
    avcodec_flush_buffers(stream->codec_ctx);
#if defined(__SWITCH__)
    stream->NotifyDecoderLocked();
#endif

    return 0;
}

struct XMAPLAYBACKLOOP {
    be<uint32_t> loopStartOffset;
    be<uint32_t> loopEndOffset;
    uint8_t loopSubframeEnd;
    uint8_t loopSubframeSkip;
    uint8_t numLoops;
    uint8_t reserved;
};

uint32_t XmaPlaybackSetLoop(XmaPlayback *playback, uint32_t streamIndex, XMAPLAYBACKLOOP *loop) {
    auto stream = playback->GetStream(streamIndex);
    if (!stream) {
        return 0x80070057;
    }

    stream->numLoops = loop->numLoops;
    stream->loopSubframeEnd = loop->loopSubframeEnd;
    stream->loopSubframeSkip = loop->loopSubframeSkip;
    stream->loopStartOffset = loop->loopStartOffset.get() & 0x3FFFFFF;
    stream->loopEndOffset = loop->loopEndOffset.get() & 0x3FFFFFF;

    return 0;
}

uint32_t XMAPlaybackGetRemainingLoopCount(XmaPlayback *playback, uint32_t streamIndex) {
    debug_printf("XMAPlaybackGetRemainingLoopCount %x\n", playback);
    const auto stream = playback->GetStream(streamIndex);
    return stream ? stream->numLoops : 0;
}

uint32_t XMAPlaybackGetStreamPosition(XmaPlayback *playback, uint32_t streamIndex) {
    const auto stream = playback->GetStream(streamIndex);
    return stream ? stream->streamPosition : 0;
}

uint32_t XMAPlaybackSetDecodePosition(XmaPlayback *playback, uint32_t streamIndex, uint32_t bitOffset,
                                      uint32_t subframe) {
    auto stream = playback->GetStream(streamIndex);
    if (!stream) {
        return 0x80070057;
    }

    stream->inputBufferReadOffset = bitOffset & 0x3FFFFFF;
    stream->numSubframesToSkip = subframe & 0x7;
    return 0;
}

uint32_t XMAPlaybackRewindDecodePosition(XmaPlayback *playback, uint32_t streamIndex, uint32_t numSamples) {
    auto stream = playback->GetStream(streamIndex);
    if (!stream) {
        return 0;
    }

    uint32_t shift = 7 - (1 != 0);
    uint32_t adjustedSamples = numSamples >> shift;
    uint32_t writeSize = stream->outputBufferBlockCount & 0x1F;

    uint32_t newOffset;
    if (adjustedSamples >= writeSize) {
        newOffset = stream->inputBufferReadOffset & 0x3FFFFFF;
        stream->outputBufferValid = 1;
        stream->outputBufferWriteOffset = newOffset >> 27;
        return 0;
    }

    newOffset = (stream->outputBufferWriteOffset - adjustedSamples + writeSize) & 0x1F;

    if (newOffset >= writeSize) {
        newOffset -= writeSize;
    }

    stream->outputBufferValid = 1;
    stream->outputBufferWriteOffset = newOffset;

    return 1;
}

uint32_t XMAPlaybackQueryCurrentPosition(XmaPlayback *playback, uint32_t streamIndex) {
    debug_printf("XMAPlaybackQueryCurrentPosition %x\n", playback);
    const auto stream = playback->GetStream(streamIndex);
    return stream ? stream->inputBufferReadOffset : 0;
}

GUEST_FUNCTION_HOOK(sub_8255C090, XMAPlaybackCreate);
GUEST_FUNCTION_HOOK(sub_8255CC48, XMAPlaybackRequestModifyLock);
GUEST_FUNCTION_HOOK(sub_8255CCC8, XMAPlaybackWaitUntilModifyLockObtained);
GUEST_FUNCTION_HOOK(sub_8255C4D0, XMAPlaybackQueryReadyForMoreData);
GUEST_FUNCTION_HOOK(sub_8255C520, XMAPlaybackIsIdle);
GUEST_FUNCTION_HOOK(sub_8255C388, XMAPlaybackQueryContextsAllocated);
GUEST_FUNCTION_HOOK(sub_8255CF10, XMAPlaybackResumePlayback);
GUEST_FUNCTION_HOOK(sub_8255C470, XMAPlaybackQueryInputDataPending);
GUEST_FUNCTION_HOOK(sub_8255C9A0, XMAPlaybackGetErrorBits);
GUEST_FUNCTION_HOOK(sub_8255C398, XMAPlaybackSubmitData);
GUEST_FUNCTION_HOOK(sub_8255C578, XMAPlaybackQueryAvailableData);
GUEST_FUNCTION_HOOK(sub_8255C7A8, XMAPlaybackAccessDecodedData);
GUEST_FUNCTION_HOOK(sub_8255C5F0, XMAPlaybackConsumeDecodedData);
GUEST_FUNCTION_HOOK(sub_8255CD90, XMAPlaybackQueryModifyLockObtained);
GUEST_FUNCTION_HOOK(sub_8255C8D8, XMAPlaybackFlushData);
GUEST_FUNCTION_HOOK(sub_8255C9D8, XmaPlaybackSetLoop);
GUEST_FUNCTION_HOOK(sub_8255CA50, XMAPlaybackGetRemainingLoopCount);
GUEST_FUNCTION_HOOK(sub_8255CA90, XMAPlaybackGetStreamPosition);
GUEST_FUNCTION_HOOK(sub_8255CB20, XMAPlaybackSetDecodePosition);
GUEST_FUNCTION_HOOK(sub_8255C850, XMAPlaybackRewindDecodePosition);
GUEST_FUNCTION_HOOK(sub_8255CAB0, XMAPlaybackQueryCurrentPosition);
GUEST_FUNCTION_HOOK(sub_8255C2C0, XMAPlaybackDestroy);
