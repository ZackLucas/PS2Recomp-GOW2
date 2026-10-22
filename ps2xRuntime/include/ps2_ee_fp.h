#ifndef PS2_EE_FP_H
#define PS2_EE_FP_H
// PS2 EE floating point rules for the recompiled code (FPU/COP1 and VU0 macro mode): no NaN/Inf, no denormals.
//   * operands: denormal -> signed zero, Inf/NaN -> +-Fmax (largest finite float)
//   * results:  Inf -> +-Fmax (overflow), denormal -> signed zero (underflow)
//   * SQRT.S of a negative number uses its absolute value; DIV by zero gives +-Fmax (sign = xor of the signs)
//   * RSQRT.S is fs / sqrt(|ft|); ft zero gives +-Fmax with the sign of ft (PCSX2's FPU.cpp; its x86 recompiler takes
//     the sign of fs instead, which only differs when ft is +-0 and the signs differ)
// Flags (FCR31 O/U/I/D and the sticky ones, VU0 MAC/status) are not tracked yet.
// Rounding (compile-time PS2X_EE_FP_ROUND): 2 (default) rounds ADD/SUB/MUL and CVT.S.W toward zero and truncates
// CVT.W.S, like PCSX2 with its default FPU.Roundmode = 3 (Chop); DIV and SQRT stay round-to-nearest (PCSX2's
// FPUDiv.Roundmode = 0). 0 rounds ADD/SUB/MUL and CVT.W.S to nearest (the host default); 1 does the same but counts
// the results that toward-zero rounding would change (ps2x_fp_round_stats, printed with PS2_PERF_STATS).
// Rules cross-checked against PCSX2's FPU.cpp behaviour (read to understand, reimplemented here).
#include <cmath>
#include <cstdint>
#include <cstring>

#define PS2X_FORCE_INLINE inline __attribute__((always_inline))

extern "C" void ps2x_fp_report(const char *kind, const float *a, const float *b, const float *r, int lanes);

#ifndef PS2X_EE_FP_ROUND
#define PS2X_EE_FP_ROUND 2
#endif
// [op count, results that differ] for: 0 add/sub, 1 mul, 2 CVT.W.S, 3 CVT.S.W (filled only when PS2X_EE_FP_ROUND == 1)
extern "C" uint64_t ps2x_fp_round_stats[4][2];
// PS2X_EE_FP_ROUND == 3 (measurement build): each operation class picks toward-zero (bit set) or nearest per run, from
// PS2X_FP_CLS (hex). bit0 FPU ADD/SUB, 1 FPU MUL, 2 CVT.S.W, 3 CVT.W.S (truncate), 4 VU0 macro ADD/SUB, 5 VU0 macro MUL,
// 6 DIV (and the division of RSQRT.S), 7 SQRT (and the root of RSQRT.S). Default 0x3F = the ROUND == 2 behavior.
extern "C" uint32_t ps2x_fp_cls;

namespace ps2x_fp
{
    PS2X_FORCE_INLINE uint32_t bitsOf(float v)
    {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        return b;
    }
    PS2X_FORCE_INLINE float floatOf(uint32_t b)
    {
        float v;
        std::memcpy(&v, &b, 4);
        return v;
    }
    // Operand/result clamp: exponent 0 -> signed zero, exponent 255 -> +-Fmax.
    PS2X_FORCE_INLINE float norm(float v)
    {
        uint32_t b = bitsOf(v);
        const uint32_t e = (b >> 23) & 0xFFu;
        if (e == 0u)
            b &= 0x80000000u;
        else if (e == 0xFFu)
            b = (b & 0x80000000u) | 0x7F7FFFFFu;
        return floatOf(b);
    }
    PS2X_FORCE_INLINE __m128 norm4(__m128 v)
    {
        const __m128i x = _mm_castps_si128(v);
        const __m128i sign = _mm_set1_epi32(static_cast<int>(0x80000000u));
        const __m128i e = _mm_and_si128(_mm_srli_epi32(x, 23), _mm_set1_epi32(0xFF));
        const __m128i isZero = _mm_cmpeq_epi32(e, _mm_setzero_si128());
        const __m128i isFF = _mm_cmpeq_epi32(e, _mm_set1_epi32(0xFF));
        __m128i r = _mm_or_si128(_mm_andnot_si128(isZero, x), _mm_and_si128(isZero, _mm_and_si128(x, sign)));
        const __m128i big = _mm_or_si128(_mm_and_si128(x, sign), _mm_set1_epi32(0x7F7FFFFF));
        r = _mm_or_si128(_mm_andnot_si128(isFF, r), _mm_and_si128(isFF, big));
        return _mm_castsi128_ps(r);
    }

