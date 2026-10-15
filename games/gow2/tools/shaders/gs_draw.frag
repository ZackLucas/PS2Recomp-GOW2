// Per-fragment GS: coverage and attribute interpolation as the CPU rasterizer does them, texture
// sampling, then the per-pixel pipeline inside an ordered per-pixel critical section.
layout(pixel_interlock_ordered) in;
flat in uint vPrim;

uint u8c(int v) { return uint(clamp(v, 0, 255)); }

void main()
{
    const uint id = vPrim;
    const uint flags = pu(id, H_FLAGS);
    const uint type = pu(id, H_TYPE);
    const int x = int(gl_FragCoord.x), y = int(gl_FragCoord.y);
    const int ofx = pi(id, H_OFX), ofy = pi(id, H_OFY);
    const Vtx v0 = prims[id].v[0], v1 = prims[id].v[1], v2 = prims[id].v[2];

    uint r, g, b, a, fog, z;
    float is = 0.0, it = 0.0, iq = 1.0;
    uint iu = 0u, iv = 0u;
    TexState ts;
    ts.tbp0 = pu(id, H_TBP0); ts.tbw = pu(id, H_TBW); ts.psm = pu(id, H_TPSM); ts.w = pu(id, H_TW); ts.h = pu(id, H_TH);
    ts.tcc = pu(id, H_TCC); ts.tfx = pu(id, H_TFX); ts.cbp = pu(id, H_CBP); ts.cpsm = pu(id, H_CPSM);
    ts.csm = pu(id, H_CSM); ts.csa = pu(id, H_CSA);
    ts.wrapU = pu(id, H_WRAPU); ts.wrapV = pu(id, H_WRAPV); ts.minU = pu(id, H_MINU); ts.maxU = pu(id, H_MAXU);
    ts.minV = pu(id, H_MINV); ts.maxV = pu(id, H_MAXV);
    ts.ta0 = pu(id, H_TA0); ts.aem = pu(id, H_AEM); ts.ta1 = pu(id, H_TA1);
    ts.linear = (flags & F_LINEAR) != 0u ? 1u : 0u; ts.fst = (flags & F_FST) != 0u ? 1u : 0u;
    const bool tme = (flags & F_TME) != 0u;

    uint texel = 0u;
    if (type == PRIM_TRIANGLE || type == PRIM_TRISTRIP || type == PRIM_TRIFAN)
    {
        precise float fx0 = v0.x - float(ofx), fy0 = v0.y - float(ofy);
        precise float fx1 = v1.x - float(ofx), fy1 = v1.y - float(ofy);
        precise float fx2 = v2.x - float(ofx), fy2 = v2.y - float(ofy);
        precise float denom = (fy1 - fy2) * (fx0 - fx2) + (fx2 - fx1) * (fy0 - fy2);
        if (abs(denom) < 0.001) discard;
        const float winding = (denom < 0.0) ? -1.0 : 1.0;
        precise float invAbsDenom = gsDiv(1.0, abs(denom));
        precise float px = float(x), py = float(y);
        precise float w0 = (((fy1 - fy2) * (px - fx2) + (fx2 - fx1) * (py - fy2)) * winding) * invAbsDenom;
        precise float w1 = (((fy2 - fy0) * (px - fx2) + (fx0 - fx2) * (py - fy2)) * winding) * invAbsDenom;
        precise float w2 = 1.0 - w0 - w1;
        const float eps = 1.0e-4;
        if (w0 < -eps || w1 < -eps || w2 < -eps) discard;
        {   // top-left fill rule (see GSCpuBackend::DrawTriangle): on-edge pixels only for left/top edges
            const float gx0 = (fy1 - fy2) * winding, gy0 = (fx2 - fx1) * winding;
            const float gx1 = (fy2 - fy0) * winding, gy1 = (fx0 - fx2) * winding;
            const float gx2 = -(gx0 + gx1), gy2 = -(gy0 + gy1);
            if (w0 <= eps && !(gx0 > 0.0 || (gx0 == 0.0 && gy0 > 0.0))) discard;
            if (w1 <= eps && !(gx1 > 0.0 || (gx1 == 0.0 && gy1 > 0.0))) discard;
            if (w2 <= eps && !(gx2 > 0.0 || (gx2 == 0.0 && gy2 > 0.0))) discard;
        }

        precise double zd = v0.z * double(w0) + v1.z * double(w1) + v2.z * double(w2);
        z = uint(zd);

        if ((flags & F_IIP) != 0u)
        {
            precise float rr = float(v0.rgba & 0xFFu) * w0 + float((v1.rgba) & 0xFFu) * w1 + float(v2.rgba & 0xFFu) * w2;
            precise float gg = float((v0.rgba >> 8u) & 0xFFu) * w0 + float((v1.rgba >> 8u) & 0xFFu) * w1 + float((v2.rgba >> 8u) & 0xFFu) * w2;
            precise float bb = float((v0.rgba >> 16u) & 0xFFu) * w0 + float((v1.rgba >> 16u) & 0xFFu) * w1 + float((v2.rgba >> 16u) & 0xFFu) * w2;
            precise float aa = float(v0.rgba >> 24u) * w0 + float(v1.rgba >> 24u) * w1 + float(v2.rgba >> 24u) * w2;
            r = u8c(int(rr)); g = u8c(int(gg)); b = u8c(int(bb)); a = u8c(int(aa));
        }
        else
        {
            r = v2.rgba & 0xFFu; g = (v2.rgba >> 8u) & 0xFFu; b = (v2.rgba >> 16u) & 0xFFu; a = v2.rgba >> 24u;
        }

        if (tme)
        {
            if (ts.fst != 0u)
            {
                precise float fu = float(v0.uv & 0xFFFFu) * w0 + float(v1.uv & 0xFFFFu) * w1 + float(v2.uv & 0xFFFFu) * w2;
                precise float fv = float(v0.uv >> 16u) * w0 + float(v1.uv >> 16u) * w1 + float(v2.uv >> 16u) * w2;
                iu = uint(fu) & 0xFFFFu; iv = uint(fv) & 0xFFFFu;
            }
            else
            {
                is = v0.s * w0 + v1.s * w1 + v2.s * w2;
                it = v0.t * w0 + v1.t * w1 + v2.t * w2;
                iq = v0.q * w0 + v1.q * w1 + v2.q * w2;
            }
        }
        precise float ff = float(v0.fog) * w0 + float(v1.fog) * w1 + float(v2.fog) * w2;
        fog = u8c(int(ff));
    }
    else if (type == PRIM_SPRITE)
    {
        int x0 = int(v0.x) - ofx, y0 = int(v0.y) - ofy, x1 = int(v1.x) - ofx, y1 = int(v1.y) - ofy;
        if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { const int t = y0; y0 = y1; y1 = t; }
        const int spanX = max(1, x1 - x0), spanY = max(1, y1 - y0);
        r = v1.rgba & 0xFFu; g = (v1.rgba >> 8u) & 0xFFu; b = (v1.rgba >> 16u) & 0xFFu; a = v1.rgba >> 24u;
        z = uint(v1.z);
        fog = v1.fog;
        if (tme)
        {
            float u0f, v0f, u1f, v1f;
            if (ts.fst != 0u)
            {
                u0f = float((v0.uv & 0xFFFFu) >> 4u); v0f = float((v0.uv >> 16u) >> 4u);
                u1f = float((v1.uv & 0xFFFFu) >> 4u); v1f = float((v1.uv >> 16u) >> 4u);
            }
            else
            {
                const float q0 = (abs(v0.q) > 1.0e-8) ? v0.q : 1.0, q1 = (abs(v1.q) > 1.0e-8) ? v1.q : 1.0;
                u0f = gsDiv(v0.s, q0) * float(ts.w); v0f = gsDiv(v0.t, q0) * float(ts.h);
                u1f = gsDiv(v1.s, q1) * float(ts.w); v1f = gsDiv(v1.t, q1) * float(ts.h);
            }
            precise float spriteW = max(float(spanX), 1.0), spriteH = max(float(spanY), 1.0);
            precise float ty = gsDiv(float(y - y0), spriteH);
            precise float tx = gsDiv(float(x - x0), spriteW);
            precise float texVf = v0f + (v1f - v0f) * ty;
            precise float texUf = u0f + (u1f - u0f) * tx;
            if (ts.fst != 0u)
            {
                const int fixedU = int(texUf * 16.0 + 0.5), fixedV = int(texVf * 16.0 + 0.5);
                iu = uint(clamp(fixedU, 0, 0xFFFF)); iv = uint(clamp(fixedV, 0, 0xFFFF));
            }
            else
            {
                is = gsDiv(texUf, float(ts.w)); it = gsDiv(texVf, float(ts.h)); iq = 1.0;
            }
        }
    }
    else if (type == PRIM_POINT)
    {
        r = v0.rgba & 0xFFu; g = (v0.rgba >> 8u) & 0xFFu; b = (v0.rgba >> 16u) & 0xFFu; a = v0.rgba >> 24u;
        z = uint(v0.z);
        fog = v0.fog;
    }
    else
        discard;

    // The CPU backend's point path never samples the texture (it writes the vertex color), so neither
    // do we, to stay equivalent; a textured point on real hardware would sample it.
    if (tme && type != PRIM_POINT)
    {
        texel = gsSampleTexture(ts, is, it, iq, iu, iv);
        const uint c = gsCombineTexture(ts, r, g, b, a, texel);
        r = c & 0xFFu; g = (c >> 8u) & 0xFFu; b = (c >> 16u) & 0xFFu; a = c >> 24u;
    }

    GsPixelState s;
    s.fbp = pu(id, H_FBP); s.fbw = pu(id, H_FBW); s.fpsm = pu(id, H_FPSM); s.fbmsk = pu(id, H_FBMSK);
    s.zbp = pu(id, H_ZBP); s.zpsm = pu(id, H_ZPSM); s.zmask = (flags & F_ZMASK) != 0u ? 1u : 0u;
    s.test = pu(id, H_TEST); s.alphaLo = pu(id, H_ALPHA); s.fix = pu(id, H_FIX); s.fba = pu(id, H_FBA);
    s.sx0 = pi(id, H_SX0); s.sy0 = pi(id, H_SY0); s.sx1 = pi(id, H_SX1); s.sy1 = pi(id, H_SY1);
    s.abe = (flags & F_ABE) != 0u ? 1u : 0u; s.fge = (flags & F_FGE) != 0u ? 1u : 0u; s.pabe = (flags & F_PABE) != 0u ? 1u : 0u;
    s.fogR = pu(id, H_FOGR); s.fogG = pu(id, H_FOGG); s.fogB = pu(id, H_FOGB);

    beginInvocationInterlockARB();
    gsWritePixel(s, x, y, z, r, g, b, a, fog);
    endInvocationInterlockARB();
}
