// Shared by the draw vertex and fragment shaders: primitive records and bounds.
// A primitive is one triangle / sprite / point with the full draw state, laid out to match
// GpuPrim in gs_prim_pack.h (std430).

struct Vtx
{
    float x, y;          // pixel coordinates (already /16)
    double z;
    uint rgba;           // r | g<<8 | b<<16 | a<<24
    uint uv;             // u | v<<16 (12.4 fixed)
    float s, t, q;
    uint fog;
};

struct Prim
{
    uint h[48];
    Vtx v[3];
};

// Correctly rounded float division. GLSL allows 2.5 ULP on `/`, and NVIDIA's differs from IEEE in
// ~15% of cases; the CPU rasterizer uses IEEE division, so bit-exact results need this. Computing the
// quotient in double and rounding once to float is exact (53 >= 2*24+2 bits).
float gsDiv(float a, float b) { return float(double(a) / double(b)); }

layout(std430, binding = 5) readonly buffer Prims { Prim prims[]; };

const uint H_FLAGS = 0u, H_TYPE = 1u, H_FBP = 2u, H_FBW = 3u, H_FPSM = 4u, H_FBMSK = 5u, H_ZBP = 6u, H_ZPSM = 7u,
           H_TEST = 8u, H_ALPHA = 9u, H_FIX = 10u, H_FBA = 11u, H_SX0 = 12u, H_SY0 = 13u, H_SX1 = 14u, H_SY1 = 15u,
           H_OFX = 16u, H_OFY = 17u, H_TBP0 = 18u, H_TBW = 19u, H_TPSM = 20u, H_TW = 21u, H_TH = 22u, H_TCC = 23u,
           H_TFX = 24u, H_CBP = 25u, H_CPSM = 26u, H_CSM = 27u, H_CSA = 28u, H_WRAPU = 29u, H_WRAPV = 30u,
           H_MINU = 31u, H_MAXU = 32u, H_MINV = 33u, H_MAXV = 34u, H_TA0 = 35u, H_AEM = 36u, H_TA1 = 37u,
           H_FOGR = 38u, H_FOGG = 39u, H_FOGB = 40u;
const uint F_IIP = 1u, F_TME = 2u, F_FGE = 4u, F_ABE = 8u, F_FST = 16u, F_PABE = 32u, F_LINEAR = 64u, F_ZMASK = 128u;
const uint PRIM_POINT = 0u, PRIM_LINE = 1u, PRIM_LINESTRIP = 2u, PRIM_TRIANGLE = 3u, PRIM_TRISTRIP = 4u,
           PRIM_TRIFAN = 5u, PRIM_SPRITE = 6u;

uint pu(uint id, uint field) { return prims[id].h[field]; }
int pi(uint id, uint field) { return int(prims[id].h[field]); }

// Inclusive pixel bounds the primitive can touch (x1 < x0 means "nothing"), exactly as the CPU
// rasterizer derives them in DrawTriangle / DrawSprite / the point path.
ivec4 gsPrimBounds(uint id)
{
    const uint type = pu(id, H_TYPE);
    const int ofx = pi(id, H_OFX), ofy = pi(id, H_OFY);
    const int sx0 = pi(id, H_SX0), sy0 = pi(id, H_SY0), sx1 = pi(id, H_SX1), sy1 = pi(id, H_SY1);
    const Vtx a = prims[id].v[0], b = prims[id].v[1], c = prims[id].v[2];

    if (type == PRIM_TRIANGLE || type == PRIM_TRISTRIP || type == PRIM_TRIFAN)
    {
        const float fx0 = a.x - float(ofx), fx1 = b.x - float(ofx), fx2 = c.x - float(ofx);
        const float fy0 = a.y - float(ofy), fy1 = b.y - float(ofy), fy2 = c.y - float(ofy);
        int minX = int(floor(min(fx0, min(fx1, fx2)))), maxX = int(ceil(max(fx0, max(fx1, fx2))));
        int minY = int(floor(min(fy0, min(fy1, fy2)))), maxY = int(ceil(max(fy0, max(fy1, fy2))));
        return ivec4(clamp(minX, sx0, sx1), clamp(minY, sy0, sy1), clamp(maxX, sx0, sx1), clamp(maxY, sy0, sy1));
    }
    if (type == PRIM_SPRITE)
    {
        int x0 = int(a.x) - ofx, y0 = int(a.y) - ofy, x1 = int(b.x) - ofx, y1 = int(b.y) - ofy;
        if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { const int t = y0; y0 = y1; y1 = t; }
        const int ux1 = x0 + max(1, x1 - x0) - 1, uy1 = y0 + max(1, y1 - y0) - 1;
        if (ux1 < sx0 || x0 > sx1 || uy1 < sy0 || y0 > sy1) return ivec4(1, 1, 0, 0);
        return ivec4(clamp(x0, sx0, sx1), clamp(y0, sy0, sy1), clamp(ux1, sx0, sx1), clamp(uy1, sy0, sy1));
    }
    if (type == PRIM_POINT)
    {
        const int px = int(a.x) - ofx, py = int(a.y) - ofy;
        if (px < 0 || py < 0 || px > 2047 || py > 2047) return ivec4(1, 1, 0, 0);
        return ivec4(px, py, px, py);
    }
    return ivec4(1, 1, 0, 0);   // lines are not handled on the GPU yet
}
