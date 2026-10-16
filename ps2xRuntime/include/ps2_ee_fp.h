#ifndef PS2_EE_FP_H
#define PS2_EE_FP_H
// PS2 EE floating point rules for the recompiled code (FPU/COP1 and VU0 macro mode): no NaN/Inf, no denormals.
//   * operands: denormal -> signed zero, Inf/NaN -> +-Fmax (largest finite float)
//   * results:  Inf -> +-Fmax (overflow), denormal -> signed zero (underflow)
//   * SQRT.S of a negative number uses its absolute value; DIV by zero gives +-Fmax (sign = xor of the signs)
// Flags (FCR31 O/U/I/D and the sticky ones, VU0 MAC/status) are not tracked yet. Rounding stays round-to-nearest
// (the EE rounds toward zero): differences are in the last bit only.
// Rules cross-checked against PCSX2's FPU.cpp behaviour (read to understand, reimplemented here).
#include <cmath>
#include <cstdint>
#include <cstring>

#define PS2X_FORCE_INLINE inline __attribute__((always_inline))

extern "C" void ps2x_fp_report(const char *kind, const float *a, const float *b, const float *r, int lanes);

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

    PS2X_FORCE_INLINE float add(float a, float b) { return norm(norm(a) + norm(b)); }
    PS2X_FORCE_INLINE float sub(float a, float b) { return norm(norm(a) - norm(b)); }
    PS2X_FORCE_INLINE float mul(float a, float b) { return norm(norm(a) * norm(b)); }
    PS2X_FORCE_INLINE float div(float a, float b)
    {
        const uint32_t bb = bitsOf(b);
        if (((bb >> 23) & 0xFFu) == 0u) // divisor zero/denormal (also 0/0): +-Fmax
            return floatOf(((bitsOf(a) ^ bb) & 0x80000000u) | 0x7F7FFFFFu);
        return norm(norm(a) / norm(b));
    }
    PS2X_FORCE_INLINE float sqrt_(float a)
    {
        const uint32_t ab = bitsOf(a);
        if (((ab >> 23) & 0xFFu) == 0u)
            return floatOf(ab & 0x80000000u);
        return std::sqrt(std::fabs(norm(a)));
    }
    PS2X_FORCE_INLINE float rsqrt(float a)
    {
        const uint32_t ab = bitsOf(a);
        if (((ab >> 23) & 0xFFu) == 0u)
            return floatOf((ab & 0x80000000u) | 0x7F7FFFFFu);
        return norm(1.0f / std::sqrt(std::fabs(norm(a))));
    }
    PS2X_FORCE_INLINE __m128 vadd(__m128 a, __m128 b) { return norm4(_mm_add_ps(norm4(a), norm4(b))); }
    PS2X_FORCE_INLINE __m128 vsub(__m128 a, __m128 b) { return norm4(_mm_sub_ps(norm4(a), norm4(b))); }
    PS2X_FORCE_INLINE __m128 vmul(__m128 a, __m128 b) { return norm4(_mm_mul_ps(norm4(a), norm4(b))); }
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
