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
} // namespace vu1n

#endif
