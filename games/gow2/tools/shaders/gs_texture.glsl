// Texture sampling and texture-function combine. Mirrors GSCpuBackend::SampleTexture,
// wrapTextureCoordinate and combineTexture. Needs gs_vram.glsl and gs_clut.glsl.

struct TexState
{
    uint tbp0, tbw, psm, w, h, tcc, tfx, cbp, cpsm, csm, csa;
    uint wrapU, wrapV, minU, maxU, minV, maxV;
    uint ta0, aem, ta1;
    uint linear, fst;
};

int gsWrapCoord(int c, int size, uint mode, uint rmin, uint rmax)
{
    switch (mode & 3u)
    {
    case 0u: return int(uint(c) & uint(size - 1));
    case 1u: return clamp(c, 0, size - 1);
    case 2u: return min(max(c, int(rmin)), int(rmax));
    default: return int((uint(c) & rmin) | rmax);
    }
}

uint gsSamplePoint(const TexState t, int u, int v)
{
    u = gsWrapCoord(u, int(t.w), t.wrapU, t.minU, t.maxU);
    v = gsWrapCoord(v, int(t.h), t.wrapV, t.minV, t.maxV);
    const uint raw = gsReadRaw(t.psm, t.tbp0, t.tbw, uint(u), uint(v));
    switch (t.psm)
    {
    case GS_PSM_CT32: case GS_PSM_Z32: case GS_PSM_CT24: case GS_PSM_Z24:
        return gsApplyTexa(t.ta0, t.aem, t.ta1, t.psm, raw);
    case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S:
        return gsApplyTexa(t.ta0, t.aem, t.ta1, t.psm, gsRgba5551ToRgba8888(raw));
    case GS_PSM_T8: case GS_PSM_T8H: case GS_PSM_T4: case GS_PSM_T4HL: case GS_PSM_T4HH:
        return gsClutLookup(raw & 0xFFu, t.cpsm, t.csm, t.csa, t.psm, t.ta0, t.aem, t.ta1);
    }
    return 0xFFFF00FFu;
}

uint gsLerpChannel(uint c00, uint c10, uint c01, uint c11, float fx, float fy)
{
    // 4-bit weights, truncation at each stage (mirrors GSCpuBackend lerpChannel)
    const int wx = clamp(int(fx * 16.0), 0, 15), wy = clamp(int(fy * 16.0), 0, 15);
    const int top = int(c00) + (((int(c10) - int(c00)) * wx) >> 4);
    const int bottom = int(c01) + (((int(c11) - int(c01)) * wx) >> 4);
    return uint(clamp(top + (((bottom - top) * wy) >> 4), 0, 255));
}

uint gsSampleTexture(const TexState t, float s, float tt, float q, uint u, uint v)
{
    precise float texUf, texVf;
    if (t.fst != 0u)
    {
        texUf = float(u) / 16.0;
        texVf = float(v) / 16.0;
    }
    else
    {
        const float qq = (abs(q) > 1.0e-8) ? q : 1.0;
        precise float invQ = gsDiv(1.0, qq);
        texUf = s * invQ * float(t.w);
        texVf = tt * invQ * float(t.h);
    }

    if (t.linear == 0u)
        return gsSamplePoint(t, int(texUf), int(texVf));

    precise float su = texUf - 0.5, sv = texVf - 0.5;
    const int u0 = int(floor(su)), v0 = int(floor(sv));
    precise float fx = su - float(u0), fy = sv - float(v0);
    const uint c00 = gsSamplePoint(t, u0, v0), c10 = gsSamplePoint(t, u0 + 1, v0);
    const uint c01 = gsSamplePoint(t, u0, v0 + 1), c11 = gsSamplePoint(t, u0 + 1, v0 + 1);
    const uint r = gsLerpChannel(c00 & 0xFFu, c10 & 0xFFu, c01 & 0xFFu, c11 & 0xFFu, fx, fy);
    const uint g = gsLerpChannel((c00 >> 8u) & 0xFFu, (c10 >> 8u) & 0xFFu, (c01 >> 8u) & 0xFFu, (c11 >> 8u) & 0xFFu, fx, fy);
    const uint b = gsLerpChannel((c00 >> 16u) & 0xFFu, (c10 >> 16u) & 0xFFu, (c01 >> 16u) & 0xFFu, (c11 >> 16u) & 0xFFu, fx, fy);
    const uint a = gsLerpChannel((c00 >> 24u) & 0xFFu, (c10 >> 24u) & 0xFFu, (c01 >> 24u) & 0xFFu, (c11 >> 24u) & 0xFFu, fx, fy);
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

// combineTexture: returns RGBA packed like the texel (r | g<<8 | b<<16 | a<<24).
uint gsCombineTexture(const TexState t, uint vr, uint vg, uint vb, uint va, uint texel)
{
    const uint tr = texel & 0xFFu, tg = (texel >> 8u) & 0xFFu, tb = (texel >> 16u) & 0xFFu, ta = (texel >> 24u) & 0xFFu;
    const bool tcc = t.tcc != 0u;
    uint r = tr, g = tg, b = tb, a = tcc ? ta : va;
    switch (t.tfx)
    {
    case 0u:
        r = min((tr * vr) >> 7u, 255u); g = min((tg * vg) >> 7u, 255u); b = min((tb * vb) >> 7u, 255u);
        a = tcc ? min((ta * va) >> 7u, 255u) : va;
        break;
    case 1u:
        r = tr; g = tg; b = tb; a = tcc ? ta : va;
        break;
    case 2u:
        r = min(((tr * vr) >> 7u) + va, 255u); g = min(((tg * vg) >> 7u) + va, 255u); b = min(((tb * vb) >> 7u) + va, 255u);
        a = tcc ? min(ta + va, 255u) : va;
        break;
    case 3u:
        r = min(((tr * vr) >> 7u) + va, 255u); g = min(((tg * vg) >> 7u) + va, 255u); b = min(((tb * vb) >> 7u) + va, 255u);
        a = tcc ? ta : va;
        break;
    default:
        r = tr; g = tg; b = tb; a = tcc ? ta : va;
        break;
    }
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}
