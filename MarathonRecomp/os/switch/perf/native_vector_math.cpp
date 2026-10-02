#if defined(__SWITCH__)

#include <stdafx.h>
#include <os/logger.h>
#include <os/switch/perf/native_hooks.h>
#include <os/switch_cpu_profiler.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>

// [Switch] SwitchNativeVectorMath (native_hooks.h): the game's hottest vector math leaves, from the perf4 CPU profile at
// the benchmark spot (the game thread at ~20 ms of work per frame): the 4x4 matrix product sub_82168C48 (and its
// operand-swapped twin sub_82272648) and the projection of a box's eight corners to a bound sub_825A1490, together
// ~15 % of the game thread and a few % of the worker threads.
//
// The recompiled bodies keep every vector register in the context, copy both matrices to the stack through 8-byte
// loads and stores and read them back with 16-byte loads (which wait for those stores to land), and the projection
// copies the matrix again on each of its eight passes, behind a loop barrier that sends every register through memory.
// These are the same statements in the same order with the vector registers as locals and the matrix rows read from
// where the guest copied them from: the same simde operations on the same values, so the same results bit for bit
// (each vmaddfp/vnmsubfp is one expression in both, which GCC contracts the same way; no other product meets a sum).
// They write every context register the recompiled code leaves changed (vector, integer, the FPSCR's flush mode; its ctr
// and cr6 are locals of the function with SWITCH_REGISTER_LOCALS, and no caller reads them after a call otherwise) and
// the stores the recompiled code makes at or above the stack pointer; not its copies below it (dead once it returns).
// Anything they do not reproduce with certainty (an unaligned stack pointer, a matrix or corner list that overlaps the
// stack copies) runs the recompiled code. SwitchVerifyNativeVectorMath runs both on every call and compares.

PPC_FUNC_IMPL(__imp__sub_82168C48);
PPC_FUNC_IMPL(__imp__sub_82272648);
PPC_FUNC_IMPL(__imp__sub_825A1490);

namespace
{
    using V = PPCVRegister;

    inline simde__m128i MaskL()
    {
        return simde_mm_load_si128((simde__m128i*)VectorMaskL);
    }

    // lvx128 of a row the recompiled code copied to the stack, read from where it was copied from (any alignment: the
    // copy used 8-byte loads).
    inline void LoadRow(V& d, const uint8_t* source)
    {
        simde_mm_store_si128((simde__m128i*)d.u8, simde_mm_shuffle_epi8(simde_mm_loadu_si128((const simde__m128i*)source), MaskL()));
    }

