// Per-pixel GS pipeline: scissor, fog, alpha test, destination-alpha test, Z test, blend, FBA,
// FBMSK and the framebuffer / depth writes. Mirrors GSCpuBackend::WritePixel exactly (including
// its simplifications: COLCLAMP and dither are not applied there, so they are not applied here).
//
// Requires gs_vram.glsl (gsReadRaw / gsWriteRaw) and gs_clut.glsl's gsRgba5551ToRgba8888.

struct GsPixelState
{
    uint fbp, fbw, fpsm, fbmsk;     // FRAME (fbp in page units)
    uint zbp, zpsm, zmask;          // ZBUF (zbp in page units)
    uint test;                      // TEST (low 32 bits hold every field the pipeline uses)
    uint alphaLo, fix;              // ALPHA selectors (a,b,c,d) and FIX
    uint fba;
    int  sx0, sy0, sx1, sy1;        // SCISSOR (inclusive)
    uint abe, fge, pabe;            // PRIM.ABE, PRIM.FGE, PABE
    uint fogR, fogG, fogB;
};

uint gsRgba8888ToRgba5551(uint c)
{
    return ((c & 0xFFu) >> 3u) | ((((c >> 8u) & 0xFFu) >> 3u) << 5u) |
           ((((c >> 16u) & 0xFFu) >> 3u) << 10u) | ((((c >> 24u) & 0xFFu) >> 7u) << 15u);
}

bool gsIs16BitFormat(uint psm)
{
    return psm == GS_PSM_CT16 || psm == GS_PSM_CT16S || psm == GS_PSM_Z16 || psm == GS_PSM_Z16S;
}

int gsPick(uint sel, int cs, int cd) { return sel == 0u ? cs : (sel == 1u ? cd : 0); }
uint gsClampU8(int v) { return uint(clamp(v, 0, 255)); }

