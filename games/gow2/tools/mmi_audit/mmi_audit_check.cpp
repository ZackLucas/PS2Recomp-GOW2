// MMI audit, step 2: run the C++ the recompiler emits (mmi_gen.inc from mmi_audit_gen) against a scalar
// reference written from the EE Core Instruction Set Manual, on random + edge inputs, and compare
// rd, rs, rt, LO (128 bits), HI (128 bits) lane by lane.
//
// Runtime model: ctx->lo / ctx->hi are LO/HI bits 0-63, ctx->lo1 / ctx->hi1 bits 64-127; ctx->sa is in bits.
// Ops that write a 64-bit GPR keep bits 64-127 of rd (as PCSX2 does).
//
// build: see games/gow2/tools/mmi_audit/README.md
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include MMI_GEN_INC

union V
{
    uint32_t w[4];
    int32_t sw[4];
    uint16_t h[8];
    int16_t sh[8];
    uint8_t b[16];
    int8_t sb[16];
    uint64_t d[2];
    int64_t sd[2];
};

struct St
{
    V r[32];
    V lo, hi;
    uint32_t sa;
};

// Words of LO/HI that the manual leaves undefined for an op are not compared.
struct Mask
{
    bool lo[4] = {true, true, true, true};
    bool hi[4] = {true, true, true, true};
};

static uint64_t sx32(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }
static int32_t sat32(int64_t v) { return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v; }
static int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }
static int8_t sat8(int32_t v) { return v > 127 ? 127 : v < -128 ? -128 : (int8_t)v; }
static uint32_t clz32(uint32_t v) { return v ? __builtin_clz(v) : 32; }

// DIV semantics shared by DIV1 / PDIVW (PCSX2 = hardware): x/0 -> LO = (x<0)?1:-1, HI = x; INT_MIN/-1 -> LO=INT_MIN, HI=0
static void sdiv(int32_t n, int32_t d, uint32_t &q, uint32_t &r)
{
    if (d == 0) { q = n < 0 ? 1u : 0xFFFFFFFFu; r = (uint32_t)n; }
    else if (n == INT32_MIN && d == -1) { q = (uint32_t)INT32_MIN; r = 0; }
    else { q = (uint32_t)(n / d); r = (uint32_t)(n % d); }
}
static void udiv(uint32_t n, uint32_t d, uint32_t &q, uint32_t &r)
{
    if (d == 0) { q = 0xFFFFFFFFu; r = n; }
    else { q = n / d; r = n % d; }
}

// LO/HI 64-bit lane i (0 = bits 0-63, 1 = bits 64-127) from a 64-bit result: both halves sign-extended
static void setHiLo64(St &s, int lane, uint64_t res)
{
    s.lo.d[lane] = sx32((uint32_t)res);
    s.hi.d[lane] = sx32((uint32_t)(res >> 32));
}
static uint64_t acc64(const St &s, int lane) // {HI.w(2*lane), LO.w(2*lane)}
{
    return ((uint64_t)s.hi.w[lane * 2] << 32) | s.lo.w[lane * 2];
}

using RefFn = std::function<void(St &, Mask &)>;
struct Ref
{
    const char *name;
    RefFn fn;
};

#define RS s.r[2]
#define RT s.r[3]
#define RD s.r[4]

