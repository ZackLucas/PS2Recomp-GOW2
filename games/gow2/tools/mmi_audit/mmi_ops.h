// MMI audit: list of the instructions under test (shared by the generator and the checker).
// Encoding: opcode 0x1C, rs=2, rt=3, rd=4, sa = sub-function (MMI0..3, PMFHL, PMTHL) or the shift amount.
#pragma once
#include <cstdint>

struct MmiOp
{
    const char *name;
    uint8_t func; // bits 5-0
    uint8_t sa;   // bits 10-6
    uint8_t rs, rt, rd;
};

#define MMI_OP(n, f, s) {#n, f, s, 2, 3, 4}
#define MMI_OP_RT(n, f, s) {#n, f, s, 0, 3, 4} // "op rd, rt" forms: rs field is 0

static const MmiOp kMmiOps[] = {
    MMI_OP(MADD, 0x00, 0), MMI_OP(MADDU, 0x01, 0), MMI_OP(PLZCW, 0x04, 0),
    MMI_OP(MFHI1, 0x10, 0), MMI_OP(MTHI1, 0x11, 0), MMI_OP(MFLO1, 0x12, 0), MMI_OP(MTLO1, 0x13, 0),
    MMI_OP(MULT1, 0x18, 0), MMI_OP(MULTU1, 0x19, 0), MMI_OP(DIV1, 0x1A, 0), MMI_OP(DIVU1, 0x1B, 0),
    MMI_OP(MADD1, 0x20, 0), MMI_OP(MADDU1, 0x21, 0),
    MMI_OP_RT(PSLLH_5, 0x34, 5), MMI_OP_RT(PSRLH_5, 0x36, 5), MMI_OP_RT(PSRAH_5, 0x37, 5),
    MMI_OP_RT(PSLLH_20, 0x34, 20), MMI_OP_RT(PSRLH_20, 0x36, 20), MMI_OP_RT(PSRAH_20, 0x37, 20),
    MMI_OP_RT(PSLLW_7, 0x3C, 7), MMI_OP_RT(PSRLW_7, 0x3E, 7), MMI_OP_RT(PSRAW_7, 0x3F, 7),
    MMI_OP_RT(PSLLW_31, 0x3C, 31), MMI_OP_RT(PSRLW_31, 0x3E, 31), MMI_OP_RT(PSRAW_31, 0x3F, 31),
    // MMI0 (func 0x08)
    MMI_OP(PADDW, 0x08, 0x00), MMI_OP(PSUBW, 0x08, 0x01), MMI_OP(PCGTW, 0x08, 0x02), MMI_OP(PMAXW, 0x08, 0x03),
    MMI_OP(PADDH, 0x08, 0x04), MMI_OP(PSUBH, 0x08, 0x05), MMI_OP(PCGTH, 0x08, 0x06), MMI_OP(PMAXH, 0x08, 0x07),
    MMI_OP(PADDB, 0x08, 0x08), MMI_OP(PSUBB, 0x08, 0x09), MMI_OP(PCGTB, 0x08, 0x0A),
    MMI_OP(PADDSW, 0x08, 0x10), MMI_OP(PSUBSW, 0x08, 0x11), MMI_OP(PEXTLW, 0x08, 0x12), MMI_OP(PPACW, 0x08, 0x13),
    MMI_OP(PADDSH, 0x08, 0x14), MMI_OP(PSUBSH, 0x08, 0x15), MMI_OP(PEXTLH, 0x08, 0x16), MMI_OP(PPACH, 0x08, 0x17),
    MMI_OP(PADDSB, 0x08, 0x18), MMI_OP(PSUBSB, 0x08, 0x19), MMI_OP(PEXTLB, 0x08, 0x1A), MMI_OP(PPACB, 0x08, 0x1B),
    MMI_OP_RT(PEXT5, 0x08, 0x1E), MMI_OP_RT(PPAC5, 0x08, 0x1F),
    // MMI1 (func 0x28)
    MMI_OP_RT(PABSW, 0x28, 0x01), MMI_OP(PCEQW, 0x28, 0x02), MMI_OP(PMINW, 0x28, 0x03), MMI_OP(PADSBH, 0x28, 0x04),
    MMI_OP_RT(PABSH, 0x28, 0x05), MMI_OP(PCEQH, 0x28, 0x06), MMI_OP(PMINH, 0x28, 0x07), MMI_OP(PCEQB, 0x28, 0x0A),
    MMI_OP(PADDUW, 0x28, 0x10), MMI_OP(PSUBUW, 0x28, 0x11), MMI_OP(PEXTUW, 0x28, 0x12),
    MMI_OP(PADDUH, 0x28, 0x14), MMI_OP(PSUBUH, 0x28, 0x15), MMI_OP(PEXTUH, 0x28, 0x16),
    MMI_OP(PADDUB, 0x28, 0x18), MMI_OP(PSUBUB, 0x28, 0x19), MMI_OP(PEXTUB, 0x28, 0x1A), MMI_OP(QFSRV, 0x28, 0x1B),
    // MMI2 (func 0x09)
    MMI_OP(PMADDW, 0x09, 0x00), MMI_OP(PSLLVW, 0x09, 0x02), MMI_OP(PSRLVW, 0x09, 0x03), MMI_OP(PMSUBW, 0x09, 0x04),
    {"PMFHI", 0x09, 0x08, 0, 0, 4}, {"PMFLO", 0x09, 0x09, 0, 0, 4}, MMI_OP(PINTH, 0x09, 0x0A),
    MMI_OP(PMULTW, 0x09, 0x0C), {"PDIVW", 0x09, 0x0D, 2, 3, 0}, MMI_OP(PCPYLD, 0x09, 0x0E),
    MMI_OP(PMADDH, 0x09, 0x10), MMI_OP(PHMADH, 0x09, 0x11), MMI_OP(PAND, 0x09, 0x12), MMI_OP(PXOR, 0x09, 0x13),
    MMI_OP(PMSUBH, 0x09, 0x14), MMI_OP(PHMSBH, 0x09, 0x15), MMI_OP_RT(PEXEH, 0x09, 0x1A), MMI_OP_RT(PREVH, 0x09, 0x1B),
    MMI_OP(PMULTH, 0x09, 0x1C), {"PDIVBW", 0x09, 0x1D, 2, 3, 0}, MMI_OP_RT(PEXEW, 0x09, 0x1E), MMI_OP_RT(PROT3W, 0x09, 0x1F),
    // MMI3 (func 0x29)
    MMI_OP(PMADDUW, 0x29, 0x00), MMI_OP(PSRAVW, 0x29, 0x03),
    {"PMTHI", 0x29, 0x08, 2, 0, 0}, {"PMTLO", 0x29, 0x09, 2, 0, 0}, MMI_OP(PINTEH, 0x29, 0x0A),
    MMI_OP(PMULTUW, 0x29, 0x0C), {"PDIVUW", 0x29, 0x0D, 2, 3, 0}, MMI_OP(PCPYUD, 0x29, 0x0E),
    MMI_OP(POR, 0x29, 0x12), MMI_OP(PNOR, 0x29, 0x13), MMI_OP_RT(PEXCH, 0x29, 0x1A), MMI_OP_RT(PCPYH, 0x29, 0x1B),
    MMI_OP_RT(PEXCW, 0x29, 0x1E),
    // PMFHL / PMTHL (func 0x30 / 0x31)
    {"PMFHL_LW", 0x30, 0, 0, 0, 4}, {"PMFHL_UW", 0x30, 1, 0, 0, 4}, {"PMFHL_SLW", 0x30, 2, 0, 0, 4},
    {"PMFHL_LH", 0x30, 3, 0, 0, 4}, {"PMFHL_SH", 0x30, 4, 0, 0, 4}, {"PMTHL_LW", 0x31, 0, 2, 0, 0},
};

static inline uint32_t mmiEncode(const MmiOp &op)
{
    return (0x1Cu << 26) | (uint32_t(op.rs) << 21) | (uint32_t(op.rt) << 16) | (uint32_t(op.rd) << 11) |
           (uint32_t(op.sa) << 6) | op.func;
}
