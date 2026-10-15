// CLUT load (GSCpuBackend::LoadClutUnlocked). One invocation per palette entry.
// The CLD decision logic (whether to load at all) stays on the host: it is control flow.
layout(local_size_x = 256) in;
layout(std430, binding = 0) buffer Vram { uint vram[]; };
layout(std430, binding = 2) buffer Clut { uint clut[]; };

uniform uint uPsm;        // TEX0.PSM (selects 4 or 8 bit indices)
uniform uint uCbp, uCpsm, uCsm, uCsa;
uniform uint uCbw, uCou, uCov;   // TEXCLUT

#include_gs_vram_here

uint swizzleClutIndexCSM1(uint index)
{
    // CSM1 swaps address bits 3 and 4.
    return (index & ~0x18u) | ((index & 0x08u) << 1u) | ((index & 0x10u) >> 1u);
}

void main()
{
    const uint entry = gl_GlobalInvocationID.x;
    const bool fourBit = uPsm == GS_PSM_T4 || uPsm == GS_PSM_T4HL || uPsm == GS_PSM_T4HH;
    const bool sixteen = uCpsm == GS_PSM_CT16 || uCpsm == GS_PSM_CT16S;
    const bool thirtyTwo = uCpsm == GS_PSM_CT32 || uCpsm == GS_PSM_CT24;
    if (!sixteen && !thirtyTwo) return;

    const uint entryCount = fourBit ? 16u : 256u;
    const uint destinationBase = (uCsa & (sixteen ? 0x1Fu : 0x0Fu)) << 4u;
    const bool loadCsm1Suffix = uCsm == 0u && thirtyTwo && !fourBit;
    const uint firstEntry = loadCsm1Suffix ? destinationBase : 0u;
    if (entry < firstEntry || entry >= entryCount) return;

    uint sx, sy, sw = 1u;
    if (uCsm == 0u)
    {
        const uint si = swizzleClutIndexCSM1(entry);
        sx = si & 0x0Fu;
        sy = si >> 4u;
    }
    else
    {
        sw = uCbw != 0u ? uCbw : 1u;
        sx = (uCou << 4u) + entry;
        sy = uCov;
    }

    const uint raw = gsReadRaw(uCpsm, uCbp, sw, sx, sy);
    const uint dest = (loadCsm1Suffix ? entry : destinationBase + entry) & (sixteen ? 0x1FFu : 0x0FFu);
    if (sixteen)
        clut[dest] = raw & 0xFFFFu;
    else
    {
        clut[dest] = raw & 0xFFFFu;
        clut[dest + 256u] = (raw >> 16u) & 0xFFFFu;
    }
}
