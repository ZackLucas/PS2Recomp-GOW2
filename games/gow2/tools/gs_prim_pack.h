// Packs a GSPrimitiveBatch into the std430 record the GPU draw shaders read (gs_draw_common.glsl).
#pragma once
#include "runtime/gs/gs_types.h"

#include <cstdint>

struct GpuVtx
{
    float x, y;
    double z;
    uint32_t rgba, uv;
    float s, t, q;
    uint32_t fog;
};
struct GpuPrim
{
    uint32_t h[48];
    GpuVtx v[3];
};
static_assert(sizeof(GpuVtx) == 40, "GpuVtx must match the std430 Vtx struct");
static_assert(sizeof(GpuPrim) == 312, "GpuPrim must match the std430 Prim struct");

namespace gpuprim
{
enum Field : uint32_t
{
    FLAGS = 0, TYPE, FBP, FBW, FPSM, FBMSK, ZBP, ZPSM, TEST, ALPHA, FIX, FBA, SX0, SY0, SX1, SY1, OFX, OFY,
    TBP0, TBW, TPSM, TW, TH, TCC, TFX, CBP, CPSM, CSM, CSA, WRAPU, WRAPV, MINU, MAXU, MINV, MAXV,
    TA0, AEM, TA1, FOGR, FOGG, FOGB
};
enum Flags : uint32_t { IIP = 1, TME = 2, FGE = 4, ABE = 8, FST = 16, PABE = 32, LINEAR = 64, ZMASK = 128 };

inline GpuPrim pack(const GSPrimitiveBatch &batch)
{
    GpuPrim p{};
    const GSDrawState &st = batch.state;
    const GSContext &c = st.context;
    uint32_t flags = 0;
    flags |= st.prim.iip ? IIP : 0;
    flags |= st.prim.tme ? TME : 0;
    flags |= st.prim.fge ? FGE : 0;
    flags |= st.prim.abe ? ABE : 0;
    flags |= st.prim.fst ? FST : 0;
    flags |= st.pabe ? PABE : 0;
    flags |= st.linearFilter ? LINEAR : 0;
    flags |= c.zbuf.zmask ? ZMASK : 0;
    p.h[FLAGS] = flags;
    p.h[TYPE] = static_cast<uint32_t>(st.prim.type);
    p.h[FBP] = c.frame.fbp; p.h[FBW] = c.frame.fbw; p.h[FPSM] = c.frame.psm; p.h[FBMSK] = c.frame.fbmsk;
    p.h[ZBP] = c.zbuf.zbp; p.h[ZPSM] = c.zbuf.psm;
    p.h[TEST] = static_cast<uint32_t>(c.test);
    p.h[ALPHA] = static_cast<uint32_t>(c.alpha & 0xFFu);
    p.h[FIX] = static_cast<uint32_t>((c.alpha >> 32) & 0xFFu);
    p.h[FBA] = static_cast<uint32_t>(c.fba & 1u);
    p.h[SX0] = c.scissor.x0; p.h[SY0] = c.scissor.y0; p.h[SX1] = c.scissor.x1; p.h[SY1] = c.scissor.y1;
    p.h[OFX] = static_cast<uint32_t>(static_cast<int32_t>(c.xyoffset.ofx >> 4));
    p.h[OFY] = static_cast<uint32_t>(static_cast<int32_t>(c.xyoffset.ofy >> 4));
    p.h[TBP0] = c.tex0.tbp0; p.h[TBW] = c.tex0.tbw; p.h[TPSM] = c.tex0.psm;
    p.h[TW] = st.textureWidth; p.h[TH] = st.textureHeight;
    p.h[TCC] = c.tex0.tcc; p.h[TFX] = c.tex0.tfx;
    p.h[CBP] = c.tex0.cbp; p.h[CPSM] = c.tex0.cpsm; p.h[CSM] = c.tex0.csm; p.h[CSA] = c.tex0.csa;
    p.h[WRAPU] = static_cast<uint32_t>(c.clamp & 3u);
    p.h[WRAPV] = static_cast<uint32_t>((c.clamp >> 2) & 3u);
    p.h[MINU] = static_cast<uint32_t>((c.clamp >> 4) & 0x3FFu);
    p.h[MAXU] = static_cast<uint32_t>((c.clamp >> 14) & 0x3FFu);
    p.h[MINV] = static_cast<uint32_t>((c.clamp >> 24) & 0x3FFu);
    p.h[MAXV] = static_cast<uint32_t>((c.clamp >> 34) & 0x3FFu);
    p.h[TA0] = st.texa.ta0; p.h[AEM] = st.texa.aem ? 1u : 0u; p.h[TA1] = st.texa.ta1;
    p.h[FOGR] = st.fogR; p.h[FOGG] = st.fogG; p.h[FOGB] = st.fogB;
    for (uint32_t i = 0; i < 3 && i < batch.vertexCount; ++i)
    {
        const GSVertex &v = batch.vertices[i];
        GpuVtx &o = p.v[i];
        o.x = v.x; o.y = v.y; o.z = v.z;
        o.rgba = static_cast<uint32_t>(v.r) | (static_cast<uint32_t>(v.g) << 8) |
                 (static_cast<uint32_t>(v.b) << 16) | (static_cast<uint32_t>(v.a) << 24);
        o.uv = static_cast<uint32_t>(v.u) | (static_cast<uint32_t>(v.v) << 16);
        o.s = v.s; o.t = v.t; o.q = v.q; o.fog = v.fog;
    }
    return p;
}
} // namespace gpuprim
