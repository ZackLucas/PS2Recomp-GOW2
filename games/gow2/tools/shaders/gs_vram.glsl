// GS local-memory addressing in GLSL. The VRAM is a buffer of uint (4 MiB).
// Mirrors GSMem::PixelStorageTraits<>::{PageId,Address,Read} from ps2_gs_memory.h exactly.
//
// Requires: `buffer Vram { uint vram[]; }` declared by the includer and gs_vram_tables.glsl.

const uint GS_PSM_CT32 = 0x00u, GS_PSM_CT24 = 0x01u, GS_PSM_CT16 = 0x02u, GS_PSM_CT16S = 0x0Au,
           GS_PSM_T8 = 0x13u, GS_PSM_T4 = 0x14u, GS_PSM_T8H = 0x1Bu, GS_PSM_T4HL = 0x24u, GS_PSM_T4HH = 0x2Cu,
           GS_PSM_Z32 = 0x30u, GS_PSM_Z24 = 0x31u, GS_PSM_Z16 = 0x32u, GS_PSM_Z16S = 0x3Au;

// Bits per pixel in the unpacked in-memory form (GSMem::UnpackedBitWidth).
uint gsUnpackedBits(uint psm)
{
    switch (psm)
    {
    case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S: return 16u;
    case GS_PSM_T8: return 8u;
    case GS_PSM_T4: return 4u;
    default: return 32u;
    }
}

// Bit offset inside the 32-bit word for the "H" formats.
uint gsBitOffset(uint psm)
{
    switch (psm)
    {
    case GS_PSM_T8H: case GS_PSM_T4HL: return 24u;
    case GS_PSM_T4HH: return 28u;
    default: return 0u;
    }
}

// Pixel index (in units of "unpacked pixels") for a block base, buffer width (in 64 px units), x, y.
uint gsPixelIndex(uint psm, uint bp, uint bw, uint x, uint y)
{
    uint pageW, pageH, blockCols, blockRows, colW, colH, ppb;
    switch (psm)
    {
    case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S:
        pageW = 64u; pageH = 64u; blockCols = 4u; blockRows = 8u; colW = 16u; colH = 8u; ppb = 128u; break;
    case GS_PSM_T8:
        pageW = 128u; pageH = 64u; blockCols = 8u; blockRows = 4u; colW = 16u; colH = 16u; ppb = 256u; break;
    case GS_PSM_T4:
        pageW = 128u; pageH = 128u; blockCols = 4u; blockRows = 8u; colW = 32u; colH = 16u; ppb = 512u; break;
    default: // 32-bit layouts: CT32, CT24, Z32, Z24, T8H, T4HL, T4HH
        pageW = 64u; pageH = 32u; blockCols = 8u; blockRows = 4u; colW = 8u; colH = 8u; ppb = 64u; break;
    }

    const uint page = (bp / 32u) + (y / pageH) * ((bw * 64u) / pageW) + (x / pageW);
    const uint block = bp % 32u;
    const uint xi = x % pageW, yi = y % pageH;
    const uint bIdx = ((yi / colH) % blockRows) * blockCols + ((xi / colW) % blockCols);
    const uint cIdx = (yi % colH) * colW + (xi % colW);

    uint blockId, columnId;
    switch (psm)
    {
    case GS_PSM_Z32: case GS_PSM_Z24: blockId = BlockTableZ32[bIdx]; columnId = ColumnTable32[cIdx]; break;
    case GS_PSM_CT16: blockId = BlockTableC16[bIdx]; columnId = ColumnTable16[cIdx]; break;
    case GS_PSM_CT16S: blockId = BlockTableC16S[bIdx]; columnId = ColumnTable16[cIdx]; break;
    case GS_PSM_Z16: blockId = BlockTableZ16[bIdx]; columnId = ColumnTable16[cIdx]; break;
    case GS_PSM_Z16S: blockId = BlockTableZ16S[bIdx]; columnId = ColumnTable16[cIdx]; break;
    case GS_PSM_T8: blockId = BlockTableP8[bIdx]; columnId = ColumnTable8[cIdx]; break;
    case GS_PSM_T4: blockId = BlockTableP4[bIdx]; columnId = ColumnTable4[cIdx]; break;
    default: blockId = BlockTableC32[bIdx]; columnId = ColumnTable32[cIdx]; break;
    }
    return page * (pageW * pageH) + (block + blockId) * ppb + columnId;
}

