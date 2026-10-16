// Unit test of the PS2 EE floating point rules (ps2xRuntime/include/ps2_ee_fp.h). Header-only:
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
    expect("NaN(neg sign)+1 -> -Fmax+1 = -Fmax", add(nan, 1.0f), 0xFF7FFFFFu);
    expect("denormal flush in add", add(den, 0.0f), 0u);
    expect("denormal result flushed", mul(floatOf(0x00800000u), 0.5f), 0u);
    expect("1.5+2.25", add(1.5f, 2.25f), bitsOf(3.75f));
    expect("rsqrt(4)=0.5", rsqrt(4.0f), bitsOf(0.5f));
    expect("rsqrt(0)=+Fmax", rsqrt(0.0f), 0x7F7FFFFFu);
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
    std::printf("%d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
