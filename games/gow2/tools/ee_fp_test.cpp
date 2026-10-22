// Unit test of the PS2 EE floating point rules (ps2xRuntime/include/ps2_ee_fp.h), for the default rounding (add
// -DPS2X_EE_FP_ROUND=0 to test round-to-nearest). Header-only:
//   g++ -O1 -msse4.1 -I ps2xRuntime/include games/gow2/tools/ee_fp_test.cpp -o /tmp/ee_fp_test && /tmp/ee_fp_test
#include <immintrin.h>
#include <cstdio>
#include "ps2_ee_fp.h"

static int fails = 0, total = 0;
static void expect(const char *what, float got, uint32_t wantBits)
{
    ++total;
    const uint32_t g = ps2x_fp::bitsOf(got);
    if (g != wantBits) { ++fails; std::printf("FAIL %s: got %08x want %08x\n", what, g, wantBits); }
}
static void expectInt(const char *what, int32_t got, int32_t want)
{
    ++total;
    if (got != want) { ++fails; std::printf("FAIL %s: got %d want %d\n", what, got, want); }
}
int main()
{
    using namespace ps2x_fp;
    const float inf = floatOf(0x7F800000u), ninf = floatOf(0xFF800000u), nan = floatOf(0xFFC00000u);
    const float fmax = floatOf(0x7F7FFFFFu), nfmax = floatOf(0xFF7FFFFFu), den = floatOf(0x00000001u);
    expect("sqrt(-4)=2 (abs)", sqrt_(-4.0f), 0x40000000u);
    expect("sqrt(+4)=2", sqrt_(4.0f), 0x40000000u);
    expect("sqrt(0)=0", sqrt_(0.0f), 0u);
    expect("sqrt(-0)=-0", sqrt_(floatOf(0x80000000u)), 0x80000000u);
    expect("sqrt(denormal)=0", sqrt_(den), 0u);
    expect("sqrt(NaN input) finite", sqrt_(nan), bitsOf(std::sqrt(fmax)));
    expect("1/0=+Fmax", div(1.0f, 0.0f), 0x7F7FFFFFu);
    expect("-1/0=-Fmax", div(-1.0f, 0.0f), 0xFF7FFFFFu);
    expect("1/-0=-Fmax", div(1.0f, floatOf(0x80000000u)), 0xFF7FFFFFu);
    expect("0/0=+Fmax (no NaN)", div(0.0f, 0.0f), 0x7F7FFFFFu);
    expect("1/denormal=+Fmax", div(1.0f, den), 0x7F7FFFFFu);
    expect("6/3=2", div(6.0f, 3.0f), 0x40000000u);
    expect("Fmax*2 -> Fmax", mul(fmax, 2.0f), 0x7F7FFFFFu);
    expect("-Fmax*2 -> -Fmax", mul(nfmax, 2.0f), 0xFF7FFFFFu);
    expect("Fmax+Fmax -> Fmax", add(fmax, fmax), 0x7F7FFFFFu);
    expect("inf+(-inf) = 0 (not NaN)", add(inf, ninf), 0u);
    expect("0*inf = 0 (not NaN)", mul(0.0f, inf), 0u);
    expect("inf-inf = 0", sub(inf, inf), 0u);
    // -Fmax + 1 is -Fmax to nearest, and one ulp less in magnitude toward zero.
    expect("NaN(neg sign)+1 -> -Fmax+1", add(nan, 1.0f), PS2X_EE_FP_ROUND >= 2 ? 0xFF7FFFFEu : 0xFF7FFFFFu);
    expect("denormal flush in add", add(den, 0.0f), 0u);
    expect("denormal result flushed", mul(floatOf(0x00800000u), 0.5f), 0u);
    expect("1.5+2.25", add(1.5f, 2.25f), bitsOf(3.75f));
    expect("rsqrt(4)=0.5", rsqrt(4.0f), bitsOf(0.5f));
    expect("rsqrt(0)=+Fmax", rsqrt(0.0f), 0x7F7FFFFFu);
    // RSQRT.S fd, fs, ft = fs / sqrt(|ft|) (PCSX2 FPU.cpp RSQRT_S); ft zero -> +-Fmax with the sign of ft.
    expect("RSQRT.S 2/sqrt(4)=1", rsqrt(2.0f, 4.0f), 0x3F800000u);
    expect("RSQRT.S -3/sqrt(9)=-1", rsqrt(-3.0f, 9.0f), 0xBF800000u);
    expect("RSQRT.S 1/sqrt(-4)=0.5 (abs)", rsqrt(1.0f, -4.0f), 0x3F000000u);
    expect("RSQRT.S 3/sqrt(2): root rounded to float first", rsqrt(3.0f, 2.0f), 0x4007C3B7u);
    expect("RSQRT.S -3/+0=+Fmax (sign of ft)", rsqrt(-3.0f, 0.0f), 0x7F7FFFFFu);
    expect("RSQRT.S 3/-0=-Fmax (sign of ft)", rsqrt(3.0f, floatOf(0x80000000u)), 0xFF7FFFFFu);
    expect("RSQRT.S 1/denormal=+Fmax", rsqrt(1.0f, den), 0x7F7FFFFFu);
    expect("RSQRT.S 0/sqrt(4)=0", rsqrt(0.0f, 4.0f), 0u);
    expect("RSQRT.S denormal/sqrt(4)=0", rsqrt(den, 4.0f), 0u);
    expect("RSQRT.S overflow -> Fmax", rsqrt(fmax, floatOf(0x00800000u)), 0x7F7FFFFFu);
    expect("RSQRT.S denormal result flushed", rsqrt(floatOf(0x00800000u), 4.0f), 0u);
    expect("RSQRT.S NaN(neg) numerator -> -Fmax/2", rsqrt(nan, 4.0f), 0xFEFFFFFFu);
    alignas(16) float va[4] = {1.0f, inf, nan, den}, vb[4] = {2.0f, ninf, 1.0f, 3.0f}, vr[4];
    _mm_store_ps(vr, vmul(_mm_load_ps(va), _mm_load_ps(vb)));
    expect("vmul lane0 1*2", vr[0], bitsOf(2.0f));
    expect("vmul lane1 inf*-inf -> -Fmax", vr[1], 0xFF7FFFFFu);
    expect("vmul lane2 NaN(neg)*1 -> -Fmax", vr[2], 0xFF7FFFFFu);
    expect("vmul lane3 denormal*3 -> 0", vr[3], 0u);
    float big[4] = {floatOf(0x6c018249u), 1.0f, 1.0f, 1.0f};
    _mm_store_ps(vr, vmul(_mm_loadu_ps(big), _mm_loadu_ps(big)));
    expect("vmul overflow (seen in game) -> Fmax", vr[0], 0x7F7FFFFFu);
    _mm_store_ps(vr, vadd(_mm_load_ps(va), _mm_load_ps(vb)));
    expect("vadd lane1 inf+-inf = 0", vr[1], 0u);
#if PS2X_EE_FP_ROUND == 2
    // Toward-zero rounding (PCSX2 FPU.Roundmode = 3): each case gives another result when rounded to nearest.
    const float q = floatOf(0x33C00000u), r = floatOf(0x33A00000u); // 0.75 and 1.25 ulp of 1.0 below/above
    expect("1 + 0.75ulp -> 1 (nearest: 1+ulp)", add(1.0f, q), 0x3F800000u);
    expect("-1 - 0.75ulp -> -1", add(-1.0f, -q), 0xBF800000u);
    expect("1 - 1.25ulp(below 1) -> 1-2ulp (nearest: 1-ulp)", sub(1.0f, r), 0x3F7FFFFEu);
    expect("1.5 * (1+ulp) -> 1.5+ulp (nearest: 1.5+2ulp)", mul(1.5f, floatOf(0x3F800001u)), 0x3FC00001u);
    expect("-1.5 * (1+ulp) -> -(1.5+ulp)", mul(-1.5f, floatOf(0x3F800001u)), 0xBFC00001u);
    expectInt("CVT.W.S 2.7 -> 2", cvtWS(2.7f), 2);
    expectInt("CVT.W.S -2.7 -> -2", cvtWS(-2.7f), -2);
    expectInt("CVT.W.S 3.5 -> 3 (nearest: 4)", cvtWS(3.5f), 3);
    expectInt("CVT.W.S -3.5 -> -3", cvtWS(-3.5f), -3);
    expectInt("CVT.W.S 0.99 -> 0", cvtWS(0.99f), 0);
    expectInt("CVT.W.S 1.5*2^30 -> exact", cvtWS(floatOf(0x4EC00000u)), 1610612736);
    expectInt("CVT.W.S 2^31 -> INT32_MAX", cvtWS(floatOf(0x4F000000u)), INT32_MAX);
    expectInt("CVT.W.S -3e9 -> INT32_MIN", cvtWS(-3e9f), INT32_MIN);
    expect("CVT.S.W 2^24+3 -> 2^24+2 (nearest: 2^24+4)", cvtSW(16777219), 0x4B800001u);
    expect("CVT.S.W -(2^24+3) -> -(2^24+2)", cvtSW(-16777219), 0xCB800001u);
    alignas(16) float ta[4] = {1.0f, 1.5f, -1.0f, 2.0f}, tb[4] = {q, 1.0f, -q, 2.0f};
    _mm_store_ps(vr, vadd(_mm_load_ps(ta), _mm_load_ps(tb)));
    expect("vadd lane0 1 + 0.75ulp -> 1", vr[0], 0x3F800000u);
    expect("vadd lane2 -1 - 0.75ulp -> -1", vr[2], 0xBF800000u);
    tb[1] = floatOf(0x3F800001u);
    _mm_store_ps(vr, vmul(_mm_load_ps(ta), _mm_load_ps(tb)));
    expect("vmul lane1 1.5 * (1+ulp) -> 1.5+ulp", vr[1], 0x3FC00001u);
#endif
    std::printf("%d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
