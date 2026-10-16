#ifndef PS2_VU1_NATIVE_H
#define PS2_VU1_NATIVE_H

// Native (translated) VU1 instruction semantics. Generated microprogram code calls these with constant
// register/dest arguments so the compiler specializes each call. Writes are immediate (no pipeline latency):
// the translated programs are validated against VU1Interpreter, which models the hardware pipelines.
// The caller must run with the FPU rounding mode set to round-toward-zero (the VU1 rounding).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace vu1n
{
    struct State
    {
        float vf[32][4];
        float acc[4];
        float q;
        float p;
        float i;
        int32_t vi[16];
        uint32_t mac;
        uint32_t status;
        uint32_t clip;
        uint32_t workingClip;
        uint32_t r;
        uint32_t top;
        uint32_t itop;
        uint8_t *mem;      // VU data memory
        uint32_t memSize;  // power of two (16 KiB on VU1)
    };

    inline float opnd(float v)
    {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        const uint32_t e = (b >> 23) & 0xFFu;
        if (e == 0u)
            b &= 0x80000000u;
        else if (e == 0xFFu)
            b = (b & 0x80000000u) | 0x7F7FFFFFu;
        std::memcpy(&v, &b, 4);
        return v;
    }

    // Classify an exactly computed result (bit0 zero, bit1 sign, bit2 underflow, bit3 overflow). The value is
    // replaced only when it is zero, underflowed or overflowed, like the hardware.
    inline uint8_t classify(double exact, float &value)
    {
        const bool neg = std::signbit(exact);
        const double mag = std::fabs(exact);
        uint8_t flags = neg ? 0x2u : 0u;
        uint32_t bits = neg ? 0x80000000u : 0u;
        if (mag == 0.0)
        {
            flags |= 0x1u;
            std::memcpy(&value, &bits, 4);
        }
        else if (mag > static_cast<double>(std::numeric_limits<float>::max()))
        {
            flags |= 0x8u;
            bits |= 0x7F7FFFFFu;
            std::memcpy(&value, &bits, 4);
        }
        else if (mag < static_cast<double>(std::numeric_limits<float>::min()))
        {
            flags |= 0x5u;
            std::memcpy(&value, &bits, 4);
        }
        return flags;
    }

    inline void applyDest(float *dst, const float *r, uint8_t dest)
    {
        if (dest & 8u) dst[0] = r[0];
        if (dest & 4u) dst[1] = r[1];
        if (dest & 2u) dst[2] = r[2];
        if (dest & 1u) dst[3] = r[3];
    }

    // vf0 is hard-wired to (0,0,0,1): writes to it are dropped.
    inline void writeVf(State &s, int reg, const float *r, uint8_t dest)
    {
        if (reg != 0)
            applyDest(s.vf[reg], r, dest);
    }

    enum Kind : uint8_t { K_ADD, K_SUB, K_MADD, K_MSUB, K_MUL, K_OPMSUB, K_OPMULA };

    // Core FMAC step. `a` and `b` are normalized operands per lane. productSum ops also feed the sticky flags.
    inline void fmac(State &s, Kind k, bool toAcc, int dstReg, uint8_t dest, const float *a, const float *b)
    {
        if (dest == 0u)
            return;
        float acc[4], r[4];
        for (int c = 0; c < 4; ++c)
            acc[c] = opnd(s.acc[c]);
        uint8_t lf[4] = {0, 0, 0, 0};
        uint32_t sticky = 0;
        const bool productSum = k == K_MADD || k == K_MSUB || k == K_OPMSUB;
        for (int c = 0; c < 4; ++c)
        {
            const float x = a[c], y = b[c];
            double ex = 0.0;
            switch (k)
            {
            case K_ADD: r[c] = x + y; ex = double(x) + double(y); break;
            case K_SUB: r[c] = x - y; ex = double(x) - double(y); break;
            case K_MADD: r[c] = acc[c] + x * y; ex = double(acc[c]) + double(x) * double(y); break;
            case K_MSUB: r[c] = acc[c] - x * y; ex = double(acc[c]) - double(x) * double(y); break;
            case K_MUL: r[c] = x * y; ex = double(x) * double(y); break;
            case K_OPMSUB: r[c] = c == 3 ? 0.0f : acc[c] - x * y; ex = c == 3 ? 0.0 : double(acc[c]) - double(x) * double(y); break;
            case K_OPMULA: r[c] = c == 3 ? 0.0f : x * y; ex = c == 3 ? 0.0 : double(x) * double(y); break;
            }
            if (!(dest & (8u >> c)))
                continue;
            lf[c] = classify(ex, r[c]);
            if (productSum)
            {
                float p = x * y;
                sticky |= classify(double(x) * double(y), p) & 0xFu;
            }
        }
        uint32_t mac = 0, status = 0;
        for (int c = 0; c < 4; ++c)
        {
            const uint32_t lane = 8u >> c;
            if (!(dest & lane))
                continue;
            if (lf[c] & 1u) mac |= lane;
            if (lf[c] & 2u) mac |= lane << 4;
            if (lf[c] & 4u) mac |= lane << 8;
            if (lf[c] & 8u) mac |= lane << 12;
            status |= lf[c];
        }
        s.mac = mac;
        const uint32_t cur = status & 0xFu;
        s.status = (s.status & 0xFF0u) | cur | ((cur | sticky) << 6);
        if (toAcc)
            applyDest(s.acc, r, dest);
        else
            writeVf(s, dstReg, r, dest);
    }

    inline void norm4(const float *v, float *o)
    {
        for (int c = 0; c < 4; ++c)
            o[c] = opnd(v[c]);
    }
    inline void bcast(float v, float *o)
    {
        o[0] = o[1] = o[2] = o[3] = opnd(v);
    }

    // ---- Binary FMAC families: Kind x {vt, bc, q, i}, to vf[fd] or to ACC ----
    inline void op_vt(State &s, Kind k, bool toAcc, int fd, int fs, int ft, uint8_t dest)
    {
        float a[4], b[4];
        norm4(s.vf[fs], a);
        norm4(s.vf[ft], b);
        fmac(s, k, toAcc, fd, dest, a, b);
    }
    inline void op_bc(State &s, Kind k, bool toAcc, int fd, int fs, int ft, int bc, uint8_t dest)
    {
        float a[4], b[4];
        norm4(s.vf[fs], a);
        bcast(s.vf[ft][bc], b);
        fmac(s, k, toAcc, fd, dest, a, b);
    }
    inline void op_q(State &s, Kind k, bool toAcc, int fd, int fs, uint8_t dest)
    {
        float a[4], b[4];
        norm4(s.vf[fs], a);
        bcast(s.q, b);
        fmac(s, k, toAcc, fd, dest, a, b);
    }
    inline void op_i(State &s, Kind k, bool toAcc, int fd, int fs, uint8_t dest)
    {
        float a[4], b[4];
        norm4(s.vf[fs], a);
        bcast(s.i, b);
        fmac(s, k, toAcc, fd, dest, a, b);
    }
    // OPMSUB / OPMULA: cross-product halves.
    inline void op_cross(State &s, Kind k, bool toAcc, int fd, int fs, int ft, uint8_t dest)
    {
        float vs[4], vt[4], a[4], b[4];
        norm4(s.vf[fs], vs);
        norm4(s.vf[ft], vt);
        a[0] = vs[1]; b[0] = vt[2];
        a[1] = vs[2]; b[1] = vt[0];
        a[2] = vs[0]; b[2] = vt[1];
        a[3] = vs[3]; b[3] = vt[3];
        fmac(s, k, toAcc, fd, dest, a, b);
    }

    // ---- MAX / MINI (no flags) ----
    inline void maxmini(State &s, bool isMax, int fd, int fs, const float *b, uint8_t dest)
    {
        float a[4], r[4];
        norm4(s.vf[fs], a);
        for (int c = 0; c < 4; ++c)
            r[c] = isMax ? ((a[c] > b[c]) ? a[c] : b[c]) : ((a[c] < b[c]) ? a[c] : b[c]);
        writeVf(s, fd, r, dest);
    }
    inline void maxmini_vt(State &s, bool isMax, int fd, int fs, int ft, uint8_t dest)
    {
        float b[4];
        norm4(s.vf[ft], b);
        maxmini(s, isMax, fd, fs, b, dest);
    }
    inline void maxmini_bc(State &s, bool isMax, int fd, int fs, int ft, int bc, uint8_t dest)
    {
        float b[4];
        bcast(s.vf[ft][bc], b);
        maxmini(s, isMax, fd, fs, b, dest);
    }
    inline void maxmini_i(State &s, bool isMax, int fd, int fs, uint8_t dest)
    {
        float b[4];
        bcast(s.i, b);
        maxmini(s, isMax, fd, fs, b, dest);
    }

    // ---- Conversions / ABS / CLIP (destination is FT) ----
    inline void itof(State &s, int ft, int fs, uint8_t dest, int shift)
    {
        float r[4];
        for (int c = 0; c < 4; ++c)
        {
            int32_t iv;
            std::memcpy(&iv, &s.vf[fs][c], 4);
            float f = static_cast<float>(iv);
            if (shift == 4) f /= 16.0f;
            else if (shift == 12) f /= 4096.0f;
            else if (shift == 15) f /= 32768.0f;
            r[c] = f;
        }
        writeVf(s, ft, r, dest);
    }
    inline int32_t floatToInt(float value, float scale)
    {
        const double scaled = static_cast<double>(value) * static_cast<double>(scale);
        if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        if (scaled <= static_cast<double>(std::numeric_limits<int32_t>::min()))
            return std::numeric_limits<int32_t>::min();
        return static_cast<int32_t>(scaled);
    }
    inline void ftoi(State &s, int ft, int fs, uint8_t dest, int shift)
    {
        float a[4], r[4];
        norm4(s.vf[fs], a);
        const float scale = shift == 0 ? 1.0f : shift == 4 ? 16.0f : shift == 12 ? 4096.0f : 32768.0f;
        for (int c = 0; c < 4; ++c)
        {
            const int32_t iv = floatToInt(a[c], scale);
            std::memcpy(&r[c], &iv, 4);
        }
        writeVf(s, ft, r, dest);
    }
    inline void abs_(State &s, int ft, int fs, uint8_t dest)
    {
        float a[4], r[4];
        norm4(s.vf[fs], a);
        for (int c = 0; c < 4; ++c)
            r[c] = std::fabs(a[c]);
        writeVf(s, ft, r, dest);
    }
    inline void clip(State &s, int fs, int ft)
    {
        uint32_t wBits;
        std::memcpy(&wBits, &s.vf[ft][3], 4);
        const int32_t limit = (wBits & 0x7F800000u) != 0u ? static_cast<int32_t>(wBits & 0x7FFFFFFFu) : 0x007FFFFF;
        auto exceeds = [limit](float value, uint32_t signMask)
        {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            bits ^= signMask;
            int32_t ordered;
            std::memcpy(&ordered, &bits, 4);
            return ordered > limit;
        };
        uint32_t f = 0;
        for (int c = 0; c < 3; ++c)
        {
            if (exceeds(s.vf[fs][c], 0x00000000u)) f |= 1u << (2 * c);
            if (exceeds(s.vf[fs][c], 0x80000000u)) f |= 2u << (2 * c);
        }
        s.workingClip = ((s.workingClip << 6) | (f & 0x3Fu)) & 0xFFFFFFu;
        s.clip = s.workingClip;
    }

    // ======================= Lower unit (integer / load-store / flags / divide / EFU) =======================
    // Branches, XGKICK and program flow are handled by the generated code, not here.
#define VU1N_INLINE inline __attribute__((always_inline))

    namespace detail
    {
        VU1N_INLINE int16_t imm11(uint32_t i) { return static_cast<int16_t>(static_cast<int32_t>(i << 21) >> 21); }
        VU1N_INLINE uint32_t addrOf(const State &s, int32_t base) { return (static_cast<uint32_t>(base) * 16u) & (s.memSize - 1u); }
        VU1N_INLINE float normResult(float value)
        {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            const uint32_t sign = bits & 0x80000000u, mag = bits & 0x7FFFFFFFu, e = (bits >> 23) & 0xFFu;
            if (mag == 0u) { }
            else if (e == 0u) bits = sign;
            else if (e == 0xFFu) bits = sign | 0x7F7FFFFFu;
            std::memcpy(&value, &bits, 4);
            return value;
        }
        inline float eatan(float value)
        {
            static const float c[] = {0.999999344348907f, -0.333298563957214f, 0.199465364217758f, -0.13085337519646f,
                                      0.096420042216778f, -0.055909886956215f, 0.021861229091883f, -0.004054057877511f};
            const float sq = value * value;
            float poly = c[7];
            for (int k = 6; k >= 0; --k) poly = c[k] + sq * poly;
            return 0.785398185253143f + value * poly;
        }
        inline float esin(float value)
        {
            static const float c[] = {1.0f, -0.166666567325592f, 0.008333025500178f, -0.000198074136279f, 0.000002601886990f};
            const float sq = value * value;
            float poly = c[4];
            for (int k = 3; k >= 0; --k) poly = c[k] + sq * poly;
            return value * poly;
        }
        inline float eexp(float value)
        {
            static const float c[] = {0.249998688697815f, 0.031257584691048f, 0.002591371303424f,
                                      0.000171562001924f, 0.000005430199963f, 0.000000690600018f};
            float poly = c[5];
            for (int k = 4; k >= 0; --k) poly = c[k] + value * poly;
            poly = 1.0f + value * poly;
            poly *= poly;
            poly *= poly;
            return poly != 0.0f ? 1.0f / poly : std::numeric_limits<float>::max();
        }
        VU1N_INLINE void setQ(State &s, float v, uint32_t di)
        {
            s.q = normResult(v);
            const uint32_t d = di & 0x30u;
            s.status = (s.status & 0xFCFu) | d | (d << 6);
        }
        VU1N_INLINE int destComp(uint8_t dest) { return (dest & 8) ? 0 : (dest & 4) ? 1 : (dest & 2) ? 2 : 3; }
        VU1N_INLINE void storeQ(State &s, uint32_t addr, const uint32_t w[4], uint8_t mask)
        {
            uint32_t old[4];
            std::memcpy(old, s.mem + addr, 16);
            for (int c = 0; c < 4; ++c)
                if (mask & (8u >> c)) old[c] = w[c];
            std::memcpy(s.mem + addr, old, 16);
        }
        VU1N_INLINE void viSet(State &s, int reg, int32_t v) { if (reg != 0) s.vi[reg] = static_cast<int16_t>(v); }
    } // namespace detail

    // Executes one non-branching lower instruction. `i` is a literal in generated code so this folds away.
    VU1N_INLINE void lower(State &s, uint32_t i)
    {
        using namespace detail;
        if (i == 0x00000000u || i == 0x8000033Cu)
            return;
        const uint32_t op = (i >> 25) & 0x7Fu;
        const int ft = (i >> 16) & 0x1F, fs = (i >> 11) & 0x1F, fd = (i >> 6) & 0x1F;
        const int it = ft & 0xF, is = fs & 0xF, id = fd & 0xF;
        const uint8_t dest = (i >> 21) & 0xF;
        const uint32_t imm24 = i & 0xFFFFFFu;
        const uint16_t imm12 = static_cast<uint16_t>((((i >> 21) & 1u) << 11) | (i & 0x7FFu));
        switch (op)
        {
        case 0x00: { const uint32_t a = addrOf(s, s.vi[is] + imm11(i)); float t[4]; std::memcpy(t, s.mem + a, 16); writeVf(s, ft, t, dest); return; }
        case 0x01: { const uint32_t a = addrOf(s, s.vi[it] + imm11(i)); uint32_t w[4]; std::memcpy(w, s.vf[fs], 16); storeQ(s, a, w, dest); return; }
        case 0x04: { const uint32_t a = addrOf(s, s.vi[is] + imm11(i)); uint32_t v; std::memcpy(&v, s.mem + a + destComp(dest) * 4, 4); viSet(s, it, static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFF))); return; }
        case 0x05: { const uint32_t a = addrOf(s, s.vi[is] + imm11(i)); const uint32_t v = static_cast<uint16_t>(s.vi[it] & 0xFFFF); const uint32_t w[4] = {v, v, v, v}; storeQ(s, a, w, dest); return; }
        case 0x08: { const int16_t imm = static_cast<int16_t>(static_cast<int16_t>(i & 0x7FF) | ((i >> 10) & 0x7800)); viSet(s, it, static_cast<int16_t>(s.vi[is] + imm)); return; }
        case 0x09: { const int16_t imm = static_cast<int16_t>(static_cast<int16_t>(i & 0x7FF) | ((i >> 10) & 0x7800)); viSet(s, it, static_cast<int16_t>(s.vi[is] - imm)); return; }
        case 0x10: s.vi[1] = ((s.clip & 0xFFFFFFu) == imm24) ? 1 : 0; return;
        case 0x11: s.workingClip = imm24; s.clip = imm24; return;
        case 0x12: s.vi[1] = ((s.clip & imm24) != 0) ? 1 : 0; return;
        case 0x13: s.vi[1] = ((s.clip | imm24) == 0xFFFFFFu) ? 1 : 0; return;
        case 0x14: viSet(s, it, ((s.status & 0xFFFu) == imm12) ? 1 : 0); return;
        case 0x15: s.status = (s.status & 0x03Fu) | (imm12 & 0xFC0u); return;
        case 0x16: viSet(s, it, static_cast<int32_t>((s.status & 0xFFFu) & imm12)); return;
        case 0x17: viSet(s, it, static_cast<int32_t>((s.status & 0xFFFu) | imm12)); return;
        case 0x18: viSet(s, it, ((s.mac & 0xFFFFu) == static_cast<uint32_t>(static_cast<uint16_t>(s.vi[is]))) ? 1 : 0); return;
        case 0x1A: viSet(s, it, static_cast<int32_t>(s.mac & static_cast<uint32_t>(static_cast<uint16_t>(s.vi[is])))); return;
        case 0x1B: viSet(s, it, static_cast<int32_t>(s.mac | static_cast<uint32_t>(static_cast<uint16_t>(s.vi[is])))); return;
        case 0x1C: viSet(s, it, static_cast<int32_t>(s.clip & 0x0FFFu)); return;
        case 0x40:
        {
            const uint32_t funct = i & 0x3Fu;
            switch (funct)
            {
            case 0x30: viSet(s, id, static_cast<int16_t>(s.vi[is] + s.vi[it])); return;
            case 0x31: viSet(s, id, static_cast<int16_t>(s.vi[is] - s.vi[it])); return;
            case 0x32: { const int16_t imm5 = static_cast<int16_t>(static_cast<int32_t>((i >> 6) & 0x1F) << 27 >> 27); viSet(s, it, static_cast<int16_t>(s.vi[is] + imm5)); return; }
            case 0x34: viSet(s, id, s.vi[is] & s.vi[it]); return;
            case 0x35: viSet(s, id, s.vi[is] | s.vi[it]); return;
            default: break;
            }
            if (funct < 0x3C) return;
            const uint32_t f2 = (i & 3u) | ((i >> 4) & 0x7Cu);
            const int fsf = (i >> 21) & 3, ftf = (i >> 23) & 3;
            switch (f2)
            {
            case 0x30: { float t[4]; std::memcpy(t, s.vf[fs], 16); writeVf(s, ft, t, dest); return; }
            case 0x31: { const float t[4] = {s.vf[fs][1], s.vf[fs][2], s.vf[fs][3], s.vf[fs][0]}; writeVf(s, ft, t, dest); return; }
            case 0x34: { const uint32_t a = addrOf(s, static_cast<uint16_t>(s.vi[is])); float t[4]; std::memcpy(t, s.mem + a, 16); writeVf(s, ft, t, dest); if (is != 0) s.vi[is] = static_cast<int16_t>(s.vi[is] + 1); return; }
            case 0x35: { const uint32_t a = addrOf(s, static_cast<uint16_t>(s.vi[it])); uint32_t w[4]; std::memcpy(w, s.vf[fs], 16); storeQ(s, a, w, dest); if (it != 0) s.vi[it] = static_cast<int16_t>(s.vi[it] + 1); return; }
            case 0x36: { if (is != 0) s.vi[is] = static_cast<int16_t>(s.vi[is] - 1); const uint32_t a = addrOf(s, static_cast<uint16_t>(s.vi[is])); float t[4]; std::memcpy(t, s.mem + a, 16); writeVf(s, ft, t, dest); return; }
            case 0x37: { if (it != 0) s.vi[it] = static_cast<int16_t>(s.vi[it] - 1); const uint32_t a = addrOf(s, static_cast<uint16_t>(s.vi[it])); uint32_t w[4]; std::memcpy(w, s.vf[fs], 16); storeQ(s, a, w, dest); return; }
            case 0x38:
            {
                const float num = opnd(s.vf[fs][fsf]), den = opnd(s.vf[ft][ftf]);
                uint32_t di = 0; float r;
                if (den == 0.0f) { di = num == 0.0f ? 0x10u : 0x20u; r = std::signbit(num) != std::signbit(den) ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max(); }
                else r = num / den;
                setQ(s, normResult(r), di);
                return;
            }
            case 0x39: { const float v = opnd(s.vf[ft][ftf]); setQ(s, std::sqrt(std::fabs(v)), v < 0.0f ? 0x10u : 0u); return; }
            case 0x3A:
            {
                const float num = opnd(s.vf[fs][fsf]), rad = opnd(s.vf[ft][ftf]);
                const float den = std::sqrt(std::fabs(rad));
                uint32_t di = rad < 0.0f ? 0x10u : 0u; float r;
                if (den != 0.0f) r = num / den;
                else { di = num == 0.0f ? 0x10u : 0x20u; r = std::signbit(num) ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max(); }
                setQ(s, normResult(r), di);
                return;
            }
            case 0x3B: return;
            case 0x3C: { uint32_t v; std::memcpy(&v, &s.vf[fs][(i >> 21) & 3], 4); viSet(s, it, static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFF))); return; }
            case 0x3D: { int32_t v = static_cast<int32_t>(static_cast<int16_t>(s.vi[is] & 0xFFFF)); float f; std::memcpy(&f, &v, 4); const float t[4] = {f, f, f, f}; writeVf(s, ft, t, dest); return; }
            case 0x3E: { const uint32_t a = addrOf(s, static_cast<uint16_t>(s.vi[is])); uint32_t v; std::memcpy(&v, s.mem + a + destComp(dest) * 4, 4); viSet(s, it, static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFF))); return; }
            case 0x3F: { const uint32_t a = addrOf(s, static_cast<uint16_t>(s.vi[is])); const uint32_t v = static_cast<uint16_t>(s.vi[it] & 0xFFFF); const uint32_t w[4] = {v, v, v, v}; storeQ(s, a, w, dest); return; }
            case 0x40: { const uint32_t x = (s.r >> 4) & 1u, y = (s.r >> 22) & 1u; s.r = ((s.r << 1) ^ x ^ y) & 0x007FFFFFu; s.r |= 0x3F800000u; float f; std::memcpy(&f, &s.r, 4); const float t[4] = {f, f, f, f}; writeVf(s, ft, t, dest); return; }
            case 0x41: { float f; std::memcpy(&f, &s.r, 4); const float t[4] = {f, f, f, f}; writeVf(s, ft, t, dest); return; }
            case 0x42: { uint32_t b; std::memcpy(&b, &s.vf[fs][(i >> 21) & 3], 4); s.r = 0x3F800000u | (b & 0x007FFFFFu); return; }
            case 0x43: { uint32_t b; std::memcpy(&b, &s.vf[fs][(i >> 21) & 3], 4); s.r = 0x3F800000u | ((s.r ^ b) & 0x007FFFFFu); return; }
            case 0x64: { const float t[4] = {s.p, s.p, s.p, s.p}; writeVf(s, ft, t, dest); return; }
            case 0x68: viSet(s, it, static_cast<int32_t>(s.top & 0x3FFu)); return;
            case 0x69: viSet(s, it, static_cast<int32_t>(s.itop & 0x3FFu)); return;
            case 0x70: case 0x71: case 0x72: case 0x73:
            {
                const float x = opnd(s.vf[fs][0]), y = opnd(s.vf[fs][1]), z = opnd(s.vf[fs][2]);
                const float sum = x * x + y * y + z * z;
                float r;
                if (f2 == 0x70) r = sum;
                else if (f2 == 0x71) r = sum != 0.0f ? 1.0f / sum : sum;
                else { const float len = std::sqrt(sum); r = f2 == 0x72 ? len : (len != 0.0f ? 1.0f / len : len); }
                s.p = normResult(r);
                return;
            }
            case 0x74: { const float x = opnd(s.vf[fs][0]), y = opnd(s.vf[fs][1]); s.p = normResult(x != 0.0f ? eatan(y / x) : 0.0f); return; }
            case 0x75: { const float x = opnd(s.vf[fs][0]), z = opnd(s.vf[fs][2]); s.p = normResult(x != 0.0f ? eatan(z / x) : 0.0f); return; }
            case 0x76: { float sum = 0.0f; for (int c = 0; c < 4; ++c) sum += opnd(s.vf[fs][c]); s.p = normResult(sum); return; }
            case 0x78: { const float v = opnd(s.vf[fs][(i >> 21) & 3]); s.p = normResult(v >= 0.0f ? std::sqrt(v) : v); return; }
            case 0x79: { float r = opnd(s.vf[fs][(i >> 21) & 3]); if (r >= 0.0f) { r = std::sqrt(r); if (r != 0.0f) r = 1.0f / r; } s.p = normResult(r); return; }
            case 0x7A: { const float v = opnd(s.vf[fs][(i >> 21) & 3]); s.p = normResult(v != 0.0f ? 1.0f / v : v); return; }
            case 0x7B: return;
            case 0x7C: s.p = normResult(esin(opnd(s.vf[fs][(i >> 21) & 3]))); return;
            case 0x7D: s.p = normResult(eatan(opnd(s.vf[fs][(i >> 21) & 3]))); return;
            case 0x7E: s.p = normResult(eexp(opnd(s.vf[fs][(i >> 21) & 3]))); return;
            default: return;
            }
        }
        default: return;
        }
    }
} // namespace vu1n

#endif
