// Compares our VU1 (interpreter and translated) with PCSX2 using a PCSX2 savestate: its VU1 code and data memory.
// The data memory holds the input (unpacked vertices) and the last GIF packet PCSX2's VU1 built. We run the program
// from every (entry, TOP, ITOP) combination the game uses and look for the packets we produce, byte for byte, in
// the memory PCSX2 left. A packet found there means our VU1 computed what PCSX2's VU1 computed.
#define VU1N_TRACE_PAIRS 1
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_vu1_native.h"

#include <cfenv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static std::vector<uint8_t> readFile(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
struct Packets { std::vector<std::vector<uint8_t>> list; };
static void sink(void *c, const uint8_t *p, uint32_t n) { static_cast<Packets *>(c)->list.emplace_back(p, p + n); }

// Best qword-aligned alignment of `pkt` inside `mem`: how many 16-byte qwords match, and where.
static void bestAlignment(const std::vector<uint8_t> &mem, const std::vector<uint8_t> &pkt, int &bestOff, int &bestHit, int &firstBad)
{
    const size_t n = mem.size(), q = pkt.size() / 16;
    bestOff = 0; bestHit = -1; firstBad = -1;
    for (size_t off = 0; off < n; off += 16)
    {
        int hit = 0, bad = -1;
        for (size_t k = 0; k < q; ++k)
        {
            bool eq = true;
            for (size_t i = 0; i < 16 && eq; ++i) eq = mem[(off + k * 16 + i) % n] == pkt[k * 16 + i];
            if (eq) ++hit; else if (bad < 0) bad = static_cast<int>(k);
        }
        if (hit > bestHit) { bestHit = hit; bestOff = static_cast<int>(off); firstBad = bad; }
    }
}

