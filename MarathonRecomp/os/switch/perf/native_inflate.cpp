#if defined(__SWITCH__)

#include <stdafx.h>
#include <kernel/memory.h>
#include <os/switch/perf/native_hooks.h>
#include <os/switch_cpu_profiler.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include <zlib.h>

// [Switch] SwitchNativeInflate, SwitchVerifyNativeInflate (native_hooks.h). Sonic 06 has no XMemDecompress (LZX);
// its archives (.arc, U8) store zlib streams, and every compressed file the game loads goes through one function,
// sub_825BE758 = bool Inflate(destination r3, destinationSize r4, source r5, sourceSize r6), called by the async file
// read job (sub_828B2BF8, which ignores the result). It runs the statically linked zlib 1.2.3 as recompiled code on
// the loader thread: a z_stream on its stack (zalloc/zfree/opaque 0), inflateInit_ (sub_826DC038: window bits 15, a
// zlib header and adler32 trailer), one inflate(Z_NO_FLUSH) (sub_826DC180), and 1 only for Z_STREAM_END followed by
// inflateEnd (sub_826DD7B8) == Z_OK; anything else prints strm->msg to the guest's stderr and returns 0 (after a
// failed inflate without inflateEnd).
//
// Here the host's zlib decodes the stream into a buffer of its own first, with the same settings (inflateInit: window
// bits 15, zlib header and adler32 checked). The decompressed bytes of a valid stream are uniquely determined, and
// zlib writes exactly those, so for a stream it decodes to the end they are what the guest's decoder writes; they are
// then copied to the destination and the result is 1. Anything else (an error, a destination too small, null,
// overlapping, wrapping or uncommitted ranges, no memory) runs the recompiled code on untouched guest memory. What
// the game can no longer see: the guest heap's allocation and release of the 9,520-byte inflate state within the call
// (net nothing), and the z_stream and saved registers below the stack pointer. Verifying, the recompiled code always
// runs as well and its output is compared with the native one ("[inflate]" lines in stderr.log). Compared on the
// build PC with the recompiled decoder: identical for all 21,064 compressed entries of the game's 93 archives (3.4 GB
// decoded).

// native_crt.cpp: memmove for guest memory, safe for a resumed fault (not the C library's memcpy).
void* GuestMemmove(void* destination, const void* source, size_t size);

namespace
{
    std::atomic<uint32_t> g_inflateCalls;
    std::atomic<uint32_t> g_inflateNative;
    std::atomic<uint32_t> g_inflateMismatches;
    std::atomic<uint64_t> g_inflateBytes;
    std::atomic<uint64_t> g_inflateNanoseconds;

    bool Overlap(uint64_t a, uint64_t aSize, uint64_t b, uint64_t bSize)
    {
        return a < b + bSize && b < a + aSize;
    }

    // Every guest page of the range committed (kernel/memory.cpp). A page's flag is set once it is mapped and never
    // cleared, so it is read without the commit mutex, as os/switch/exception_switch.cpp does.
    bool IsCommitted(uint32_t address, uint32_t size)
    {
        constexpr uint64_t PageSize = 0x1000;
        if (size == 0)
            return true;

        const uint64_t lastPage = (uint64_t(address) + size - 1) / PageSize;
        if (lastPage >= g_memory.committedPages.size())
            return false;

        for (uint64_t page = address / PageSize; page <= lastPage; page++)
        {
            if (__atomic_load_n(g_memory.committedPages.data() + page, __ATOMIC_ACQUIRE) == 0)
                return false;
        }

        return true;
    }

    // The decompressed bytes and their number when the host's zlib decodes the stream to its end.
    std::unique_ptr<uint8_t[]> InflateNative(const uint8_t* source, uint32_t sourceSize, uint32_t destinationSize, uint32_t& produced)
    {
        std::unique_ptr<uint8_t[]> output(new (std::nothrow) uint8_t[std::max<uint32_t>(destinationSize, 1)]);
        if (!output)
            return nullptr;

        z_stream stream{};
        stream.next_in = const_cast<Bytef*>(source);
        stream.avail_in = sourceSize;
        stream.next_out = output.get();
        stream.avail_out = destinationSize;
        if (inflateInit(&stream) != Z_OK)
            return nullptr;

        // Z_FINISH: the whole stream in this one call (no sliding window is kept); the bytes are the same.
        const int result = inflate(&stream, Z_FINISH);
        produced = destinationSize - stream.avail_out;
        inflateEnd(&stream);
        if (result != Z_STREAM_END)
            return nullptr;

        return output;
    }