static std::vector<Ref> buildRefs()
{
    std::vector<Ref> v;
    auto add = [&](const char *n, RefFn f) { v.push_back({n, std::move(f)}); };
    // ---- MMI (top level)
    add("MADD", [](St &s, Mask &) { uint64_t r = acc64(s, 0) + (uint64_t)((int64_t)RS.sw[0] * RT.sw[0]); setHiLo64(s, 0, r); RD.d[0] = sx32((uint32_t)r); });
    add("MADDU", [](St &s, Mask &) { uint64_t r = acc64(s, 0) + (uint64_t)RS.w[0] * RT.w[0]; setHiLo64(s, 0, r); RD.d[0] = sx32((uint32_t)r); });
    add("PLZCW", [](St &s, Mask &) { V a = RS; for (int i = 0; i < 2; ++i) { uint32_t x = a.w[i]; if (x & 0x80000000u) x = ~x; RD.w[i] = clz32(x) - 1; } });
    add("MFHI1", [](St &s, Mask &) { RD.d[0] = s.hi.d[1]; });
    add("MTHI1", [](St &s, Mask &) { s.hi.d[1] = RS.d[0]; });
    add("MFLO1", [](St &s, Mask &) { RD.d[0] = s.lo.d[1]; });
    add("MTLO1", [](St &s, Mask &) { s.lo.d[1] = RS.d[0]; });
    add("MULT1", [](St &s, Mask &) { uint64_t r = (uint64_t)((int64_t)RS.sw[0] * RT.sw[0]); setHiLo64(s, 1, r); RD.d[0] = sx32((uint32_t)r); });
    add("MULTU1", [](St &s, Mask &) { uint64_t r = (uint64_t)RS.w[0] * RT.w[0]; setHiLo64(s, 1, r); RD.d[0] = sx32((uint32_t)r); });
    add("DIV1", [](St &s, Mask &) { uint32_t q, r; sdiv(RS.sw[0], RT.sw[0], q, r); s.lo.d[1] = sx32(q); s.hi.d[1] = sx32(r); });
    add("DIVU1", [](St &s, Mask &) { uint32_t q, r; udiv(RS.w[0], RT.w[0], q, r); s.lo.d[1] = sx32(q); s.hi.d[1] = sx32(r); });
    add("MADD1", [](St &s, Mask &) { uint64_t r = acc64(s, 1) + (uint64_t)((int64_t)RS.sw[0] * RT.sw[0]); setHiLo64(s, 1, r); RD.d[0] = sx32((uint32_t)r); });
    add("MADDU1", [](St &s, Mask &) { uint64_t r = acc64(s, 1) + (uint64_t)RS.w[0] * RT.w[0]; setHiLo64(s, 1, r); RD.d[0] = sx32((uint32_t)r); });
    // shifts: halfword ops use sa & 15, word ops sa & 31
    auto shH = [&](const char *n, int sa, int kind) {
        add(n, [sa, kind](St &s, Mask &) { V t = RT; for (int i = 0; i < 8; ++i) { int k = sa & 15; RD.h[i] = kind == 0 ? (uint16_t)(t.h[i] << k) : kind == 1 ? (uint16_t)(t.h[i] >> k) : (uint16_t)(t.sh[i] >> k); } });
    };
    auto shW = [&](const char *n, int sa, int kind) {
        add(n, [sa, kind](St &s, Mask &) { V t = RT; for (int i = 0; i < 4; ++i) { int k = sa & 31; RD.w[i] = kind == 0 ? t.w[i] << k : kind == 1 ? t.w[i] >> k : (uint32_t)(t.sw[i] >> k); } });
    };
    shH("PSLLH_5", 5, 0); shH("PSRLH_5", 5, 1); shH("PSRAH_5", 5, 2);
    shH("PSLLH_20", 20, 0); shH("PSRLH_20", 20, 1); shH("PSRAH_20", 20, 2);
    shW("PSLLW_7", 7, 0); shW("PSRLW_7", 7, 1); shW("PSRAW_7", 7, 2);
    shW("PSLLW_31", 31, 0); shW("PSRLW_31", 31, 1); shW("PSRAW_31", 31, 2);

    // ---- MMI0
#define LANES(n, cnt, expr) add(#n, [](St &s, Mask &) { V a = RS, b = RT; V o{}; for (int i = 0; i < cnt; ++i) { expr; } RD = o; })
    LANES(PADDW, 4, o.w[i] = a.w[i] + b.w[i]);
    LANES(PSUBW, 4, o.w[i] = a.w[i] - b.w[i]);
    LANES(PCGTW, 4, o.w[i] = a.sw[i] > b.sw[i] ? ~0u : 0);
    LANES(PMAXW, 4, o.sw[i] = a.sw[i] > b.sw[i] ? a.sw[i] : b.sw[i]);
    LANES(PADDH, 8, o.h[i] = a.h[i] + b.h[i]);
    LANES(PSUBH, 8, o.h[i] = a.h[i] - b.h[i]);
    LANES(PCGTH, 8, o.h[i] = a.sh[i] > b.sh[i] ? 0xFFFF : 0);
    LANES(PMAXH, 8, o.sh[i] = a.sh[i] > b.sh[i] ? a.sh[i] : b.sh[i]);
    LANES(PADDB, 16, o.b[i] = a.b[i] + b.b[i]);
    LANES(PSUBB, 16, o.b[i] = a.b[i] - b.b[i]);
    LANES(PCGTB, 16, o.b[i] = a.sb[i] > b.sb[i] ? 0xFF : 0);
    LANES(PADDSW, 4, o.sw[i] = sat32((int64_t)a.sw[i] + b.sw[i]));
    LANES(PSUBSW, 4, o.sw[i] = sat32((int64_t)a.sw[i] - b.sw[i]));
    LANES(PEXTLW, 2, o.w[2 * i] = b.w[i]; o.w[2 * i + 1] = a.w[i]);
    LANES(PPACW, 2, o.w[i] = b.w[2 * i]; o.w[2 + i] = a.w[2 * i]);
    LANES(PADDSH, 8, o.sh[i] = sat16(a.sh[i] + b.sh[i]));
    LANES(PSUBSH, 8, o.sh[i] = sat16(a.sh[i] - b.sh[i]));
    LANES(PEXTLH, 4, o.h[2 * i] = b.h[i]; o.h[2 * i + 1] = a.h[i]);
    LANES(PPACH, 4, o.h[i] = b.h[2 * i]; o.h[4 + i] = a.h[2 * i]);
    LANES(PADDSB, 16, o.sb[i] = sat8(a.sb[i] + b.sb[i]));
    LANES(PSUBSB, 16, o.sb[i] = sat8(a.sb[i] - b.sb[i]));
    LANES(PEXTLB, 8, o.b[2 * i] = b.b[i]; o.b[2 * i + 1] = a.b[i]);
    LANES(PPACB, 8, o.b[i] = b.b[2 * i]; o.b[8 + i] = a.b[2 * i]);
    LANES(PEXT5, 4, { uint32_t x = b.w[i]; o.w[i] = ((x & 0x1F) << 3) | (((x >> 5) & 0x1F) << 11) | (((x >> 10) & 0x1F) << 19) | (((x >> 15) & 1) << 31); });
    LANES(PPAC5, 4, { uint32_t x = b.w[i]; o.w[i] = ((x >> 3) & 0x1F) | (((x >> 11) & 0x1F) << 5) | (((x >> 19) & 0x1F) << 10) | (((x >> 31) & 1) << 15); });
    // ---- MMI1
    LANES(PABSW, 4, o.w[i] = b.w[i] == 0x80000000u ? 0x7FFFFFFFu : (uint32_t)(b.sw[i] < 0 ? -b.sw[i] : b.sw[i]));
    LANES(PCEQW, 4, o.w[i] = a.w[i] == b.w[i] ? ~0u : 0);
    LANES(PMINW, 4, o.sw[i] = a.sw[i] < b.sw[i] ? a.sw[i] : b.sw[i]);
    LANES(PADSBH, 8, o.h[i] = i < 4 ? a.h[i] - b.h[i] : a.h[i] + b.h[i]);
    LANES(PABSH, 8, o.h[i] = b.h[i] == 0x8000 ? 0x7FFF : (uint16_t)(b.sh[i] < 0 ? -b.sh[i] : b.sh[i]));
    LANES(PCEQH, 8, o.h[i] = a.h[i] == b.h[i] ? 0xFFFF : 0);
    LANES(PMINH, 8, o.sh[i] = a.sh[i] < b.sh[i] ? a.sh[i] : b.sh[i]);
    LANES(PCEQB, 16, o.b[i] = a.b[i] == b.b[i] ? 0xFF : 0);
    LANES(PADDUW, 4, { uint64_t t = (uint64_t)a.w[i] + b.w[i]; o.w[i] = t > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)t; });
    LANES(PSUBUW, 4, o.w[i] = a.w[i] > b.w[i] ? a.w[i] - b.w[i] : 0);
    LANES(PEXTUW, 2, o.w[2 * i] = b.w[2 + i]; o.w[2 * i + 1] = a.w[2 + i]);
    LANES(PADDUH, 8, { uint32_t t = (uint32_t)a.h[i] + b.h[i]; o.h[i] = t > 0xFFFF ? 0xFFFF : t; });
    LANES(PSUBUH, 8, o.h[i] = a.h[i] > b.h[i] ? a.h[i] - b.h[i] : 0);
    LANES(PEXTUH, 4, o.h[2 * i] = b.h[4 + i]; o.h[2 * i + 1] = a.h[4 + i]);
    LANES(PADDUB, 16, { uint32_t t = (uint32_t)a.b[i] + b.b[i]; o.b[i] = t > 0xFF ? 0xFF : t; });
    LANES(PSUBUB, 16, o.b[i] = a.b[i] > b.b[i] ? a.b[i] - b.b[i] : 0);
    LANES(PEXTUB, 8, o.b[2 * i] = b.b[8 + i]; o.b[2 * i + 1] = a.b[8 + i]);
    add("QFSRV", [](St &s, Mask &) { // rd = low 128 bits of (rs:rt) >> sa bits
        uint8_t t[32]; memcpy(t, RT.b, 16); memcpy(t + 16, RS.b, 16);
        unsigned sh = s.sa & 0x7F; V o{};
        for (int bit = 0; bit < 128; ++bit) { unsigned src = bit + sh; if ((t[src / 8] >> (src % 8)) & 1) o.b[bit / 8] |= 1 << (bit % 8); }
        RD = o; });
    // ---- MMI2
    auto wordMac = [&](const char *n, int kind /*0 madd 1 msub 2 mult*/, bool uns) {
        add(n, [kind, uns](St &s, Mask &) { V a = RS, b = RT;
            for (int l = 0; l < 2; ++l) {
                uint64_t p = uns ? (uint64_t)a.w[2 * l] * b.w[2 * l] : (uint64_t)((int64_t)a.sw[2 * l] * b.sw[2 * l]);
                uint64_t r = kind == 2 ? p : kind == 0 ? acc64(s, l) + p : acc64(s, l) - p;
                setHiLo64(s, l, r); RD.d[l] = r; } });
    };
    wordMac("PMADDW", 0, false); wordMac("PMSUBW", 1, false); wordMac("PMULTW", 2, false);
    wordMac("PMADDUW", 0, true); wordMac("PMULTUW", 2, true);
    add("PSLLVW", [](St &s, Mask &) { V a = RS, b = RT; for (int l = 0; l < 2; ++l) RD.d[l] = sx32(b.w[2 * l] << (a.w[2 * l] & 31)); });
    add("PSRLVW", [](St &s, Mask &) { V a = RS, b = RT; for (int l = 0; l < 2; ++l) RD.d[l] = sx32(b.w[2 * l] >> (a.w[2 * l] & 31)); });
    add("PSRAVW", [](St &s, Mask &) { V a = RS, b = RT; for (int l = 0; l < 2; ++l) RD.d[l] = sx32((uint32_t)(b.sw[2 * l] >> (a.w[2 * l] & 31))); });
    add("PMFHI", [](St &s, Mask &) { RD = s.hi; });
    add("PMFLO", [](St &s, Mask &) { RD = s.lo; });
    LANES(PINTH, 4, o.h[2 * i] = b.h[i]; o.h[2 * i + 1] = a.h[4 + i]);
    add("PDIVW", [](St &s, Mask &) { for (int l = 0; l < 2; ++l) { uint32_t q, r; sdiv(RS.sw[2 * l], RT.sw[2 * l], q, r); s.lo.d[l] = sx32(q); s.hi.d[l] = sx32(r); } });
    add("PDIVUW", [](St &s, Mask &) { for (int l = 0; l < 2; ++l) { uint32_t q, r; udiv(RS.w[2 * l], RT.w[2 * l], q, r); s.lo.d[l] = sx32(q); s.hi.d[l] = sx32(r); } });
    LANES(PCPYLD, 1, o.d[0] = b.d[0]; o.d[1] = a.d[0]);
    // halfword multiply family: product k (0..7) goes to word: LO.w0,LO.w1,HI.w0,HI.w1,LO.w2,LO.w3,HI.w2,HI.w3
    auto halfMac = [&](const char *n, int kind /*0 madd 1 msub 2 mult*/) {
        add(n, [kind](St &s, Mask &) { V a = RS, b = RT;
            for (int k = 0; k < 8; ++k) {
                int32_t p = (int32_t)a.sh[k] * b.sh[k];
                V &dst = ((k >> 1) & 1) ? s.hi : s.lo; int wi = (k & 1) + ((k >> 2) << 1);
                dst.w[wi] = kind == 2 ? (uint32_t)p : kind == 0 ? dst.w[wi] + (uint32_t)p : dst.w[wi] - (uint32_t)p; }
            RD.w[0] = s.lo.w[0]; RD.w[1] = s.hi.w[0]; RD.w[2] = s.lo.w[2]; RD.w[3] = s.hi.w[2]; });
    };
    halfMac("PMADDH", 0); halfMac("PMSUBH", 1); halfMac("PMULTH", 2);
    // horizontal: LO.w0 = h1*h1 +/- h0*h0, HI.w0 = h3,h2, LO.w2 = h5,h4, HI.w2 = h7,h6. Odd words undefined.
    auto horiz = [&](const char *n, bool sub) {
        add(n, [sub](St &s, Mask &m) { V a = RS, b = RT;
            for (int p = 0; p < 4; ++p) {
                int32_t hiP = (int32_t)a.sh[2 * p + 1] * b.sh[2 * p + 1], loP = (int32_t)a.sh[2 * p] * b.sh[2 * p];
                uint32_t r = sub ? (uint32_t)hiP - (uint32_t)loP : (uint32_t)hiP + (uint32_t)loP;
                V &dst = (p & 1) ? s.hi : s.lo; dst.w[(p >> 1) * 2] = r; }
            m.lo[1] = m.lo[3] = m.hi[1] = m.hi[3] = false;
            RD.w[0] = s.lo.w[0]; RD.w[1] = s.hi.w[0]; RD.w[2] = s.lo.w[2]; RD.w[3] = s.hi.w[2]; });
    };
    horiz("PHMADH", false); horiz("PHMSBH", true);
    LANES(PAND, 4, o.w[i] = a.w[i] & b.w[i]);
    LANES(PXOR, 4, o.w[i] = a.w[i] ^ b.w[i]);
    LANES(PEXEH, 2, o.h[4 * i] = b.h[4 * i + 2]; o.h[4 * i + 1] = b.h[4 * i + 1]; o.h[4 * i + 2] = b.h[4 * i]; o.h[4 * i + 3] = b.h[4 * i + 3]);
    LANES(PREVH, 2, for (int k = 0; k < 4; ++k) o.h[4 * i + k] = b.h[4 * i + 3 - k]);
    add("PDIVBW", [](St &s, Mask &) { int16_t d = RT.sh[0]; for (int i = 0; i < 4; ++i) { uint32_t q, r; sdiv(RS.sw[i], d, q, r); s.lo.w[i] = q; s.hi.w[i] = (uint32_t)(int32_t)(int16_t)r; } });
    LANES(PEXEW, 1, o.w[0] = b.w[2]; o.w[1] = b.w[1]; o.w[2] = b.w[0]; o.w[3] = b.w[3]);
    LANES(PROT3W, 1, o.w[0] = b.w[1]; o.w[1] = b.w[2]; o.w[2] = b.w[0]; o.w[3] = b.w[3]);
    // ---- MMI3
    add("PMTHI", [](St &s, Mask &) { s.hi = RS; });
    add("PMTLO", [](St &s, Mask &) { s.lo = RS; });
    LANES(PINTEH, 4, o.h[2 * i] = b.h[2 * i]; o.h[2 * i + 1] = a.h[2 * i]);
    LANES(PCPYUD, 1, o.d[0] = a.d[1]; o.d[1] = b.d[1]);
    LANES(POR, 4, o.w[i] = a.w[i] | b.w[i]);
    LANES(PNOR, 4, o.w[i] = ~(a.w[i] | b.w[i]));
    LANES(PEXCH, 2, o.h[4 * i] = b.h[4 * i]; o.h[4 * i + 1] = b.h[4 * i + 2]; o.h[4 * i + 2] = b.h[4 * i + 1]; o.h[4 * i + 3] = b.h[4 * i + 3]);
    LANES(PCPYH, 2, for (int k = 0; k < 4; ++k) o.h[4 * i + k] = b.h[4 * i]);
    LANES(PEXCW, 1, o.w[0] = b.w[0]; o.w[1] = b.w[2]; o.w[2] = b.w[1]; o.w[3] = b.w[3]);
    // ---- PMFHL / PMTHL
    add("PMFHL_LW", [](St &s, Mask &) { RD.w[0] = s.lo.w[0]; RD.w[1] = s.hi.w[0]; RD.w[2] = s.lo.w[2]; RD.w[3] = s.hi.w[2]; });
    add("PMFHL_UW", [](St &s, Mask &) { RD.w[0] = s.lo.w[1]; RD.w[1] = s.hi.w[1]; RD.w[2] = s.lo.w[3]; RD.w[3] = s.hi.w[3]; });
    add("PMFHL_SLW", [](St &s, Mask &) { for (int l = 0; l < 2; ++l) RD.d[l] = sx32((uint32_t)sat32((int64_t)acc64(s, l))); });
    add("PMFHL_LH", [](St &s, Mask &) { V L = s.lo, H = s.hi; for (int l = 0; l < 2; ++l) { RD.h[4 * l] = L.h[4 * l]; RD.h[4 * l + 1] = L.h[4 * l + 2]; RD.h[4 * l + 2] = H.h[4 * l]; RD.h[4 * l + 3] = H.h[4 * l + 2]; } });
    add("PMFHL_SH", [](St &s, Mask &) { V L = s.lo, H = s.hi; for (int l = 0; l < 2; ++l) { RD.sh[4 * l] = sat16(L.sw[2 * l]); RD.sh[4 * l + 1] = sat16(L.sw[2 * l + 1]); RD.sh[4 * l + 2] = sat16(H.sw[2 * l]); RD.sh[4 * l + 3] = sat16(H.sw[2 * l + 1]); } });
    add("PMTHL_LW", [](St &s, Mask &) { s.lo.w[0] = RS.w[0]; s.hi.w[0] = RS.w[1]; s.lo.w[2] = RS.w[2]; s.hi.w[2] = RS.w[3]; });
    return v;
}

