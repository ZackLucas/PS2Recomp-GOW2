// Per-instruction check of the native VU1 upper-unit semantics (ps2_vu1_native.h) against VU1Interpreter,
// the reference that models the hardware pipelines. Random operands include zero, denormals, inf, NaN, huge.
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_vu1_native.h"

#include <cfenv>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>

using namespace vu1n;

static void execNativeUpper(State &s, uint32_t u)
{
    const uint8_t dest = (u >> 21) & 0xF;
    const int ft = (u >> 16) & 0x1F, fs = (u >> 11) & 0x1F, fd = (u >> 6) & 0x1F;
    const uint32_t f = u & 0x3F;
    if (f < 0x3C)
    {
        if (f <= 3) return op_bc(s, K_ADD, false, fd, fs, ft, f & 3, dest);
        if (f <= 7) return op_bc(s, K_SUB, false, fd, fs, ft, f & 3, dest);
        if (f <= 0xB) return op_bc(s, K_MADD, false, fd, fs, ft, f & 3, dest);
        if (f <= 0xF) return op_bc(s, K_MSUB, false, fd, fs, ft, f & 3, dest);
        if (f <= 0x13) return maxmini_bc(s, true, fd, fs, ft, f & 3, dest);
        if (f <= 0x17) return maxmini_bc(s, false, fd, fs, ft, f & 3, dest);
        if (f <= 0x1B) return op_bc(s, K_MUL, false, fd, fs, ft, f & 3, dest);
        switch (f)
        {
        case 0x1C: return op_q(s, K_MUL, false, fd, fs, dest);
        case 0x1D: return maxmini_i(s, true, fd, fs, dest);
        case 0x1E: return op_i(s, K_MUL, false, fd, fs, dest);
        case 0x1F: return maxmini_i(s, false, fd, fs, dest);
        case 0x20: return op_q(s, K_ADD, false, fd, fs, dest);
        case 0x21: return op_q(s, K_MADD, false, fd, fs, dest);
        case 0x22: return op_i(s, K_ADD, false, fd, fs, dest);
        case 0x23: return op_i(s, K_MADD, false, fd, fs, dest);
        case 0x24: return op_q(s, K_SUB, false, fd, fs, dest);
        case 0x25: return op_q(s, K_MSUB, false, fd, fs, dest);
        case 0x26: return op_i(s, K_SUB, false, fd, fs, dest);
        case 0x27: return op_i(s, K_MSUB, false, fd, fs, dest);
        case 0x28: return op_vt(s, K_ADD, false, fd, fs, ft, dest);
        case 0x29: return op_vt(s, K_MADD, false, fd, fs, ft, dest);
        case 0x2A: return op_vt(s, K_MUL, false, fd, fs, ft, dest);
        case 0x2B: return maxmini_vt(s, true, fd, fs, ft, dest);
        case 0x2C: return op_vt(s, K_SUB, false, fd, fs, ft, dest);
        case 0x2D: return op_vt(s, K_MSUB, false, fd, fs, ft, dest);
        case 0x2E: return op_cross(s, K_OPMSUB, false, fd, fs, ft, dest);
        case 0x2F: return maxmini_vt(s, false, fd, fs, ft, dest);
        }
        return;
    }
    const uint32_t sp = (u & 3u) | ((u >> 4) & 0x7Cu);
    if (sp <= 3) return op_bc(s, K_ADD, true, 0, fs, ft, sp & 3, dest);
    if (sp <= 7) return op_bc(s, K_SUB, true, 0, fs, ft, sp & 3, dest);
    if (sp <= 0xB) return op_bc(s, K_MADD, true, 0, fs, ft, sp & 3, dest);
    if (sp <= 0xF) return op_bc(s, K_MSUB, true, 0, fs, ft, sp & 3, dest);
    if (sp >= 0x10 && sp <= 0x13) { static const int sh[4] = {0, 4, 12, 15}; return itof(s, ft, fs, dest, sh[sp & 3]); }
    if (sp >= 0x14 && sp <= 0x17) { static const int sh[4] = {0, 4, 12, 15}; return ftoi(s, ft, fs, dest, sh[sp & 3]); }
    if (sp >= 0x18 && sp <= 0x1B) return op_bc(s, K_MUL, true, 0, fs, ft, sp & 3, dest);
    switch (sp)
    {
    case 0x1C: return op_q(s, K_MUL, true, 0, fs, dest);
    case 0x1D: return abs_(s, ft, fs, dest);
    case 0x1E: return op_i(s, K_MUL, true, 0, fs, dest);
    case 0x1F: return clip(s, fs, ft);
    case 0x20: return op_q(s, K_ADD, true, 0, fs, dest);
    case 0x21: return op_q(s, K_MADD, true, 0, fs, dest);
    case 0x22: return op_i(s, K_ADD, true, 0, fs, dest);
    case 0x23: return op_i(s, K_MADD, true, 0, fs, dest);
    case 0x24: return op_q(s, K_SUB, true, 0, fs, dest);
    case 0x25: return op_q(s, K_MSUB, true, 0, fs, dest);
    case 0x26: return op_i(s, K_SUB, true, 0, fs, dest);
    case 0x27: return op_i(s, K_MSUB, true, 0, fs, dest);
    case 0x28: return op_vt(s, K_ADD, true, 0, fs, ft, dest);
    case 0x29: return op_vt(s, K_MADD, true, 0, fs, ft, dest);
    case 0x2A: return op_vt(s, K_MUL, true, 0, fs, ft, dest);
    case 0x2C: return op_vt(s, K_SUB, true, 0, fs, ft, dest);
    case 0x2D: return op_vt(s, K_MSUB, true, 0, fs, ft, dest);
    case 0x2E: return op_cross(s, K_OPMULA, true, 0, fs, ft, dest);
    }
}

