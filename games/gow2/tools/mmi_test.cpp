// Checks the R5900 MMI shuffle macros against the instruction definitions (word/halfword lane by lane).
//   g++ -O1 -msse4.1 -I ps2xRuntime/include games/gow2/tools/mmi_test.cpp -o /tmp/mmi_test
#include <emmintrin.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define PS2_EXTRACT_EPI32_0(x) 0
#define PS2_EXTRACT_EPI64_0(x) 0
#define PS2_EXTRACT_EPI64_1(x) 0
// Only the shuffle macros are needed; copy them from the header to keep the test self contained.
#include "mmi_macros_extract.inc"

static int fails = 0, total = 0;
static void check(const char *name, __m128i got, const uint16_t exp[8])
{
    uint16_t g[8];
    std::memcpy(g, &got, 16);
    ++total;
    if (std::memcmp(g, exp, 16))
    {
        ++fails;
        std::printf("FAIL %s: got", name);
        for (int i = 0; i < 8; ++i) std::printf(" %04x", g[i]);
        std::printf("  expected");
        for (int i = 0; i < 8; ++i) std::printf(" %04x", exp[i]);
        std::printf("\n");
    }
}
int main()
{
    uint16_t in[8] = {0x1000, 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777};
    __m128i x;
    std::memcpy(&x, in, 16);
    // word lanes w0=(h1:h0) w1=(h3:h2) w2=(h5:h4) w3=(h7:h6)
    auto W = [&](int a, int b, int c, int d, uint16_t out[8]) {
        const int w[4] = {a, b, c, d};
        for (int i = 0; i < 4; ++i) { out[2 * i] = in[2 * w[i]]; out[2 * i + 1] = in[2 * w[i] + 1]; }
    };
    uint16_t e[8];
    W(2, 1, 0, 3, e); check("PEXEW {w2,w1,w0,w3}", PS2_PEXEW(x), e);
    W(0, 2, 1, 3, e); check("PEXCW {w0,w2,w1,w3}", PS2_PEXCW(x), e);
    W(1, 2, 0, 3, e); check("PROT3W {w1,w2,w0,w3}", PS2_PROT3W(x), e);
    { const int h[8] = {2, 1, 0, 3, 6, 5, 4, 7}; for (int i = 0; i < 8; ++i) e[i] = in[h[i]]; check("PEXEH", PS2_PEXEH(x), e); }
    { const int h[8] = {0, 2, 1, 3, 4, 6, 5, 7}; for (int i = 0; i < 8; ++i) e[i] = in[h[i]]; check("PEXCH", PS2_PEXCH(x), e); }
    { const int h[8] = {3, 2, 1, 0, 7, 6, 5, 4}; for (int i = 0; i < 8; ++i) e[i] = in[h[i]]; check("PREVH", PS2_PREVH(x), e); }
    std::printf("%d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