// Offset (qword-aligned, wrapping) where `pkt` appears in `mem`, or -1.
static int findPacket(const std::vector<uint8_t> &mem, const std::vector<uint8_t> &pkt)
{
    const size_t n = mem.size();
    for (size_t off = 0; off < n; off += 16)
    {
        bool ok = true;
        for (size_t i = 0; i < pkt.size() && ok; ++i) ok = mem[(off + i) % n] == pkt[i];
        if (ok) return static_cast<int>(off);
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 3) { std::puts("usage: vu1_ss_test micro.bin data.bin [internal.dat vf_offset]"); return 2; }
    const std::vector<uint8_t> image = readFile(argv[1]), data0 = readFile(argv[2]);
    PS2Memory mem;
    if (!mem.initialize()) return 2;
    GS gs;
    gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
    std::vector<uint8_t> code(PS2_VU1_CODE_SIZE, 0), data(PS2_VU1_DATA_SIZE), nmem(PS2_VU1_DATA_SIZE);
    std::memcpy(code.data(), image.data(), std::min(code.size(), image.size()));
    vu1n::SegTable table;
    table.build(code.data());
    int segs = 0;
    for (auto f : table.fn) segs += f != nullptr;
    std::printf("code image: %d of 32 segments have a translation\n", segs);

    // Optional: VU1 registers as PCSX2 had them (VF[32] at vfOff, then VI[32] of 16 bytes each).
    bool haveRegs = false;
    float vf0[32][4] = {};
    int32_t vi0[16] = {};
    if (argc >= 5)
    {
        const std::vector<uint8_t> internal = readFile(argv[3]);
        const size_t vfOff = static_cast<size_t>(std::strtoul(argv[4], nullptr, 0));
        if (internal.size() >= vfOff + 512 + 512)
        {
            std::memcpy(vf0, &internal[vfOff], 512);
            for (int r = 0; r < 16; ++r)
            {
                uint16_t v;
                std::memcpy(&v, &internal[vfOff + 512 + r * 16], 2);
                vi0[r] = static_cast<int16_t>(v);
            }
            haveRegs = true;
            std::printf("registers from %s @%zu (VI12=%d)\n", argv[3], vfOff, vi0[12]);
        }
    }

    VU1Interpreter vu;
    int totalI = 0, foundI = 0, totalN = 0, foundN = 0;
    std::printf("%-26s %-22s %-22s\n", "entry  top   itop", "interpreter (found/pkts)", "native (found/pkts)");
    for (uint32_t pc : {0u, 24u, 48u})
        for (uint32_t top : {0x0u, 0x119u, 0x155u, 0x2abu})
            for (uint32_t itop : {0xf6u, 0x24bu})
            {
                std::memcpy(data.data(), data0.data(), data.size());
                Packets pi;
                vu.reset();
                if (haveRegs)
                {
                    std::memcpy(vu.state().vf, vf0, sizeof(vf0));
                    for (int r = 0; r < 16; ++r) vu.state().vi[r] = vi0[r];
                }
                vu.setXgkickSink(sink, &pi);
                vu.execute(code.data(), PS2_VU1_CODE_SIZE, data.data(), PS2_VU1_DATA_SIZE, gs, nullptr, pc, top, itop, 65536);
                if (getenv("VU1_SS_VERBOSE") && pc == 0 && top == 0x155 && itop == 0xf6)
                {
                    std::printf("   memory ranges our run changed (qword index: pcsx2 vs ours):\n");
                    int start = -1;
                    for (size_t q = 0; q <= data.size() / 16; ++q)
                    {
                        const bool diff = q < data.size() / 16 && std::memcmp(&data[q * 16], &data0[q * 16], 16) != 0;
                        if (diff && start < 0) start = static_cast<int>(q);
                        if (!diff && start >= 0) { std::printf("      qwords %d..%zu (%zu qwords)\n", start, q - 1, q - start); start = -1; }
                    }
                }
                if (getenv("VU1_SS_VERBOSE") && pc == 0 && top == 0x155 && itop == 0xf6)
                {
                    // Feed our own output back in: if the program transforms in place, the second pass differs.
                    std::vector<uint8_t> second = data;
                    Packets p2;
                    VU1Interpreter v2;
                    if (haveRegs) { std::memcpy(v2.state().vf, vf0, sizeof(vf0)); for (int r = 0; r < 16; ++r) v2.state().vi[r] = vi0[r]; }
                    v2.setXgkickSink(sink, &p2);
                    v2.execute(code.data(), PS2_VU1_CODE_SIZE, second.data(), PS2_VU1_DATA_SIZE, gs, nullptr, pc, top, itop, 65536);
                    size_t d2 = 0;
                    for (size_t k = 0; k < second.size(); ++k) d2 += second[k] != data[k];
                    std::printf("   second pass over our own output: %zu bytes differ from the first pass (%s)\n", d2, d2 ? "input is overwritten in place" : "input is not overwritten: idempotent");
                }
                int fi = 0;
                for (auto &p : pi.list)
                {
                    const int off = findPacket(data0, p);
                    fi += off >= 0;
                    if (getenv("VU1_SS_VERBOSE") && pc == 0 && top == 0x155 && itop == 0xf6)
                    {
                        std::printf("   packet %zu bytes: %s (offset 0x%x)\n", p.size(), off >= 0 ? "FOUND" : "not found", off >= 0 ? off : 0);
                        if (off < 0)
                        {
                            int bo, bh, bad;
                            bestAlignment(data0, p, bo, bh, bad);
                            std::printf("      best alignment at 0x%x: %d of %zu qwords identical; first differing qword #%d\n", bo, bh, p.size() / 16, bad);
                            if (bad >= 0)
                            {
                                const uint8_t *a = &p[bad * 16];
                                uint8_t b[16];
                                for (int i = 0; i < 16; ++i) b[i] = data0[(bo + bad * 16 + i) % data0.size()];
                                float fa[4], fb[4];
                                std::memcpy(fa, a, 16); std::memcpy(fb, b, 16);
                                std::printf("      ours   qword: %08x %08x %08x %08x (%g %g %g %g)\n", *(uint32_t *)a, *(uint32_t *)(a + 4), *(uint32_t *)(a + 8), *(uint32_t *)(a + 12), fa[0], fa[1], fa[2], fa[3]);
                                std::printf("      pcsx2  qword: %08x %08x %08x %08x (%g %g %g %g)\n", *(uint32_t *)b, *(uint32_t *)(b + 4), *(uint32_t *)(b + 8), *(uint32_t *)(b + 12), fb[0], fb[1], fb[2], fb[3]);
                            }
                        }
                    }
                }

                int fn = -1, nn = 0;
                if (segs)
                {
                    std::memcpy(nmem.data(), data0.data(), nmem.size());
                    vu1n::State a{};
                    if (haveRegs) { std::memcpy(a.vf, vf0, sizeof(vf0)); for (int r = 0; r < 16; ++r) a.vi[r] = vi0[r]; }
                    else for (int c = 0; c < 4; ++c) a.vf[0][c] = c == 3 ? 1.0f : 0.0f;
                    a.q = 1.0f; a.r = 0x3F800000u; a.top = top; a.itop = itop;
                    a.mem = nmem.data(); a.memSize = PS2_VU1_DATA_SIZE; a.budget = 65536;
                    Packets pn; a.sink = sink; a.sinkCtx = &pn;
                    const int prev = std::fegetround();
                    std::fesetround(FE_TOWARDZERO);
                    const int32_t rc = vu1n::runProgram(a, table, pc / 8);
                    std::fesetround(prev);
                    if (rc == -1) { fn = 0; nn = static_cast<int>(pn.list.size()); for (auto &p : pn.list) fn += findPacket(data0, p) >= 0; }
                }
                char nat[32];
                if (fn < 0) std::snprintf(nat, sizeof nat, "n/a"); else std::snprintf(nat, sizeof nat, "%d/%d", fn, nn);
                std::printf("pc=%-3u top=0x%-4x itop=0x%-4x  %d/%-18zu %s\n", pc, top, itop, fi, pi.list.size(), nat);
                totalI += static_cast<int>(pi.list.size()); foundI += fi;
                if (fn >= 0) { totalN += nn; foundN += fn; }
            }
    std::printf("TOTAL interpreter: %d of %d packets appear verbatim in PCSX2's memory; native: %d of %d\n", foundI, totalI, foundN, totalN);
    return 0;
}