struct Gen
{
    const char *name;
    void (*fn)(R5900Context *);
};
static const Gen kGen[] = {MMI_GEN_TABLE};

static void toCtx(const St &s, R5900Context &c)
{
    memset(&c, 0, sizeof(c));
    for (int i = 0; i < 32; ++i)
        c.r[i] = _mm_loadu_si128((const __m128i *)s.r[i].w);
    c.lo = s.lo.d[0]; c.lo1 = s.lo.d[1];
    c.hi = s.hi.d[0]; c.hi1 = s.hi.d[1];
    c.sa = s.sa;
}
static void fromCtx(const R5900Context &c, St &s)
{
    for (int i = 0; i < 32; ++i)
        _mm_storeu_si128((__m128i *)s.r[i].w, c.r[i]);
    s.lo.d[0] = c.lo; s.lo.d[1] = c.lo1;
    s.hi.d[0] = c.hi; s.hi.d[1] = c.hi1;
    s.sa = c.sa;
}

static uint32_t pick(std::mt19937 &rng)
{
    static const uint32_t edge[] = {0, 1, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x8000u, 0x7FFFu, 0xFFFF8000u,
                                    0x80008000u, 0x7FFF7FFFu, 0x80808080u, 0x7F7F7F7Fu, 0xFFFF0000u, 2, 0xFFFFFFFEu};
    switch (rng() % 4)
    {
    case 0: return edge[rng() % (sizeof(edge) / 4)];
    case 1: return rng() & 0xFFFF; // small
    default: return rng();
    }
}

