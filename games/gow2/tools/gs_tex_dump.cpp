// Decodes a PSMT4 texture out of a raw GS VRAM dump (PS2_GS_VRAMDUMP) into a PPM, using the CSM1 CT32 CLUT.
//   gs_tex_dump <vram.bin> <tbp0> <tbw> <w> <h> <cbp> <csa> <out.ppm> [gray]
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/ps2_gs_psmt4.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv)
{
    if (argc < 9) return 1;
    FILE *f = std::fopen(argv[1], "rb");
    std::vector<uint8_t> vram(4u << 20);
    if (!f || std::fread(vram.data(), 1, vram.size(), f) != vram.size()) { std::fprintf(stderr, "bad vram dump\n"); return 1; }
    std::fclose(f);
    uint32_t tbp0 = std::strtoul(argv[2], 0, 0), tbw = std::atoi(argv[3]), w = std::atoi(argv[4]), h = std::atoi(argv[5]);
    uint32_t cbp = std::strtoul(argv[6], 0, 0), csa = std::atoi(argv[7]);
    uint32_t clut[16];
    for (uint32_t i = 0; i < 16; ++i)
    {
        uint32_t idx = csa * 16 + i;  // 32-bit CLUT, CSM1: 8 wide x 8 high of CT32 pixels
        uint32_t a = GSPSMCT32::addrPSMCT32(cbp, 1, idx & 7, idx >> 3);
        clut[i] = *reinterpret_cast<uint32_t *>(&vram[a]);
    }
    const bool gray = argc > 9;
    std::vector<uint8_t> img(w * h * 3);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            uint32_t n = GSPSMT4::addrPSMT4(tbp0, tbw, x, y);
            uint8_t b = vram[n >> 1];
            uint32_t ix = (n & 1) ? (b >> 4) : (b & 15);
            uint32_t c = gray ? (ix * 17u) * 0x010101u : clut[ix];
            uint8_t *p = &img[(y * w + x) * 3];
            p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = (c >> 16) & 255;
        }
    FILE *o = std::fopen(argv[8], "wb");
    std::fprintf(o, "P6\n%u %u\n255\n", w, h);
    std::fwrite(img.data(), 1, img.size(), o);
    std::fclose(o);
}
