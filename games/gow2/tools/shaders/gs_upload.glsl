// Host -> local image transfer (GIF IMAGE data). One invocation per destination pixel.
// Mirrors GSCpuBackend::UploadImage for a transfer whose data arrives as one byte stream.
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Vram { uint vram[]; };
layout(std430, binding = 1) readonly buffer Src { uint src[]; };

uniform uint uDbp, uDbw, uPsm, uDsax, uDsay, uRrw;
uniform uint uStartPixel;   // pixel index of src stream element 0 within the transfer
uniform uint uCount;        // number of pixels carried by this buffer

uint srcByte(uint i) { return (src[i >> 2u] >> ((i & 3u) * 8u)) & 0xFFu; }

#include_gs_vram_here

void main()
{
    const uint p = gl_GlobalInvocationID.x;
    if (p >= uCount) return;

    uint value;
    switch (uPsm)
    {
    case GS_PSM_CT32: case GS_PSM_Z32:
        value = src[p]; break;
    case GS_PSM_CT24: case GS_PSM_Z24:
        value = srcByte(3u * p) | (srcByte(3u * p + 1u) << 8u) | (srcByte(3u * p + 2u) << 16u); break;
    case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S:
        value = (src[p >> 1u] >> ((p & 1u) * 16u)) & 0xFFFFu; break;
    case GS_PSM_T8: case GS_PSM_T8H:
        value = srcByte(p); break;
    case GS_PSM_T4: case GS_PSM_T4HL: case GS_PSM_T4HH:
        value = (srcByte(p >> 1u) >> ((p & 1u) * 4u)) & 0xFu; break;
    default:
        return;
    }

    const uint pix = uStartPixel + p;
    gsWriteRaw(uPsm, uDbp, max(uDbw, 1u), uDsax + (pix % uRrw), uDsay + (pix / uRrw), value);
}