static void dumpV(const char *tag, const V &v) { printf("      %-4s %08x %08x %08x %08x (w3..w0)\n", tag, v.w[3], v.w[2], v.w[1], v.w[0]); }

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    std::vector<Ref> refs = buildRefs();
    std::mt19937 rng(12345);
    int bad = 0, good = 0, missing = 0;
    for (const Gen &g : kGen)
    {
        if (only && strcmp(only, g.name) != 0)
            continue;
        const Ref *ref = nullptr;
        for (const Ref &r : refs)
            if (strcmp(r.name, g.name) == 0)
                ref = &r;
        if (!ref) { printf("%-10s NO REFERENCE\n", g.name); ++missing; continue; }
        int fails = 0;
        const int kTrials = 20000;
        St firstIn{}, firstExp{}, firstGot{};
        std::string firstWhat;
        for (int t = 0; t < kTrials; ++t)
        {
            St in{};
            for (int r = 1; r < 32; ++r)
                for (int k = 0; k < 4; ++k)
                    in.r[r].w[k] = pick(rng);
            for (int k = 0; k < 4; ++k) { in.lo.w[k] = pick(rng); in.hi.w[k] = pick(rng); }
            in.sa = (rng() % 16) * 8; // MTSAB/MTSAH only produce byte multiples (stored in bits)
            if (strcmp(g.name, "PDIVBW") == 0 && in.r[3].h[0] == 0)
                in.r[3].h[0] = 7; // x/0 for PDIVBW is not specified in the manual
            St exp = in;
            Mask m;
            ref->fn(exp, m);
            alignas(16) R5900Context c;
            toCtx(in, c);
            g.fn(&c);
            St got;
            fromCtx(c, got);
            std::string what;
            for (int r = 0; r < 32; ++r)
                if (memcmp(&exp.r[r], &got.r[r], 16) != 0) what += " r" + std::to_string(r);
            for (int k = 0; k < 4; ++k)
            {
                if (m.lo[k] && exp.lo.w[k] != got.lo.w[k]) what += " LO.w" + std::to_string(k);
                if (m.hi[k] && exp.hi.w[k] != got.hi.w[k]) what += " HI.w" + std::to_string(k);
            }
            if (!what.empty())
            {
                if (fails == 0) { firstIn = in; firstExp = exp; firstGot = got; firstWhat = what; }
                ++fails;
            }
        }
        if (fails == 0) { printf("%-10s ok\n", g.name); ++good; continue; }
        ++bad;
        printf("%-10s FAIL %5d/%d  differs:%s\n", g.name, fails, kTrials, firstWhat.c_str());
        dumpV("rs", firstIn.r[2]); dumpV("rt", firstIn.r[3]); dumpV("LOin", firstIn.lo); dumpV("HIin", firstIn.hi);
        dumpV("rd!", firstExp.r[4]); dumpV("rd?", firstGot.r[4]);
        dumpV("LO!", firstExp.lo); dumpV("LO?", firstGot.lo); dumpV("HI!", firstExp.hi); dumpV("HI?", firstGot.hi);
    }
    printf("== %d ok, %d FAIL, %d without reference\n", good, bad, missing);
    return bad ? 1 : 0;
}
