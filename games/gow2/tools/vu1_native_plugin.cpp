// Plugin entry for the native VU1 segments: the runtime dlopen()s this library (PS2_VU1_NATIVE=<path>) and asks
// it for the translated function of each code segment. The segments themselves are game-derived and built from
// GOW2_VU1_GEN_DIR, outside the repository.
#include "runtime/ps2_vu1_native.h"

extern "C" vu1n::SegFn vu1_native_find_segment(uint32_t seg, uint64_t hash)
{
    return vu1n::findSegment(seg, hash);
}
