#if defined(__SWITCH__)

#include <stdafx.h>
#include <os/switch/perf/native_hooks.h>

// native_crt.cpp: memmove for guest memory, safe for a resumed fault (not the C library's memcpy).
void* GuestMemmove(void* destination, const void* source, size_t size);

// [Switch] SwitchNativeShaderConstants (native_hooks.h). The D3D device's float constant setters (reached through the
// engine's device wrapper: sub_8291C488/C520/C5A8 and sub_8289C518 for vertex, sub_8291C7B8/C808/C8A0 and
// sub_8289C4A0 for pixel): copy r6 float4 registers from r5 (any alignment, emulated lvlx/lvrx pairs) into the
// stage's array at register r4 (stvx: 16-byte stores at the aligned address (r3 + (r4 + first) * 16) & ~15), then OR
// r7 into the stage's 64-bit dirty word at r3 + dirtyOffset (GuestDevice, gpu/video.h: dirtyFlags at 0 and 8,
// vertexShaderFloatConstants at 0x700 = register 112, pixelShaderFloatConstants at 0x1700 = register 368). A copy of
// r6 * 16 bytes and the same OR are byte-identical when the source and destination do not overlap.
//
// Both setters also spill r7 into the caller's frame (std r7,48(r1)), which is done here too, and r3 below the stack
// pointer (stw r3,-32(r1)), which is dead once they return; they read both back after the copy. The recompiled code
// runs for overlapping ranges (it copies 64 bytes at a time), for a copy that touches those two stack slots, and for
// ranges that wrap around the 4 GB guest space.
static bool UploadShaderConstants(PPCContext& ctx, uint8_t* base, uint32_t firstRegister, uint32_t dirtyOffset)
{
    const uint64_t destination = uint32_t(ctx.r3.u32 + ((ctx.r4.u32 + firstRegister) << 4)) & ~0xFu;
    const uint64_t source = ctx.r5.u32;
    const uint64_t size = uint64_t(ctx.r6.u32) * 16;
    const uint64_t stackSlots = uint32_t(ctx.r1.u32 - 32);
    const uint64_t stackSlotsEnd = stackSlots + 32 + 56;

    auto overlaps = [](uint64_t a, uint64_t aEnd, uint64_t b, uint64_t bEnd) { return a < bEnd && b < aEnd; };
    if (destination + size > PPC_MEMORY_SIZE || source + size > PPC_MEMORY_SIZE || stackSlotsEnd > PPC_MEMORY_SIZE ||
        overlaps(source, source + size, destination, destination + size) ||
        overlaps(destination, destination + size, stackSlots, stackSlotsEnd) ||
        overlaps(source, source + size, stackSlots, stackSlotsEnd))
    {
        return false;
    }

    PPC_STORE_U64(ctx.r1.u32 + 48, ctx.r7.u64);
    GuestMemmove(base + destination, base + source, size);

    const uint32_t dirty = ctx.r3.u32 + dirtyOffset;
    PPC_STORE_U64(dirty, PPC_LOAD_U64(dirty) | ctx.r7.u64);
    return true;
}

// Vertex shader constants (addi r10,r4,112; dirty word at 0).
PPC_FUNC_IMPL(__imp__sub_82546818);
PPC_FUNC(sub_82546818)
{
    if (!g_switchNativeShaderConstants || !UploadShaderConstants(ctx, base, 112, 0))
        __imp__sub_82546818(ctx, base);
}

// Pixel shader constants (addi r10,r4,368; dirty word at 8).
PPC_FUNC_IMPL(__imp__sub_82546928);
PPC_FUNC(sub_82546928)
{
    if (!g_switchNativeShaderConstants || !UploadShaderConstants(ctx, base, 368, 8))
        __imp__sub_82546928(ctx, base);
}

#endif