// Equivalent of GSMem::PixelStorageTraits<psm>::Read: returns the PackedT value zero-extended.
uint gsReadRaw(uint psm, uint bp, uint bw, uint x, uint y)
{
    const uint pixel = gsPixelIndex(psm, bp, bw, x, y);
    const uint bits = pixel * gsUnpackedBits(psm) + gsBitOffset(psm);
    uint byteAddr = bits / 8u;
    const uint shift = bits % 8u;
    const uint kMemMask = 0x3FFFFFu; // MEMORY_SIZE - 1

    switch (psm)
    {
    case GS_PSM_CT32: case GS_PSM_Z32:
        return vram[((byteAddr & (kMemMask - 3u)) >> 2u)];
    case GS_PSM_CT24: case GS_PSM_Z24:
        return vram[((byteAddr & (kMemMask - 3u)) >> 2u)] & 0x00FFFFFFu;
    case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S:
    {
        byteAddr &= (kMemMask - 1u);
        return (vram[byteAddr >> 2u] >> ((byteAddr & 2u) * 8u)) & 0xFFFFu;
    }
    case GS_PSM_T8: case GS_PSM_T8H:
    {
        byteAddr &= kMemMask;
        return (vram[byteAddr >> 2u] >> ((byteAddr & 3u) * 8u)) & 0xFFu;
    }
    case GS_PSM_T4: case GS_PSM_T4HL: case GS_PSM_T4HH:
    {
        byteAddr &= kMemMask;
        return ((vram[byteAddr >> 2u] >> ((byteAddr & 3u) * 8u)) & 0xFFu) >> shift & 0x0Fu;
    }
    }
    return 0xFFFF00FFu;
}

// Equivalent of GSMem::PixelStorageTraits<psm>::Write. Sub-word writes use atomicAnd/atomicOr on
// the disjoint bit range of this pixel, so concurrent writers to *different* pixels of one word
// (T4/T8/16-bit/T8H) are safe. Requires a writable `vram` buffer.
void gsWriteRaw(uint psm, uint bp, uint bw, uint x, uint y, uint value)
{
    const uint pixel = gsPixelIndex(psm, bp, bw, x, y);
    const uint bits = pixel * gsUnpackedBits(psm) + gsBitOffset(psm);
    uint byteAddr = bits / 8u;
    const uint shift = bits % 8u;
    const uint kMemMask = 0x3FFFFFu;

    switch (psm)
    {
    case GS_PSM_CT32: case GS_PSM_Z32:
        vram[(byteAddr & (kMemMask - 3u)) >> 2u] = value;
        return;
    case GS_PSM_CT24: case GS_PSM_Z24:
    {
        const uint w = (byteAddr & (kMemMask - 3u)) >> 2u;
        atomicAnd(vram[w], 0xFF000000u);
        atomicOr(vram[w], value & 0x00FFFFFFu);
        return;
    }
    case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S:
    {
        byteAddr &= (kMemMask - 1u);
        const uint sh = (byteAddr & 2u) * 8u;
        const uint w = byteAddr >> 2u;
        atomicAnd(vram[w], ~(0xFFFFu << sh));
        atomicOr(vram[w], (value & 0xFFFFu) << sh);
        return;
    }
    case GS_PSM_T8: case GS_PSM_T8H:
    {
        byteAddr &= kMemMask;
        const uint sh = (byteAddr & 3u) * 8u;
        const uint w = byteAddr >> 2u;
        atomicAnd(vram[w], ~(0xFFu << sh));
        atomicOr(vram[w], (value & 0xFFu) << sh);
        return;
    }
    case GS_PSM_T4: case GS_PSM_T4HL: case GS_PSM_T4HH:
    {
        byteAddr &= kMemMask;
        const uint sh = (byteAddr & 3u) * 8u + shift;
        const uint w = byteAddr >> 2u;
        atomicAnd(vram[w], ~(0xFu << sh));
        atomicOr(vram[w], (value & 0xFu) << sh);
        return;
    }
    }
}