    // Exact value hi + lo (|lo| below half an ulp of hi) rounded toward zero to float.
    PS2X_FORCE_INLINE float towardZero(double hi, double lo)
    {
        float r = static_cast<float>(hi);
        const double rd = static_cast<double>(r);
        if (std::fabs(rd) > std::fabs(hi) || (rd == hi && lo != 0.0 && ((lo < 0.0) != (hi < 0.0))))
            r = std::nextafter(r, 0.0f);
        return r;
    }
    // a + b as hi + lo (TwoSum): hi is the double rounding of the sum, lo the exact error.
    PS2X_FORCE_INLINE float addTowardZero(float a, float b)
    {
        const double x = a, y = b, hi = x + y, bv = hi - x;
        return towardZero(hi, (x - (hi - bv)) + (y - bv));
    }
    // The product of two floats is exact in double.
    PS2X_FORCE_INLINE float mulTowardZero(float a, float b) { return towardZero(static_cast<double>(a) * b, 0.0); }
    // a / b and sqrt(x) rounded toward zero (the float result of the nearest rounding is corrected with an exact FMA remainder).
    PS2X_FORCE_INLINE float divTowardZero(float a, float b)
    {
        float q = a / b;
        const float r = std::fma(-q, b, a);
        if (r != 0.0f && ((r < 0.0f) != (a < 0.0f)))
            q = std::nextafter(q, 0.0f);
        return q;
    }
    PS2X_FORCE_INLINE float sqrtTowardZero(float x)
    {
        float q = std::sqrt(x);
        if (std::fma(q, q, -x) > 0.0f)
            q = std::nextafter(q, 0.0f);
        return q;
    }

    PS2X_FORCE_INLINE void countRound(int kind, bool differs)
    {
        ++ps2x_fp_round_stats[kind][0];
        ps2x_fp_round_stats[kind][1] += differs ? 1u : 0u;
    }
    PS2X_FORCE_INLINE float addSel(float a, float b, bool toZero) { return toZero ? addTowardZero(a, b) : a + b; }
    PS2X_FORCE_INLINE float mulSel(float a, float b, bool toZero) { return toZero ? mulTowardZero(a, b) : a * b; }
    PS2X_FORCE_INLINE float addRounded(float a, float b)
    {
#if PS2X_EE_FP_ROUND == 3
        return addSel(a, b, (ps2x_fp_cls & 1u) != 0u);
#elif PS2X_EE_FP_ROUND == 2
        return addTowardZero(a, b);
#else
        const float r = a + b;
#if PS2X_EE_FP_ROUND == 1
        countRound(0, bitsOf(r) != bitsOf(addTowardZero(a, b)));
#endif
        return r;
#endif
    }
    PS2X_FORCE_INLINE float mulRounded(float a, float b)
    {
#if PS2X_EE_FP_ROUND == 3
        return mulSel(a, b, (ps2x_fp_cls & 2u) != 0u);
#elif PS2X_EE_FP_ROUND == 2
        return mulTowardZero(a, b);
#else
        const float r = a * b;
#if PS2X_EE_FP_ROUND == 1
        countRound(1, bitsOf(r) != bitsOf(mulTowardZero(a, b)));
#endif
        return r;
#endif
    }

