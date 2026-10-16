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

// Compare mode: after every pair, find the qwords our run just wrote and check them against the final PCSX2 memory.
struct Compare
{
    const uint8_t *data = nullptr;
    std::vector<uint8_t> px, prev;
    uint32_t lastPair = 0, lastPair2 = 0;
    int reported = 0;
    uint32_t lastWriter[1024] = {}, lastWriter2[1024] = {};
};
static void compareTrace(void *c, uint32_t pc, const VU1State &st)
{
    Compare *k = static_cast<Compare *>(c);
    for (int q = 0; q < 1024; ++q)
    {
        if (std::memcmp(k->data + q * 16, &k->prev[q * 16], 16) == 0) continue;
        std::memcpy(&k->prev[q * 16], k->data + q * 16, 16);
        k->lastWriter[q] = k->lastPair2; // the store committed one cycle after its pair, so one or two pairs back
        k->lastWriter2[q] = k->lastPair;
    }
    k->lastPair2 = k->lastPair;
    k->lastPair = pc / 8;
    (void)st;
}

struct Watch
{
    const uint8_t *data = nullptr;
    int qword = -1;
    uint8_t last[16] = {};
    std::vector<uint32_t> recent;
    bool done = false;
};
static void watchTrace(void *c, uint32_t pc, const VU1State &st)
{
    Watch *w = static_cast<Watch *>(c);
    if (const char *dp = getenv("VU1_DUMPAT"))
        if (pc / 8 == static_cast<uint32_t>(atoi(dp)))
        {
            std::printf("--- state at pair %u (before it executes) ---\n", pc / 8);
            for (int r : {1, 2, 3, 5, 19, 26, 28})
                std::printf("vf%-2d = %g %g %g %g   (%08x %08x %08x %08x)\n", r, st.vf[r][0], st.vf[r][1], st.vf[r][2], st.vf[r][3],
                            *(const uint32_t *)&st.vf[r][0], *(const uint32_t *)&st.vf[r][1], *(const uint32_t *)&st.vf[r][2], *(const uint32_t *)&st.vf[r][3]);
            std::printf("q=%g status=%x mac=%x\n", st.q, st.status, st.mac);
        }
    if (w->qword < 0 || w->done) return;
    if (std::memcmp(w->data + w->qword * 16, w->last, 16) != 0)
    {
        std::printf("qword %d changed; pairs executed just before (oldest first):", w->qword);
        for (uint32_t r : w->recent) std::printf(" %u", r);
        std::printf("  (now at pair %u)\n", pc / 8);
        float v[4], f[4];
        std::memcpy(v, w->data + w->qword * 16, 16);
        std::printf("   new value: %g %g %g %g\n", v[0], v[1], v[2], v[3]);
        std::printf("   q=%g p=%g i=%g acc=(%g %g %g %g)\n", st.q, st.p, st.i, st.acc[0], st.acc[1], st.acc[2], st.acc[3]);
        (void)f;
        w->done = true;
    }
    w->recent.push_back(pc / 8);
    if (w->recent.size() > 6) w->recent.erase(w->recent.begin());
}

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
    Watch watch;
    Compare cmp;
    if (const char *pxPath = getenv("VU1_COMPARE"))
    {
        cmp.px = readFile(pxPath);
        cmp.data = data.data();
        cmp.prev = data;
        vu.setPairTrace(compareTrace, &cmp);
    }
    else if (getenv("VU1_WATCH") || getenv("VU1_DUMPAT"))
    {
        const char *wq = getenv("VU1_WATCH") ? getenv("VU1_WATCH") : "-1";
        watch.data = data.data();
        watch.qword = atoi(wq);
        std::memcpy(watch.last, data.data() + watch.qword * 16, 16);
        vu.setPairTrace(watchTrace, &watch);
    }
    vu.execute(code.data(), PS2_VU1_CODE_SIZE, data.data(), PS2_VU1_DATA_SIZE, gs, nullptr, hdr[1], hdr[2], hdr[3], 65536);
    if (!cmp.px.empty())
        for (int q = 0; q < 1024; ++q)
            if (std::memcmp(data.data() + q * 16, &cmp.px[q * 16], 16) != 0)
            {
                float a[4], b[4];
                std::memcpy(a, data.data() + q * 16, 16); std::memcpy(b, &cmp.px[q * 16], 16);
                std::printf("final qword %4d differs (last written by pair %u/%u): ours %g %g %g %g | PCSX2 %g %g %g %g\n", q, cmp.lastWriter[q], cmp.lastWriter2[q], a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
            }
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