static std::mt19937 rng(12345);
static float randFloat()
{
    switch (rng() % 12)
    {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: { uint32_t b = rng() & 0x807FFFFFu; float f; std::memcpy(&f, &b, 4); return f; }          // denormal
    case 3: { uint32_t b = (rng() & 0x80000000u) | 0x7F800000u | (rng() & 1); float f; std::memcpy(&f, &b, 4); return f; } // inf/nan
    case 4: { uint32_t b = (rng() & 0x80000000u) | 0x7F000000u | (rng() & 0x7FFFFFu); float f; std::memcpy(&f, &b, 4); return f; } // huge
    case 5: { uint32_t b = (rng() & 0x80000000u) | 0x00800000u | (rng() & 0xFFFFu); float f; std::memcpy(&f, &b, 4); return f; } // tiny normal
    default: { float f = (static_cast<int32_t>(rng() % 200001) - 100000) / 128.0f; return f; }
    }
}
static int32_t randIntBits() { return static_cast<int32_t>(rng()); }

int main()
{
    PS2Memory mem;
    if (!mem.initialize()) { std::puts("memory init failed"); return 2; }
    GS gs;
    gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
    uint8_t *code = mem.getVU1Code();
    uint8_t *data = mem.getVU1Data();

    struct Stat { int n = 0, bad = 0; std::string first; };
    std::map<std::string, Stat> stats;
    VU1Interpreter vu;
    int total = 0, bad = 0;
    const int kCases = 20000;

    // opcode list: plain 0..0x2F plus special 0..0x2E
    for (int sel = 0; sel < 0x30 + 0x2F; ++sel)
    {
        for (int n = 0; n < kCases / 20; ++n)
        {
            uint32_t u;
            const uint32_t dest = rng() & 0xF, ft = rng() & 31, fs = rng() & 31, fd = rng() & 31;
            std::string name;
            if (sel < 0x30) { u = sel; name = "op" + std::to_string(sel); }
            else { const uint32_t sp = sel - 0x30; if (sp == 0x2B) continue; u = 0x3C | (sp & 3) | ((sp >> 2) << 6); name = "sp" + std::to_string(sp); }
            u |= (dest << 21) | (ft << 16) | (fs << 11);
            if (sel < 0x30 || true) u = (u & ~(0x1Fu << 6)) | ((sel < 0x30 ? fd : (u >> 6 & 0x1F)) << 6);
            u |= 1u << 30; // E bit: program ends after the delay slot

            State a{};
            vu.reset();
            for (int r = 1; r < 32; ++r) for (int c = 0; c < 4; ++c) a.vf[r][c] = randFloat();
            a.vf[0][0] = a.vf[0][1] = a.vf[0][2] = 0.0f; a.vf[0][3] = 1.0f;
            for (int c = 0; c < 4; ++c) a.acc[c] = randFloat();
            a.q = randFloat(); a.i = randFloat();
            a.mac = rng() & 0xFFFF; a.status = rng() & 0xFFF; a.clip = rng() & 0xFFFFFF; a.workingClip = a.clip;
            if (sel >= 0x30 + 0x10 && sel <= 0x30 + 0x13) for (int r = 1; r < 32; ++r) for (int c = 0; c < 4; ++c) { int32_t v = randIntBits(); std::memcpy(&a.vf[r][c], &v, 4); }

            VU1State &st = vu.state();
            std::memcpy(st.vf, a.vf, sizeof(st.vf)); std::memcpy(st.acc, a.acc, sizeof(st.acc));
            st.q = a.q; st.i = a.i; st.mac = a.mac; st.status = a.status; st.clip = a.clip;
            std::memset(code, 0, 16);
            const uint32_t nopL = 0x8000033Cu, nopU = 0x000002FFu;
            std::memcpy(code + 0, &nopL, 4); std::memcpy(code + 4, &u, 4);
            std::memcpy(code + 8, &nopL, 4); std::memcpy(code + 12, &nopU, 4);
            vu.execute(code, PS2_VU1_CODE_SIZE, data, PS2_VU1_DATA_SIZE, gs, nullptr, 0, 0, 0, 1000);

            const int prev = std::fegetround();
            std::fesetround(FE_TOWARDZERO);
            execNativeUpper(a, u);
            std::fesetround(prev);

            bool ok = std::memcmp(a.vf, st.vf, sizeof(a.vf)) == 0 && std::memcmp(a.acc, st.acc, sizeof(a.acc)) == 0 &&
                      a.mac == st.mac && a.status == st.status && a.clip == st.clip;
            Stat &S = stats[name]; ++S.n; ++total;
            if (!ok) { ++S.bad; ++bad; if (S.first.empty()) { char b[400]; int vfd = 0, accd = 0; for (int r = 0; r < 32; ++r) if (std::memcmp(a.vf[r], st.vf[r], 16)) ++vfd; accd = std::memcmp(a.acc, st.acc, 16) != 0;
                std::printf("[dbg %s u=%08x] vf regs differing: %d acc differs: %d\n", name.c_str(), u, vfd, accd);
                if (name == "op0") { for (int r = 0; r < 32; ++r) if (std::memcmp(a.vf[r], st.vf[r], 16)) std::printf("   vf%d native %g %g %g %g | interp %g %g %g %g\n", r, a.vf[r][0], a.vf[r][1], a.vf[r][2], a.vf[r][3], st.vf[r][0], st.vf[r][1], st.vf[r][2], st.vf[r][3]); std::printf("   fd=%d fs=%d ft=%d dest=%x\n", (u >> 6) & 31, (u >> 11) & 31, (u >> 16) & 31, (u >> 21) & 15); } std::snprintf(b, sizeof b, "instr=%08x mac %x/%x st %x/%x clip %x/%x", u, a.mac, st.mac, a.status, st.status, a.clip, st.clip); S.first = b; } }
        }
    }
    for (auto &kv : stats) if (kv.second.bad) std::printf("%-6s %d/%d mismatches  e.g. %s\n", kv.first.c_str(), kv.second.bad, kv.second.n, kv.second.first.c_str());
    std::printf("TOTAL %d cases, %d mismatches (%.2f%% match)\n", total, bad, 100.0 * (total - bad) / total);
    return bad ? 1 : 0;
}