    void WriteLine(const char* format, ...) __attribute__((format(printf, 1, 2)));
    void WriteLine(const char* format, ...)
    {
        char line[256];
        va_list args;
        va_start(args, format);
        const int length = vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        os::switch_cpu_profiler::WriteLog(line, std::min(size_t(std::max(length, 0)), sizeof(line) - 1));
    }
}

PPC_FUNC_IMPL(__imp__sub_825BE758);
PPC_FUNC(sub_825BE758)
{
    if (!g_switchNativeInflate && !g_switchVerifyNativeInflate)
    {
        __imp__sub_825BE758(ctx, base);
        return;
    }

    const uint32_t destination = ctx.r3.u32;
    const uint32_t destinationSize = ctx.r4.u32;
    const uint32_t source = ctx.r5.u32;
    const uint32_t sourceSize = ctx.r6.u32;
    const auto start = std::chrono::steady_clock::now();

    // Natively only what the game's decoder decodes the same way:
    // - zlib 1.2.3's inflate returns Z_STREAM_ERROR, writing nothing, for a null next_out, or a null next_in with
    //   input left; the host zlib is never given a null pointer.
    // - Committed guest memory only. The game's decoder reads its output back (matches, adler32), so on an
    //   uncommitted destination page (zeros read, stores dropped) its result is not the native one. And the host
    //   zlib reads the source in place, prebuilt code like the memcpy it calls: a fault there would be resumed
    //   through x16, which only code built with -ffixed-x16 keeps free (os/switch/exception_switch.cpp). With both
    //   ranges committed, nothing below faults.
    uint32_t produced = 0;
    std::unique_ptr<uint8_t[]> output;
    if (destination != 0 && (source != 0 || sourceSize == 0) &&
        uint64_t(destination) + destinationSize <= PPC_MEMORY_SIZE && uint64_t(source) + sourceSize <= PPC_MEMORY_SIZE &&
        !Overlap(destination, destinationSize, source, sourceSize) &&
        IsCommitted(source, sourceSize) && IsCommitted(destination, destinationSize))
    {
        output = InflateNative(base + source, sourceSize, destinationSize, produced);
    }

    const bool native = output != nullptr;
    uint32_t decoded = 0;
    if (native && !g_switchVerifyNativeInflate)
    {
        GuestMemmove(base + destination, output.get(), produced);
        ctx.r3.u64 = 1;
        decoded = produced;
    }
    else if (native)
    {
        // Every byte the guest's decoder does not write as the native one did shows: the destination starts out as the
        // complement of the native output (the guest's decoder only reads back what it wrote), and a few bytes after
        // its end are kept to see whether the guest writes past it.
        uint8_t* guestOutput = base + destination;
        for (uint32_t i = 0; i < produced; i++)
            guestOutput[i] = uint8_t(~output[i]);

        uint8_t after[64];
        const uint32_t afterSize = std::min<uint32_t>(destinationSize - produced, sizeof(after));
        GuestMemmove(after, guestOutput + produced, afterSize);

        __imp__sub_825BE758(ctx, base);
        decoded = produced;

        if (ctx.r3.u32 != 1 || memcmp(guestOutput, output.get(), produced) != 0 || memcmp(after, guestOutput + produced, afterSize) != 0)
        {
            const uint32_t mismatches = ++g_inflateMismatches;
            if (mismatches <= 20)
            {
                WriteLine("[inflate] MISMATCH #%u: %u compressed bytes into %u: game result %u, native %u bytes\n",
                    mismatches, sourceSize, destinationSize, ctx.r3.u32, produced);
            }
        }
    }
    else
    {
        __imp__sub_825BE758(ctx, base);
        decoded = ctx.r3.u32 == 1 ? destinationSize : 0;
    }

    const uint64_t nanoseconds = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
    const uint32_t calls = ++g_inflateCalls;
    const uint32_t nativeCalls = native ? ++g_inflateNative : g_inflateNative.load();
    const uint64_t bytes = (g_inflateBytes += decoded);
    const uint64_t totalNanoseconds = (g_inflateNanoseconds += nanoseconds);

    // A line now and then, from the loader thread.
    if ((calls % 64) == 0)
    {
        WriteLine("[inflate] %u decompressions, %u native%s, %.1f MB out in %.0f ms (%.1f MB/s)%s\n", calls, nativeCalls,
            g_switchVerifyNativeInflate ? " and compared with the game's decoder" : "", double(bytes) / 1e6, double(totalNanoseconds) / 1e6,
            totalNanoseconds != 0 ? double(bytes) / 1e6 / (double(totalNanoseconds) / 1e9) : 0.0,
            g_switchVerifyNativeInflate ? (g_inflateMismatches.load() == 0 ? ", all identical" : ", MISMATCHES") : "");
    }
}

#endif