void gsWritePixel(const GsPixelState s, int x, int y, uint z, uint r, uint g, uint b, uint a, uint fog)
{
    if (x < s.sx0 || x > s.sx1 || y < s.sy0 || y > s.sy1) return;

    if (s.fge != 0u)
    {
        const uint inv = 255u - fog;
        r = (((fog * r) >> 8u) + ((inv * s.fogR) >> 8u)) & 0xFFu;   // uint8_t truncation in the CPU path
        g = (((fog * g) >> 8u) + ((inv * s.fogG) >> 8u)) & 0xFFu;
        b = (((fog * b) >> 8u) + ((inv * s.fogB) >> 8u)) & 0xFFu;
    }

    const uint fbp = s.fbp << 5u;
    const uint fbw = max(s.fbw, 1u);
    const uint zbp = s.zbp << 5u;

    // alpha test -> write mask (classifyAlphaTest)
    const uint atst = (s.test >> 1u) & 7u;
    const uint aref = (s.test >> 4u) & 0xFFu;
    bool pass = true;
    if ((s.test & 1u) != 0u)
    {
        switch (atst)
        {
        case 0u: pass = false; break;
        case 1u: pass = true; break;
        case 2u: pass = a < aref; break;
        case 3u: pass = a <= aref; break;
        case 4u: pass = a == aref; break;
        case 5u: pass = a >= aref; break;
        case 6u: pass = a > aref; break;
        default: pass = a != aref; break;
        }
    }
    bool wRgb = true, wAlpha = true, wDepth = true;
    if (!pass)
    {
        switch ((s.test >> 12u) & 3u)
        {
        case 1u: wRgb = true; wAlpha = true; wDepth = false; break;                 // FB_ONLY
        case 2u: wRgb = false; wAlpha = false; wDepth = true; break;                // ZB_ONLY
        case 3u:                                                                    // RGB_ONLY
            wRgb = true; wAlpha = (s.fpsm != GS_PSM_CT32); wDepth = false; break;
        default: wRgb = false; wAlpha = false; wDepth = false; break;               // KEEP
        }
    }
    const bool wFb = wRgb || wAlpha;
    if (!wFb && !wDepth) return;

    const uint ztestMethod = (s.test >> 17u) & 3u;
    const bool preserveDestAlpha = wRgb && !wAlpha && s.fpsm == GS_PSM_CT32;
    const bool date = ((s.test >> 14u) & 1u) != 0u;
    const bool dateNeedsRead = date && (s.fpsm == GS_PSM_CT32 || s.fpsm == GS_PSM_CT16 || s.fpsm == GS_PSM_CT16S);
    const bool frmw = dateNeedsRead || (wFb && (s.fbmsk != 0u || s.abe != 0u || preserveDestAlpha));

    uint raw = 0u, fbrgba = 0u;
    if (frmw)
    {
        raw = gsReadRaw(s.fpsm, fbp, fbw, uint(x), uint(y));
        fbrgba = raw;
        if (gsIs16BitFormat(s.fpsm)) fbrgba = gsRgba5551ToRgba8888(fbrgba);
        else if (s.fpsm == GS_PSM_CT24) fbrgba |= 0x80000000u;
    }

    // destination alpha test
    if (date)
    {
        const uint datm = (s.test >> 15u) & 1u;
        if (s.fpsm == GS_PSM_CT32) { if (((raw >> 31u) & 1u) != datm) return; }
        else if (s.fpsm == GS_PSM_CT16 || s.fpsm == GS_PSM_CT16S) { if (((raw >> 15u) & 1u) != datm) return; }
    }

    // depth test
    bool zpass = false;
    if (ztestMethod == 1u) zpass = true;
    else if (ztestMethod >= 2u)
    {
        const uint stored = gsReadRaw(s.zpsm, zbp, fbw, uint(x), uint(y));
        zpass = (ztestMethod == 2u) ? (z >= stored) : (z > stored);
    }
    if (!zpass) return;

    if (wFb)
    {
        const uint srcR = r, srcG = g, srcB = b;
        if (s.abe != 0u)
        {
            const int dr = int(fbrgba & 0xFFu), dg = int((fbrgba >> 8u) & 0xFFu), db = int((fbrgba >> 16u) & 0xFFu);
            const int da = int((fbrgba >> 24u) & 0xFFu);
            if (!(s.pabe != 0u && (a & 0x80u) == 0u))
            {
                const uint asel = s.alphaLo & 3u, bsel = (s.alphaLo >> 2u) & 3u;
                const uint csel = (s.alphaLo >> 4u) & 3u, dsel = (s.alphaLo >> 6u) & 3u;
                const int cAlpha = (csel == 0u) ? int(a) : ((csel == 1u) ? da : int(s.fix));
                r = gsClampU8(((gsPick(asel, int(r), dr) - gsPick(bsel, int(r), dr)) * cAlpha >> 7) + gsPick(dsel, int(r), dr));
                g = gsClampU8(((gsPick(asel, int(g), dg) - gsPick(bsel, int(g), dg)) * cAlpha >> 7) + gsPick(dsel, int(g), dg));
                b = gsClampU8(((gsPick(asel, int(b), db) - gsPick(bsel, int(b), db)) * cAlpha >> 7) + gsPick(dsel, int(b), db));
            }
            else { r = srcR; g = srcG; b = srcB; }
        }

        if (wAlpha && (s.fba & 1u) != 0u && s.fpsm != GS_PSM_CT24) a |= 0x80u;

        uint pixel = r | (g << 8u) | (b << 16u) | (a << 24u);
        if (s.fbmsk != 0u) pixel = (pixel & ~s.fbmsk) | (fbrgba & s.fbmsk);
        if (preserveDestAlpha) pixel = (pixel & 0x00FFFFFFu) | (fbrgba & 0xFF000000u);
        if (gsIs16BitFormat(s.fpsm)) pixel = gsRgba8888ToRgba5551(pixel);
        gsWriteRaw(s.fpsm, fbp, fbw, uint(x), uint(y), pixel);
    }

    if (wDepth && s.zmask == 0u)
        gsWriteRaw(s.zpsm, zbp, fbw, uint(x), uint(y), z);
}