    // lvx128 / stvx: the 16-byte line of the address.
    inline void Lvx(V& d, uint8_t* base, uint32_t address)
    {
        simde_mm_store_si128((simde__m128i*)d.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + (address & ~0xFu))), MaskL()));
    }

    inline void Stvx(uint8_t* base, uint32_t address, const V& s)
    {
        simde_mm_store_si128((simde__m128i*)(base + (address & ~0xFu)), simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)s.u8), MaskL()));
    }

    // vmrghw d,a,b / vmrglw d,a,b
    inline void Mrghw(V& d, const V& a, const V& b)
    {
        simde_mm_store_si128((simde__m128i*)d.u32, simde_mm_unpackhi_epi32(simde_mm_load_si128((simde__m128i*)b.u32), simde_mm_load_si128((simde__m128i*)a.u32)));
    }

    inline void Mrglw(V& d, const V& a, const V& b)
    {
        simde_mm_store_si128((simde__m128i*)d.u32, simde_mm_unpacklo_epi32(simde_mm_load_si128((simde__m128i*)b.u32), simde_mm_load_si128((simde__m128i*)a.u32)));
    }

    // vmsum4fp128 d,a,b
    inline void Msum4(V& d, const V& a, const V& b)
    {
        simde_mm_store_ps(d.f32, simde_mm_dp_ps(simde_mm_load_ps(a.f32), simde_mm_load_ps(b.f32), 0xFF));
    }

    // vmaddfp d,a,b,c (a * b + c) and vnmsubfp d,a,b,c (c - a * b)
    inline void Maddfp(V& d, const V& a, const V& b, const V& c)
    {
        simde_mm_store_ps(d.f32, simde_mm_vmaddfp(simde_mm_load_ps(a.f32), simde_mm_load_ps(b.f32), simde_mm_load_ps(c.f32)));
    }

    inline void Nmsubfp(V& d, const V& a, const V& b, const V& c)
    {
        simde_mm_store_ps(d.f32, simde_mm_vnmsubfp(simde_mm_load_ps(a.f32), simde_mm_load_ps(b.f32), simde_mm_load_ps(c.f32)));
    }

    // vspltw with the recompiler's shuffle immediate.
    template<int Shuffle>
    inline void Spltw(V& d, const V& s)
    {
        simde_mm_store_si128((simde__m128i*)d.u32, simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i*)s.u32), Shuffle));
    }

    inline void Copy(V& d, const V& s)
    {
        simde_mm_store_si128((simde__m128i*)d.u8, simde_mm_load_si128((simde__m128i*)s.u8));
    }

    // vupkd3d128 d,v10,4 with v10 = 0 (vspltisw v10,0), as the recompiled code computes it.
    inline void UnpackD3dZero(V& d, const V& zero)
    {
        PPCRegister temp{};
        V vTemp{};
        temp.f32 = 3.0f;
        temp.s32 += zero.s16[1];
        vTemp.f32[3] = temp.f32;
        temp.f32 = 3.0f;
        temp.s32 += zero.s16[0];
        vTemp.f32[2] = temp.f32;
        vTemp.f32[1] = 0.0f;
        vTemp.f32[0] = 1.0f;
        d = vTemp;
    }

    // The corner's 1 / w as the guest computes it, on one lane: vspltw splats w (lane 0 here, element 3 of the guest's
    // vector), so vrefp, the two refinement steps, vcmpeqfp and vsel give the same value in every lane, and one scalar
    // chain computes it with the same IEEE operations in the same FPCR (FZ and the rounding mode apply to scalar and
    // vector arithmetic alike): vrefp is a division (as in the recompiled code), each vmaddfp/vnmsubfp one fused
    // operation (GCC contracted every one of them: the same fmla count in the recompiled and the native code), vnmsubfp's
    // result negated (fneg flips the sign bit as the eor did, NaNs included), and vsel a select on r2 == r2. `one` is
    // the vupkd3d128 constant (1.0f). `e` and `e2` are what the guest's v11/v10 (or v0/v13) hold afterwards, splatted.
    struct Reciprocal
    {
        float value;
        float e;
        float e2;
    };

    inline Reciprocal RefinedReciprocal(float w, float one)
    {
        const float r = 1.0f / w;                       // vrefp
        const float e = -__builtin_fmaf(w, r, -one);    // vnmsubfp: -(w * r - 1)
        const float r2 = __builtin_fmaf(r, e, r);       // vmaddfp: r * e + r
        const float e2 = -__builtin_fmaf(w, r2, -one);  // vnmsubfp: -(w * r2 - 1)
        const float r3 = __builtin_fmaf(r2, e2, r2);    // vmaddfp: r2 * e2 + r2
        return Reciprocal{ (r2 == r2) ? r3 : r, e, e2 }; // vcmpeqfp r2, r2; vsel
    }

    inline void Splat(V& d, float value)
    {
        simde_mm_store_ps(d.f32, simde_mm_set1_ps(value));
    }

    bool Overlaps(uint32_t a, uint32_t aSize, uint32_t b, uint32_t bSize)
    {
        return uint64_t(a) < uint64_t(b) + bSize && uint64_t(b) < uint64_t(a) + aSize;
    }

    // ---------------------------------------------------------------- verification
    std::atomic<uint32_t> g_mismatches{ 0 };
    std::atomic<uint64_t> g_verifiedCalls{ 0 };

    void ReportMismatch(const char* function, const char* what)
    {
        const uint32_t count = g_mismatches.fetch_add(1, std::memory_order_relaxed);
        if (count < 32)
        {
            char line[192];
            const int size = snprintf(line, sizeof(line), "[native vector] MISMATCH %s: %s (%u so far, %llu calls verified)\n",
                function, what, count + 1, (unsigned long long)g_verifiedCalls.load(std::memory_order_relaxed));
            os::switch_cpu_profiler::WriteLog(line, size_t(size));
        }
    }

    // The recompiled function on the real context and memory, the native one on a copy of the context first: the native
    // one's guest stores (the `stores` lines, 16 bytes each, plus `words`, 4 bytes each) are compared, then undone so
    // that the recompiled one reads the same memory, and the two contexts are compared on the registers the function
    // writes. The recompiled code's results are the ones that stay.
    template<typename Native, typename Recompiled>
    void Verify(const char* name, PPCContext& ctx, uint8_t* base, Native native, Recompiled recompiled,
        const uint32_t* lines, size_t lineCount, const uint32_t* words, size_t wordCount)
    {
        uint8_t before[8][16];
        uint8_t beforeWords[4][4];
        uint8_t nativeLines[8][16];
        uint8_t nativeWords[4][4];
        for (size_t i = 0; i < lineCount; i++)
            memcpy(before[i], base + (lines[i] & ~0xFu), 16);
        for (size_t i = 0; i < wordCount; i++)
            memcpy(beforeWords[i], base + words[i], 4);

        PPCContext nativeCtx = ctx;
        native(nativeCtx, base);
        for (size_t i = 0; i < lineCount; i++)
            memcpy(nativeLines[i], base + (lines[i] & ~0xFu), 16);
        for (size_t i = 0; i < wordCount; i++)
            memcpy(nativeWords[i], base + words[i], 4);
        for (size_t i = lineCount; i-- > 0;)
            memcpy(base + (lines[i] & ~0xFu), before[i], 16);
        for (size_t i = wordCount; i-- > 0;)
            memcpy(base + words[i], beforeWords[i], 4);

        recompiled(ctx, base);
        g_verifiedCalls.fetch_add(1, std::memory_order_relaxed);

        for (size_t i = 0; i < lineCount; i++)
        {
            if (memcmp(nativeLines[i], base + (lines[i] & ~0xFu), 16) != 0)
                return ReportMismatch(name, "stored vector");
        }
        for (size_t i = 0; i < wordCount; i++)
        {
            if (memcmp(nativeWords[i], base + words[i], 4) != 0)
                return ReportMismatch(name, "stored word");
        }

        static_assert(offsetof(PPCContext, v127) - offsetof(PPCContext, v0) == 127 * sizeof(V));
        for (size_t i = 0; i < 128; i++)
        {
            if (memcmp(&nativeCtx.v0 + i, &ctx.v0 + i, sizeof(V)) != 0)
            {
                char what[32];
                snprintf(what, sizeof(what), "v%zu", i);
                return ReportMismatch(name, what);
            }
        }
        // The integer registers lie r3, r0, r1, r2, r4 ... r31 in the context.
        static constexpr uint8_t INTEGER_ORDER[32] = { 3, 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
            21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
        static_assert(offsetof(PPCContext, r31) - offsetof(PPCContext, r3) == 31 * sizeof(PPCRegister));
        for (size_t i = 0; i < 32; i++)
        {
            if ((&nativeCtx.r3 + i)->u64 != (&ctx.r3 + i)->u64)
            {
                char what[32];
                snprintf(what, sizeof(what), "r%u", unsigned(INTEGER_ORDER[i]));
                return ReportMismatch(name, what);
            }
        }
        if (nativeCtx.fpscr.csr != ctx.fpscr.csr)
            return ReportMismatch(name, "fpscr");
    }

    // ---------------------------------------------------------------- 4x4 matrix product
    // sub_82168C48 copies r4's 64 bytes to r1-128 and r5's to r1-64; sub_82272648 the other way round. `first` is the
    // one at r1-128. Rows of the copy at r1-128 are read as A0..A3, of the one at r1-64 as B0..B3.
    void MatrixProduct(PPCContext& ctx, uint8_t* base, uint32_t first, uint32_t second)
    {
        const uint8_t* a = base + first;
        const uint8_t* b = base + second;
        V v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v26, v27, v28, v29, v30, v31;

        const uint32_t r3 = ctx.r3.u32;
        const uint64_t r9 = ctx.r3.s64 + 48;

        LoadRow(v0, a + 32);   // lvx128 v0,r0,r11 (r1-96)
        LoadRow(v11, a + 0);   // lvx128 v11 (r1-128)
        Mrghw(v13, v11, v0);
        Mrglw(v11, v11, v0);
        LoadRow(v9, a + 48);   // r1-80
        LoadRow(v8, a + 16);   // r1-112
        Mrghw(v12, v8, v9);
        Mrglw(v7, v8, v9);
        LoadRow(v10, b + 0);   // r1-64
        Mrghw(v0, v13, v12);
        Mrglw(v13, v13, v12);
        Mrghw(v12, v11, v7);
        Mrglw(v11, v11, v7);
        LoadRow(v9, b + 16);   // r1-48
        ctx.fpscr.enableFlushMode();
        Msum4(v7, v10, v0);
        Msum4(v3, v10, v12);
        Msum4(v2, v10, v11);
        Msum4(v6, v10, v13);
        LoadRow(v10, b + 48);  // lvx128 v10,r0,r10 (r1-16)
        Msum4(v5, v9, v0);
        LoadRow(v8, b + 32);   // r1-32
        Msum4(v4, v9, v13);
        Msum4(v1, v9, v12);
        Msum4(v9, v9, v11);
        Msum4(v30, v8, v12);
        Msum4(v28, v10, v0);
        Msum4(v12, v10, v12);
        Msum4(v31, v8, v0);
        Msum4(v29, v8, v13);
        Msum4(v27, v10, v13);
        Msum4(v26, v10, v11);
        Msum4(v8, v8, v11);
        Mrghw(v0, v7, v3);
        Mrghw(v13, v6, v2);
        Mrghw(v11, v5, v1);
        Mrghw(v10, v4, v9);
        Mrghw(v0, v0, v13);
        Mrghw(v13, v28, v12);
        Mrghw(v12, v11, v10);
        Mrghw(v9, v31, v30);
        Mrghw(v11, v27, v26);
        Mrghw(v8, v29, v8);
        Stvx(base, r3, v0);                     // stvx v0,r0,r3
        Mrghw(v0, v13, v11);
        Stvx(base, uint32_t(ctx.r3.s64 + 16), v12); // stvx v12,r0,r11
        Mrghw(v10, v9, v8);
        Stvx(base, uint32_t(r9), v0);           // stvx v0,r0,r9
        Stvx(base, uint32_t(ctx.r3.s64 + 32), v10); // stvx v10,r0,r10

        ctx.v0 = v0; ctx.v1 = v1; ctx.v2 = v2; ctx.v3 = v3; ctx.v4 = v4; ctx.v5 = v5; ctx.v6 = v6; ctx.v7 = v7;
        ctx.v8 = v8; ctx.v9 = v9; ctx.v10 = v10; ctx.v11 = v11; ctx.v12 = v12; ctx.v13 = v13;
        ctx.v26 = v26; ctx.v27 = v27; ctx.v28 = v28; ctx.v29 = v29; ctx.v30 = v30; ctx.v31 = v31;
        ctx.r9.u64 = r9;
        ctx.r10.s64 = ctx.r3.s64 + 32;
        ctx.r11.s64 = ctx.r3.s64 + 16;
    }

    // The stack copies at r1-128..r1-1 must be 16-byte lines of their own (r1 aligned), and neither source may lie in
    // them (the second copy would read the first).
    bool MatrixProductExact(const PPCContext& ctx, uint32_t first, uint32_t second)
    {
        const uint32_t r1 = ctx.r1.u32;
        return (r1 & 0xF) == 0 && r1 >= 0x1000 && !Overlaps(first, 64, r1 - 128, 128) && !Overlaps(second, 64, r1 - 128, 128);
    }

    void MatrixProductHook(PPCContext& ctx, uint8_t* base, bool swapped, void (*recompiled)(PPCContext&, uint8_t*), const char* name)
    {
        const uint32_t first = swapped ? ctx.r5.u32 : ctx.r4.u32;
        const uint32_t second = swapped ? ctx.r4.u32 : ctx.r5.u32;
        if (!g_switchNativeVectorMath || !MatrixProductExact(ctx, first, second))
        {
            recompiled(ctx, base);
            return;
        }

        auto native = [first, second](PPCContext& c, uint8_t* b) { MatrixProduct(c, b, first, second); };

        if (g_switchVerifyNativeVectorMath)
        {
            const uint32_t r3 = ctx.r3.u32;
            const uint32_t lines[4] = { r3, uint32_t(ctx.r3.s64 + 16), uint32_t(ctx.r3.s64 + 48), uint32_t(ctx.r3.s64 + 32) };
            Verify(name, ctx, base, native, recompiled, lines, 4, nullptr, 0);
            return;
        }

        native(ctx, base);
    }

    // ---------------------------------------------------------------- box corners to a bound
    // r3: the two result vectors (minimum, then maximum), r4: the 4x4 matrix (copied to r1-64), r5: eight corners, 16
    // bytes apart. Each corner is transformed (row3 + z row2 + y row1 + x row0), divided by its w (a reciprocal refined
    // once, as the guest does) and folded into the minimum and maximum.
    void ProjectBox(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t r1 = ctx.r1.u32;
        const uint32_t r3 = ctx.r3.u32;
        const uint32_t r4 = ctx.r4.u32;
        const uint32_t r5 = ctx.r5.u32;
        const uint8_t* m = base + r4;
        V v0, v5, v6, v7, v8, v9, v10, v11, v12, v13;

        // stw r3,20(r1); stw r5,36(r1); stw r11,28(r1) (r11 = r4): the caller's frame, kept.
        PPC_STORE_U32(r1 + 20, r3);
        PPC_STORE_U32(r1 + 36, r5);
        PPC_STORE_U32(r1 + 28, r4);

        // perf7: the eight corners as eight independent chains, then the fold in the guest's order. Every corner gets the
        // guest's operations on the guest's operands (the first corner's vmaddfp v9,v8,v13,v9 / v12,v12,v7,v9 /
        // v9,v0,v6,v12 and the loop's v13,v7,v13,v9 / v13,v12,v6,v13 / v9,v0,v5,v13 are the same three fused operations:
        // row2 z + row3, y row1 + that, x row0 + that; each one fused as before), its reciprocal chain on its own w with
        // the same constant 1.0, and the same multiply; the minimum and maximum are folded corner by corner in the same
        // order with the same operand order. The guest reads the matrix rows again from its stack copy for every
        // corner: the same 64 bytes, which nothing in the loop writes, so they are read once here; and the FPSCR's flush
        // mode is set once (the guest's later vmaddfp found it set). What the guest leaves in its registers is rebuilt
        // from the last corner and the last fold below.
        V row0, row1, row2, row3;
        LoadRow(row3, m + 48);                          // lvx128 v9 (r1-16)
        LoadRow(row2, m + 32);                          // lvx128 v8 / v7 (r1-32)
        LoadRow(row1, m + 16);                          // lvx128 v7 / v6 (r1-48)
        LoadRow(row0, m + 0);                           // lvx128 v6 / v5 (r1-64)
        simde_mm_store_si128((simde__m128i*)v10.u32, simde_mm_set1_epi32(int(0x0)));  // vspltisw v10,0
        UnpackD3dZero(v11, v10);                        // vupkd3d128 v11,v10,4
        Spltw<0x0>(v8, v11);                            // vspltw v13,v11,3 (first corner) / vspltw v8,v0,3: 1.0 splat

        PPCFPSCRRegister fpscr = ctx.fpscr;
        fpscr.enableFlushMode();

        V transformed[8];                               // each corner's v9 before the division
        V projected[8];                                 // each corner's vmulfp128 result
        Reciprocal reciprocal[8];
        for (uint32_t r8 = 0; r8 < 8; r8++)
        {
            V corner, x, y, z, sum;
            Lvx(corner, base, (r8 << 4) + r5);          // lvx128 v0,r0,r5 / v0,r11,r10
            Spltw<0x55>(z, corner);                     // vspltw v13,v0,2
            Spltw<0xAA>(y, corner);                     // vspltw v12,v0,1
            Spltw<0xFF>(x, corner);                     // vspltw v0,v0,0
            Maddfp(sum, row2, z, row3);                 // vmaddfp v9,v8,v13,v9 / v13,v7,v13,v9
            Maddfp(sum, y, row1, sum);                  // vmaddfp v12,v12,v7,v9 / v13,v12,v6,v13
            Maddfp(transformed[r8], x, row0, sum);      // vmaddfp v9,v0,v6,v12 / v9,v0,v5,v13
        }
        for (uint32_t r8 = 0; r8 < 8; r8++)
        {
            reciprocal[r8] = RefinedReciprocal(transformed[r8].f32[0], v8.f32[0]);
            simde_mm_store_ps(projected[r8].f32, simde_mm_mul_ps(simde_mm_load_ps(transformed[r8].f32),
                simde_mm_set1_ps(reciprocal[r8].value)));  // vmulfp128 v0,v9,v11 / v0,v9,v12
        }

        V minimum = projected[0];                       // stvx v0 (r1-112), lvx128 v0 (r1-112)
        V maximum = projected[0];                       // stvx v0 (r1-96)
        for (uint32_t r8 = 1; r8 < 8; r8++)
        {
            v0 = minimum;                               // lvx128 v0 (r1-112)
            v12 = maximum;                              // lvx128 v12 (r1-96)
            v13 = projected[r8];                        // lvx128 v13 (r1-80)
            simde_mm_store_ps(v0.f32, simde_mm_min_ps(simde_mm_load_ps(v0.f32), simde_mm_load_ps(v13.f32)));   // vminfp v0,v0,v13
            simde_mm_store_ps(v13.f32, simde_mm_max_ps(simde_mm_load_ps(v12.f32), simde_mm_load_ps(v13.f32))); // vmaxfp v13,v12,v13
            minimum = v0;                               // stvx v0 (r1-112)
            maximum = v13;                              // stvx v13 (r1-96)
        }

        // lwz r3,20(r1); addi r11,r3,16; stvx v0,r0,r3; stvx v13,r0,r11
        ctx.r3.u64 = r3;
        ctx.r11.s64 = ctx.r3.s64 + 16;
        Stvx(base, r3, v0);
        Stvx(base, ctx.r11.u32, v13);

        // What the guest's last pass (r8 = 7) leaves: v9 its corner before the division, v5-v7 the rows it read, v8 the
        // 1.0 splat, v11 and v10 its refinement terms splatted; v0, v12 and v13 from the last fold above.
        const Reciprocal& last = reciprocal[7];
        v9 = transformed[7];
        v5 = row0;
        v6 = row1;
        v7 = row2;
        Splat(v11, last.e);
        Splat(v10, last.e2);
        ctx.fpscr = fpscr;
        ctx.v0 = v0; ctx.v5 = v5; ctx.v6 = v6; ctx.v7 = v7; ctx.v8 = v8; ctx.v9 = v9; ctx.v10 = v10; ctx.v11 = v11;
        ctx.v12 = v12; ctx.v13 = v13;
        ctx.r8.u64 = 8;
        ctx.r9.u64 = PPC_LOAD_U64(r4 + 56);             // the last doubleword of the last copy
        ctx.r10.u64 = r5;                               // lwz r10,36(r1)
    }

    // r1 aligned (the stack slots are whole 16-byte lines), the caller's slots at r1+20..r1+39 and the copies below r1
    // outside the matrix and the corners (written before they are read).
    bool ProjectBoxExact(const PPCContext& ctx)
    {
        const uint32_t r1 = ctx.r1.u32;
        if ((r1 & 0xF) != 0 || r1 < 0x1000)
            return false;

        const uint32_t cornersStart = ctx.r5.u32 & ~0xFu;
        return !Overlaps(ctx.r4.u32, 64, r1 - 128, 168) && !Overlaps(cornersStart, 128 + 16, r1 - 128, 168);
    }
}

PPC_FUNC(sub_82168C48)
{
    MatrixProductHook(ctx, base, false, __imp__sub_82168C48, "sub_82168C48");
}

PPC_FUNC(sub_82272648)
{
    MatrixProductHook(ctx, base, true, __imp__sub_82272648, "sub_82272648");
}

PPC_FUNC(sub_825A1490)
{
    if (!g_switchNativeVectorMath || !ProjectBoxExact(ctx))
    {
        __imp__sub_825A1490(ctx, base);
        return;
    }

    if (g_switchVerifyNativeVectorMath)
    {
        const uint32_t r1 = ctx.r1.u32;
        const uint32_t lines[2] = { ctx.r3.u32, uint32_t(uint64_t(ctx.r3.u32) + 16) };
        const uint32_t words[3] = { r1 + 20, r1 + 28, r1 + 36 };
        Verify("sub_825A1490", ctx, base, ProjectBox, __imp__sub_825A1490, lines, 2, words, 3);
        return;
    }

    ProjectBox(ctx, base);
}

#endif
