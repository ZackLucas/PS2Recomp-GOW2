// CLUT state and palette lookup. The CLUT buffer mirrors GSCpuBackend::m_clut:
// 512 x 16-bit entries stored one per uint; for 32-bit palettes entry i holds the low
// half in clut[i] and the high half in clut[i + 256].
//
// Requires: `buffer Clut { uint clut[]; };` declared by the includer, and gs_vram.glsl for loads.

uint gsRgba5551ToRgba8888(uint c)
{
    return (((c >> 0u) & 0x1Fu) << 3u) | ((((c >> 5u) & 0x1Fu) << 3u) << 8u) |
           ((((c >> 10u) & 0x1Fu) << 3u) << 16u) | ((((c >> 15u) & 0x01u) << 7u) << 24u);
}

// applyTexa from gs_cpu_backend.cpp.
uint gsApplyTexa(uint ta0, uint aem, uint ta1, uint psm, uint texel)
{
    if (psm == GS_PSM_CT32) return texel;
    const bool rgbZero = (texel & 0x00FFFFFFu) == 0u;
    uint a = (texel >> 24u) & 0xFFu;
    if (psm == GS_PSM_CT24)
        a = (aem != 0u && rgbZero) ? 0u : ta0;
    else if (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S)
        a = ((a & 0x80u) != 0u) ? ta1 : ((aem != 0u && rgbZero) ? 0u : ta0);
    return (texel & 0x00FFFFFFu) | (a << 24u);
}

uint gsClutLookup(uint index, uint cpsm, uint csm, uint csa, uint sourcePsm, uint ta0, uint aem, uint ta1)
{
    const bool sixteen = cpsm == GS_PSM_CT16 || cpsm == GS_PSM_CT16S;
    const uint clutBase = (csa & (sixteen ? 0x1Fu : 0x0Fu)) << 4u;
    const bool four = sourcePsm == GS_PSM_T4 || sourcePsm == GS_PSM_T4HL || sourcePsm == GS_PSM_T4HH;
    const bool eight = sourcePsm == GS_PSM_T8 || sourcePsm == GS_PSM_T8H;
    const uint sourceIndex = four ? (index & 0x0Fu) : (index & 0xFFu);

    uint clutIndex = (clutBase + sourceIndex) & (sixteen ? 0x1FFu : 0x0FFu);
    if (!sixteen && csm == 0u && eight)
    {
        const uint block = min((sourceIndex & 0xF0u) + clutBase, 240u);
        clutIndex = block + (sourceIndex & 0x0Fu);
    }

    switch (cpsm)
    {
    case GS_PSM_CT32:
        return gsApplyTexa(ta0, aem, ta1, cpsm, (clut[clutIndex] & 0xFFFFu) | ((clut[clutIndex + 256u] & 0xFFFFu) << 16u));
    case GS_PSM_CT24:
        return gsApplyTexa(ta0, aem, ta1, cpsm,
                           ((clut[clutIndex] & 0xFFFFu) | ((clut[clutIndex + 256u] & 0xFFFFu) << 16u)) & 0x00FFFFFFu);
    case GS_PSM_CT16: case GS_PSM_CT16S:
        return gsApplyTexa(ta0, aem, ta1, cpsm, gsRgba5551ToRgba8888(clut[clutIndex] & 0xFFFFu));
    }
    return 0xFFFF00FFu;
}
