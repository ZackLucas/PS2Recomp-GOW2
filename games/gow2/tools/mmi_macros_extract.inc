// R5900 MMI shuffles (rd.x = rt.y notation, index 0 = lowest). Verified against the spec in games/gow2/tools/mmi_test.cpp.
//   PEXEH: halfwords 0<->2 and 4<->6      PEXEW: words 0<->2      PEXCH: halfwords 1<->2 and 5<->6
//   PEXCW: words 1<->2                     PROT3W: {w1,w2,w0,w3}  PREVH: reverse halfwords inside each 64 bits
#define PS2_PEXEH(rs) _mm_shufflelo_epi16(_mm_shufflehi_epi16(rs, _MM_SHUFFLE(3, 0, 1, 2)), _MM_SHUFFLE(3, 0, 1, 2))
#define PS2_PEXCH(rs) _mm_shufflelo_epi16(_mm_shufflehi_epi16(rs, _MM_SHUFFLE(3, 1, 2, 0)), _MM_SHUFFLE(3, 1, 2, 0))
#define PS2_PEXEW(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(3, 0, 1, 2))
#define PS2_PEXCW(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(3, 1, 2, 0))
#define PS2_PROT3W(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(3, 0, 2, 1))
#define PS2_PREVH(rs) _mm_shufflelo_epi16(_mm_shufflehi_epi16(rs, _MM_SHUFFLE(0, 1, 2, 3)), _MM_SHUFFLE(0, 1, 2, 3))
