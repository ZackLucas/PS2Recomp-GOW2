// Whole-program check: a VU1 microprogram translated by vu1_gen against VU1Interpreter on the same inputs.
// Inputs: the image the translation was made from, real VU1 data memory taken from PCSX2 savestates, entry points
// 0/3/6 (PC 0/24/48 seen in the game), randomized registers. Compared: data memory, registers, XGKICK packets.
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#define VU1N_TRACE_PAIRS 1
#include "runtime/ps2_vu1_native.h"

#include <cfenv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

extern "C" int32_t vu1_prog_test(vu1n::State &s, uint32_t entry);

static std::vector<uint8_t> readFile(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Trace { std::vector<uint32_t> pairs; };
static void nativeTrace(void *c, uint32_t pair, const vu1n::State *) { static_cast<Trace *>(c)->pairs.push_back(pair); }
static void interpTrace(void *c, uint32_t pc, const VU1State &) { static_cast<Trace *>(c)->pairs.push_back(pc / 8); }

struct Packets
{
    std::vector<std::vector<uint8_t>> list;
};
static void sink(void *ctx, const uint8_t *p, uint32_t n) { static_cast<Packets *>(ctx)->list.emplace_back(p, p + n); }

int main(int argc, char **argv)
{
    if (argc < 3) { std::puts("usage: vu1_prog_test image.bin in_*.bin ..."); return 2; }
    const std::vector<uint8_t> image = readFile(argv[1]);
    PS2Memory mem;
    if (!mem.initialize()) return 2;
    GS gs;
    gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
    std::vector<uint8_t> code(PS2_VU1_CODE_SIZE, 0), data(PS2_VU1_DATA_SIZE), nmem(PS2_VU1_DATA_SIZE);
    std::memcpy(code.data(), image.data(), std::min<size_t>(image.size(), code.size()));
    VU1Interpreter vu;
    int cases = 0, noncomparable = 0, equal = 0, badMem = 0, badRegs = 0, badPkt = 0, nativeExit = 0, interpBudget = 0;
    long pktsTotal = 0;
    double benchInterp = 0, benchNative = 0;
    long benchCalls = 0;
    const int kBudget = 65536;
    for (int ai = 2; ai < argc; ++ai)
    {
        const std::vector<uint8_t> in = readFile(argv[ai]);
        uint32_t hdr[5];
        if (in.size() < sizeof(hdr)) continue;
        std::memcpy(hdr, in.data(), sizeof(hdr));
        if (hdr[0] != 0x4E495556u || hdr[4] != sizeof(VU1State) || in.size() != sizeof(hdr) + sizeof(VU1State) + PS2_VU1_DATA_SIZE) { std::printf("skip %s (format)\n", argv[ai]); continue; }
        const uint32_t startPC = hdr[1], top = hdr[2], itop = hdr[3];
        VU1State init;
        std::memcpy(&init, in.data() + sizeof(hdr), sizeof(init));
        std::memcpy(data.data(), in.data() + sizeof(hdr) + sizeof(init), data.size());
        std::memcpy(nmem.data(), data.data(), data.size());

        vu1n::State a{};
        std::memcpy(a.vf, init.vf, sizeof(a.vf)); std::memcpy(a.acc, init.acc, 16);
        a.q = init.q; a.p = init.p; a.i = init.i; a.r = init.r; a.mac = init.mac; a.status = init.status; a.clip = init.clip; a.workingClip = init.clip;
        for (int r = 0; r < 16; ++r) a.vi[r] = init.vi[r];
        a.top = top; a.itop = itop; a.mem = nmem.data(); a.memSize = PS2_VU1_DATA_SIZE; a.budget = kBudget;
        Packets pn, pi;
        Trace tn, ti;
        a.sink = sink; a.sinkCtx = &pn; a.trace = nativeTrace; a.traceCtx = &tn;

        vu.reset();
        vu.setXgkickSink(sink, &pi);
        vu.setPairTrace(interpTrace, &ti);
        std::memcpy(&vu.state(), &init, sizeof(init));
        VU1State &st = vu.state();
        vu.execute(code.data(), PS2_VU1_CODE_SIZE, data.data(), PS2_VU1_DATA_SIZE, gs, nullptr, startPC, top, itop, kBudget);
        const bool interpEnded = st.cycles < static_cast<uint64_t>(kBudget);

        const int prev = std::fegetround();
        std::fesetround(FE_TOWARDZERO);
        const int32_t rc = vu1_prog_test(a, startPC / 8);
        std::fesetround(prev);

        if (getenv("VU1_BENCH") && interpEnded && rc == -1)
        {
            const int reps = atoi(getenv("VU1_BENCH"));
            vu.setPairTrace(nullptr, nullptr);
            vu.setXgkickSink([](void *, const uint8_t *, uint32_t) {}, nullptr);
            auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < reps; ++k)
            {
                std::memcpy(data.data(), in.data() + sizeof(hdr) + sizeof(init), data.size());
                std::memcpy(&vu.state(), &init, sizeof(init));
                vu.execute(code.data(), PS2_VU1_CODE_SIZE, data.data(), PS2_VU1_DATA_SIZE, gs, nullptr, startPC, top, itop, kBudget);
            }
            auto t1 = std::chrono::steady_clock::now();
            a.trace = nullptr; a.sink = [](void *, const uint8_t *, uint32_t) {};
            const int prev2 = std::fegetround();
            std::fesetround(FE_TOWARDZERO);
            for (int k = 0; k < reps; ++k)
            {
                std::memcpy(nmem.data(), in.data() + sizeof(hdr) + sizeof(init), nmem.size());
                vu1_prog_test(a, startPC / 8);
            }
            std::fesetround(prev2);
            auto t2 = std::chrono::steady_clock::now();
            benchInterp += std::chrono::duration<double>(t1 - t0).count();
            benchNative += std::chrono::duration<double>(t2 - t1).count();
            benchCalls += reps;
        }
        ++cases;
        if (getenv("VU1_TRACE") && interpEnded && rc == -1)
        {
            size_t k = 0;
            while (k < tn.pairs.size() && k < ti.pairs.size() && tn.pairs[k] == ti.pairs[k]) ++k;
            if (k < tn.pairs.size() || k < ti.pairs.size())
            {
                std::printf("TRACE %s: sequences differ at step %zu (native %zu pairs, interp %zu pairs)\n   ...", argv[ai], k, tn.pairs.size(), ti.pairs.size());
                for (size_t q = (k > 6 ? k - 6 : 0); q < k + 4; ++q) std::printf(" [%zu] n=%d i=%d |", q, q < tn.pairs.size() ? (int)tn.pairs[q] : -1, q < ti.pairs.size() ? (int)ti.pairs[q] : -1);
                std::printf("\n");
            }
            else std::printf("TRACE %s: sequences identical (%zu pairs)\n", argv[ai], tn.pairs.size());
        }
        if (!interpEnded) { ++interpBudget; ++noncomparable; continue; }
        if (rc != -1) { ++nativeExit; ++noncomparable; continue; }
        const bool memOk = std::memcmp(nmem.data(), data.data(), data.size()) == 0;
        bool regsOk = std::memcmp(a.vf, st.vf, sizeof(a.vf)) == 0 && std::memcmp(a.acc, st.acc, 16) == 0;
        for (int r = 0; r < 16; ++r) regsOk = regsOk && a.vi[r] == st.vi[r];
        const bool pktOk = pn.list == pi.list;
        pktsTotal += static_cast<long>(pi.list.size());
        badMem += !memOk; badRegs += !regsOk; badPkt += !pktOk;
        equal += memOk && regsOk && pktOk;
        if (getenv("VU1_DETAIL") && !(memOk && regsOk && pktOk) && cases <= 3)
        {
            int dm = 0, firstm = -1;
            for (size_t k = 0; k < data.size(); ++k) if (nmem[k] != data[k]) { ++dm; if (firstm < 0) firstm = static_cast<int>(k); }
            std::printf("DETAIL %s: %d bytes of memory differ (first at 0x%x)\n", argv[ai], dm, firstm);
            for (int r = 0; r < 32; ++r) if (std::memcmp(a.vf[r], st.vf[r], 16)) std::printf("   vf%d native %g %g %g %g | interp %g %g %g %g\n", r, a.vf[r][0], a.vf[r][1], a.vf[r][2], a.vf[r][3], st.vf[r][0], st.vf[r][1], st.vf[r][2], st.vf[r][3]);
            for (int r = 0; r < 16; ++r) if (a.vi[r] != st.vi[r]) std::printf("   vi%d native %d | interp %d\n", r, a.vi[r], st.vi[r]);
            std::printf("   packets native %zu bytes:", pn.list.size()); for (auto &q : pn.list) std::printf(" %zu", q.size());
            std::printf(" | interp:"); for (auto &q : pi.list) std::printf(" %zu", q.size());
            std::printf("\n");
        }
        if (getenv("VU1_VERBOSE") && !(memOk && regsOk && pktOk))
            std::printf("  %s pc=%u: mem %s regs %s packets %s (%zu vs %zu)\n", argv[ai], startPC, memOk ? "ok" : "DIFF", regsOk ? "ok" : "DIFF", pktOk ? "ok" : "DIFF", pn.list.size(), pi.list.size());
    }
    const int comparable = cases - noncomparable;
    std::printf("cases %d: comparable %d (interp hit budget %d, native left translated area %d), %ld packets\n", cases, comparable, interpBudget, nativeExit, pktsTotal);
    std::printf("  identical (memory + registers + packets): %d  (%.1f%%)\n", equal, comparable ? 100.0 * equal / comparable : 0.0);
    std::printf("  mismatches: memory %d, registers %d, XGKICK packets %d\n", badMem, badRegs, badPkt);
    if (benchCalls) std::printf("BENCH %ld calls: interpreter %.2f us/call, native %.2f us/call (x%.1f faster)\n", benchCalls, 1e6 * benchInterp / benchCalls, 1e6 * benchNative / benchCalls, benchInterp / benchNative);
    return 0;
}