    PS2X_FORCE_INLINE float add(float a, float b) { return norm(addRounded(norm(a), norm(b))); }
    PS2X_FORCE_INLINE float sub(float a, float b) { return norm(addRounded(norm(a), -norm(b))); }
    PS2X_FORCE_INLINE float mul(float a, float b) { return norm(mulRounded(norm(a), norm(b))); }
    // CVT.W.S: PCSX2 truncates ((s32) cast) and saturates from 2^30 up (exponent field above 0x4E8).
    PS2X_FORCE_INLINE int32_t cvtWS(float a)
    {
        const uint32_t ab = bitsOf(a);
        const int32_t trunc = (ab & 0x7F800000u) <= 0x4E800000u ? static_cast<int32_t>(a)
                                                                 : ((ab & 0x80000000u) ? INT32_MIN : INT32_MAX);
#if PS2X_EE_FP_ROUND == 3
        return (ps2x_fp_cls & 8u) ? trunc : static_cast<int32_t>(nearbyintf(a));
#elif PS2X_EE_FP_ROUND == 2
        return trunc;
#else
        const int32_t r = static_cast<int32_t>(nearbyintf(a));
#if PS2X_EE_FP_ROUND == 1
        countRound(2, r != trunc);
#endif
        return r;
#endif
    }
    PS2X_FORCE_INLINE float cvtSW(int32_t v)
    {
#if PS2X_EE_FP_ROUND == 3
        return (ps2x_fp_cls & 4u) ? towardZero(static_cast<double>(v), 0.0) : static_cast<float>(v);
#elif PS2X_EE_FP_ROUND == 2
        return towardZero(static_cast<double>(v), 0.0);
#else
        const float r = static_cast<float>(v);
#if PS2X_EE_FP_ROUND == 1
        countRound(3, bitsOf(r) != bitsOf(towardZero(static_cast<double>(v), 0.0)));
#endif
        return r;
#endif
    }
#if PS2X_EE_FP_ROUND == 3
    PS2X_FORCE_INLINE float divSel(float a, float b) { return (ps2x_fp_cls & 64u) ? divTowardZero(a, b) : a / b; }
    PS2X_FORCE_INLINE float sqrtSel(float x) { return (ps2x_fp_cls & 128u) ? sqrtTowardZero(x) : std::sqrt(x); }
#endif
    PS2X_FORCE_INLINE float div(float a, float b)
    {
        const uint32_t bb = bitsOf(b);
        if (((bb >> 23) & 0xFFu) == 0u) // divisor zero/denormal (also 0/0): +-Fmax
            return floatOf(((bitsOf(a) ^ bb) & 0x80000000u) | 0x7F7FFFFFu);
#if PS2X_EE_FP_ROUND == 3
        return norm(divSel(norm(a), norm(b)));
#else
        return norm(norm(a) / norm(b));
#endif
    }
    PS2X_FORCE_INLINE float sqrt_(float a)
    {
        const uint32_t ab = bitsOf(a);
        if (((ab >> 23) & 0xFFu) == 0u)
            return floatOf(ab & 0x80000000u);
#if PS2X_EE_FP_ROUND == 3
        return sqrtSel(std::fabs(norm(a)));
#else
        return std::sqrt(std::fabs(norm(a)));
#endif
    }
    // RSQRT.S: the root is rounded to float before the division, as in PCSX2 (sqrtss + divss).
    PS2X_FORCE_INLINE float rsqrt(float s, float t)
    {
        const uint32_t tb = bitsOf(t);
        if (((tb >> 23) & 0xFFu) == 0u)
            return floatOf((tb & 0x80000000u) | 0x7F7FFFFFu);
#if PS2X_EE_FP_ROUND == 3
        return norm(divSel(norm(s), sqrtSel(std::fabs(norm(t)))));
#else
        return norm(norm(s) / std::sqrt(std::fabs(norm(t))));
#endif
    }
    PS2X_FORCE_INLINE float rsqrt(float a) { return rsqrt(1.0f, a); }
#if PS2X_EE_FP_ROUND == 0
    PS2X_FORCE_INLINE __m128 vadd(__m128 a, __m128 b) { return norm4(_mm_add_ps(norm4(a), norm4(b))); }
    PS2X_FORCE_INLINE __m128 vsub(__m128 a, __m128 b) { return norm4(_mm_sub_ps(norm4(a), norm4(b))); }
    PS2X_FORCE_INLINE __m128 vmul(__m128 a, __m128 b) { return norm4(_mm_mul_ps(norm4(a), norm4(b))); }
#else
    // VU0 macro mode lane by lane, through the same rounding as the FPU (PCSX2's VU0.Roundmode = 3 is also Chop).
    PS2X_FORCE_INLINE __m128 lanes(__m128 a, __m128 b, float (*op)(float, float))
    {
        alignas(16) float fa[4], fb[4], fr[4];
        _mm_store_ps(fa, norm4(a));
        _mm_store_ps(fb, norm4(b));
        for (int i = 0; i < 4; ++i)
            fr[i] = op(fa[i], fb[i]);
        return norm4(_mm_load_ps(fr));
    }
#if PS2X_EE_FP_ROUND == 3
    PS2X_FORCE_INLINE float vaddOp(float a, float b) { return addSel(a, b, (ps2x_fp_cls & 16u) != 0u); }
    PS2X_FORCE_INLINE float vmulOp(float a, float b) { return mulSel(a, b, (ps2x_fp_cls & 32u) != 0u); }
    PS2X_FORCE_INLINE __m128 vadd(__m128 a, __m128 b) { return lanes(a, b, vaddOp); }
    PS2X_FORCE_INLINE __m128 vsub(__m128 a, __m128 b) { return lanes(a, _mm_xor_ps(b, _mm_set1_ps(-0.0f)), vaddOp); }
    PS2X_FORCE_INLINE __m128 vmul(__m128 a, __m128 b) { return lanes(a, b, vmulOp); }
#else
    PS2X_FORCE_INLINE __m128 vadd(__m128 a, __m128 b) { return lanes(a, b, addRounded); }
    PS2X_FORCE_INLINE __m128 vsub(__m128 a, __m128 b) { return lanes(a, _mm_xor_ps(b, _mm_set1_ps(-0.0f)), addRounded); }
    PS2X_FORCE_INLINE __m128 vmul(__m128 a, __m128 b) { return lanes(a, b, mulRounded); }
#endif
#endif
    PS2X_FORCE_INLINE __m128 vdiv(__m128 a, __m128 b)
    {
        alignas(16) float fa[4], fb[4], fr[4];
        _mm_store_ps(fa, a);
        _mm_store_ps(fb, b);
        for (int i = 0; i < 4; ++i)
            fr[i] = div(fa[i], fb[i]);
        return _mm_load_ps(fr);
    }
} // namespace ps2x_fp

#endif
