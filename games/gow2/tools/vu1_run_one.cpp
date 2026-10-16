// Runs one captured VU1 call (in_<imagehash>_pc<N>_<k>.bin) through our interpreter and, when every segment is
// translated, the native program, and writes the resulting 16 KiB data memory + XGKICK packets for comparison
// with what PCSX2's VU1 produced from the same input.
//   vu1_run_one in.bin out_prefix
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
#include <string>
#include <vector>

static std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
struct Packets { std::vector<std::vector<uint8_t>> list; };
static void sink(void *c, const uint8_t *p, uint32_t n) { static_cast<Packets *>(c)->list.emplace_back(p, p + n); }
static void writeFile(const std::string &path, const void *d, size_t n) { std::ofstream(path, std::ios::binary).write(static_cast<const char *>(d), static_cast<std::streamsize>(n)); }

int main(int argc, char **argv)
{
    if (argc < 3) { std::puts("usage: vu1_run_one in.bin out_prefix"); return 2; }
    const std::string inPath = argv[1], prefix = argv[2];
    const size_t slash = inPath.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "." : inPath.substr(0, slash);
    const std::string base = slash == std::string::npos ? inPath : inPath.substr(slash + 1);
    const std::vector<uint8_t> image = readFile(dir + "/vu1_" + base.substr(3, 16) + ".bin");
    const std::vector<uint8_t> in = readFile(inPath);
    uint32_t hdr[5];
    std::memcpy(hdr, in.data(), sizeof(hdr));
    VU1State init;
    std::memcpy(&init, in.data() + sizeof(hdr), sizeof(init));
    std::vector<uint8_t> data(in.begin() + sizeof(hdr) + sizeof(init), in.end()), nmem = data;

    PS2Memory mem;
    if (!mem.initialize()) return 2;
    GS gs;
    gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
    std::vector<uint8_t> code(PS2_VU1_CODE_SIZE, 0);
    std::memcpy(code.data(), image.data(), code.size());

    VU1Interpreter vu;
    Packets pi;
    std::memcpy(&vu.state(), &init, sizeof(init));
    vu.setXgkickSink(sink, &pi);
    vu.execute(code.data(), PS2_VU1_CODE_SIZE, data.data(), PS2_VU1_DATA_SIZE, gs, nullptr, hdr[1], hdr[2], hdr[3], 65536);
    writeFile(prefix + "_interp_mem.bin", data.data(), data.size());
    size_t off = 0;
    std::vector<uint8_t> flat;
    for (auto &p : pi.list) flat.insert(flat.end(), p.begin(), p.end());
    writeFile(prefix + "_interp_packets.bin", flat.data(), flat.size());
    std::printf("interpreter: %zu packets, %zu bytes\n", pi.list.size(), flat.size());
    (void)off;

    vu1n::SegTable table;
    table.build(code.data());
    vu1n::State a{};
    std::memcpy(a.vf, init.vf, sizeof(a.vf)); std::memcpy(a.acc, init.acc, 16);
    a.q = init.q; a.p = init.p; a.i = init.i; a.r = init.r; a.mac = init.mac; a.status = init.status; a.clip = init.clip; a.workingClip = init.clip;
    for (int r = 0; r < 16; ++r) a.vi[r] = init.vi[r];
    a.top = hdr[2]; a.itop = hdr[3]; a.mem = nmem.data(); a.memSize = PS2_VU1_DATA_SIZE; a.budget = 65536;
    Packets pn; a.sink = sink; a.sinkCtx = &pn;
    const int prev = std::fegetround();
    std::fesetround(FE_TOWARDZERO);
    const int32_t rc = vu1n::runProgram(a, table, hdr[1] / 8);
    std::fesetround(prev);
    if (rc == -1) { writeFile(prefix + "_native_mem.bin", nmem.data(), nmem.size()); std::printf("native: %zu packets\n", pn.list.size()); }
    else std::printf("native: left translated area at pair %d\n", rc);
    return 0;
}
