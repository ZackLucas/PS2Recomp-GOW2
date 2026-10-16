// vu1_gen: translates VU1 micro-memory images into C++ that calls the native instruction semantics in
// runtime/ps2_vu1_native.h. The 2048-pair code memory is split into segments of vu1n::kSegPairs pairs; every distinct
// segment content (window = one pair before, the segment, one pair after) is translated once into its own function,
// so the 142 captured images collapse into ~170 small functions. Branches keep their delay slots, JR/JALR dispatch
// inside the segment or return to the driver, E-bit ends the program, XGKICK hands the packet to the sink.
//
//   vu1_gen <outdir> image.bin [image.bin ...]
//
// Writes <outdir>/seg_<seg>_<hash>.cpp (one function each) and <outdir>/vu1_registry.cpp (findSegment).
// Segment function: int32_t vu1_seg_<seg>_<hash>(vu1n::State &s, uint32_t entryPair)
//   returns -1 when the program ended (E-bit), otherwise the pair index to continue at.
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_vu1_native.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace
{
    constexpr uint32_t kPairs = vu1n::kCodePairs;
    constexpr uint32_t kSeg = vu1n::kSegPairs;
    constexpr uint32_t kPcMask = 0x3FFFu;

    struct Pair
    {
        uint32_t lower = 0, upper = 0;
        VU1Interpreter::PairInfo info;
    };

    int16_t imm11(uint32_t i) { return static_cast<int16_t>(static_cast<int32_t>(i << 21) >> 21); }
    bool isCondBranch(uint32_t op) { return op == 0x28 || op == 0x29 || op == 0x2C || op == 0x2D || op == 0x2E || op == 0x2F; }
    bool isBranchOp(uint32_t op) { return op == 0x20 || op == 0x21 || op == 0x24 || op == 0x25 || isCondBranch(op); }
    bool isXgkick(uint32_t lower)
    {
        if ((lower >> 25) != 0x40 || (lower & 0x3Fu) < 0x3Cu)
            return false;
        return ((lower & 3u) | ((lower >> 4) & 0x7Cu)) == 0x6Cu;
    }

    std::vector<Pair> g_pairs;       // the image being translated
    uint32_t g_lo = 0, g_hi = 0;     // segment [g_lo, g_hi)
    unsigned g_unsupported = 0;

    uint32_t lowerOp(const Pair &p) { return p.info.iBit ? 0xFFFFu : (p.lower >> 25) & 0x7Fu; }

    // VI value a branch reads for register r: the copy saved before the previous pair when that pair wrote it with the
    // one-instruction branch delay, otherwise the live register.
    std::string viRead(uint32_t n, int r)
    {
        if (r == 0)
            return "0";
        if (n > 0 && g_pairs[n - 1].info.delaysNextBranchRead && (g_pairs[n - 1].info.lowerViWrite & (1u << r)))
            return "s.ov[" + std::to_string(r) + "]";
        return "s.vi[" + std::to_string(r) + "]";
    }

    // One instruction pair: wait for operands, run upper then lower (lower sees the pre-pair value of a register the
    // upper unit writes), mark write latencies, advance one cycle.
    void emitBody(std::ostringstream &o, uint32_t n)
    {
        const Pair &p = g_pairs[n];
        char hex[64];
        std::snprintf(hex, sizeof hex, "// pair %u  lower %08x upper %08x", n, p.lower, p.upper);
        o << "    " << hex << "\n    VU1N_PAIR(" << n << ");\n";
        const uint32_t op = lowerOp(p);
        if (p.info.delaysNextBranchRead && n + 1 < kPairs && !g_pairs[n + 1].info.iBit && isCondBranch(lowerOp(g_pairs[n + 1])))
            for (int r = 1; r < 16; ++r)
                if (p.info.lowerViWrite & (1u << r))
                    o << "    s.ov[" << r << "] = s.vi[" << r << "];\n";
        {
            const auto &U = p.info.upper;
            const auto &L = p.info.lower;
            o << "    { uint64_t rdy = 0;";
            for (const auto *u : {&U, &L})
            {
                for (uint32_t k = 0; k < u->vfReadCount; ++k)
                    o << " rdy = vu1n::rdVf(rdy, s, " << int(u->vfRead[k].reg) << ", " << int(u->vfRead[k].lanes) << ");";
                if (u->viRead) o << " rdy = vu1n::rdVi(rdy, s, " << u->viRead << ");";
                if (u->accRead) o << " rdy = vu1n::rdAcc(rdy, s, " << int(u->accRead) << ");";
            }
            if (L.pipeline == 3) o << " rdy = vu1n::rdFdiv(rdy, s);";
            if (L.pipeline == 4) o << " rdy = vu1n::rdEfuRes(rdy, s);";
            if (L.waitQ) o << " rdy = vu1n::rdFdiv(rdy, s);";
            if (L.waitP) o << " rdy = vu1n::rdEfuAll(rdy, s);";
            o << " vu1n::begin(s, rdy); }\n";
        }
        char ubuf[16], lbuf[16];
        std::snprintf(ubuf, sizeof ubuf, "0x%08xu", p.upper);
        std::snprintf(lbuf, sizeof lbuf, "0x%08xu", p.lower);
        std::string lowerCall;
        if (!p.info.iBit)
        {
            if (isBranchOp(op))
                lowerCall = "";
            else if (isXgkick(p.lower))
                lowerCall = std::string("vu1n::xgkick(s, static_cast<uint16_t>(s.vi[") + std::to_string((p.lower >> 11) & 0xF) + "]));";
            else
                lowerCall = std::string("vu1n::lower(s, ") + lbuf + ");";
        }
        if (p.info.shadowReg)
        {
            const int r = p.info.shadowReg;
            o << "    { float o_[4], n_[4]; std::memcpy(o_, s.vf[" << r << "], 16); vu1n::upper(s, " << ubuf << "); std::memcpy(n_, s.vf[" << r
              << "], 16); std::memcpy(s.vf[" << r << "], o_, 16); " << lowerCall << " std::memcpy(s.vf[" << r << "], n_, 16); }\n";
        }
        else
        {
            o << "    vu1n::upper(s, " << ubuf << ");\n";
            if (!lowerCall.empty())
                o << "    " << lowerCall << "\n";
        }
        {
            const auto &U = p.info.upper;
            const auto &L = p.info.lower;
            if (L.vfWrite.reg != 0 && L.vfWrite.reg != p.info.suppressedLowerVf)
                o << "    vu1n::wrVf(s, " << int(L.vfWrite.reg) << ", " << int(L.vfWrite.lanes) << ", " << int(L.vfLatency) << ");\n";
            if (U.vfWrite.reg != 0)
                o << "    vu1n::wrVf(s, " << int(U.vfWrite.reg) << ", " << int(U.vfWrite.lanes) << ", " << int(U.vfLatency) << ");\n";
            if (L.viWrite)
                o << "    vu1n::wrVi(s, " << L.viWrite << ", " << int(L.viLatency) << ");\n";
            if (U.accWrite)
                o << "    vu1n::wrAcc(s, " << int(U.accWrite) << ");\n";
            o << "    vu1n::endPair(s);\n";
        }
        if (p.info.iBit)
        {
            char ib[16];
            std::snprintf(ib, sizeof ib, "0x%08xu", p.lower);
            o << "    { const uint32_t b_ = " << ib << "; float im_; std::memcpy(&im_, &b_, 4); s.i = vu1n::opnd(im_); }\n";
        }
    }

    // Jump to pair idx: a label inside this segment, otherwise back to the driver.
    std::string target(uint32_t idx)
    {
        if (idx < g_lo || idx >= g_hi)
            return "return " + std::to_string(idx) + ";";
        return "{ if (--s.budget < 0) return " + std::to_string(idx) + "; goto L" + std::to_string(idx) + "; }";
    }

    std::string emitSegment(uint32_t lo, uint32_t hi, const std::string &fname, const std::string &srcName)
    {
        g_lo = lo;
        g_hi = hi;
        std::ostringstream o;
        o << "// Generated by vu1_gen (" << srcName << ", pairs " << lo << ".." << hi - 1 << "). Do not edit.\n";
        o << "#include \"runtime/ps2_vu1_native.h\"\n#include <cstring>\n\n";
        o << "extern \"C\" int32_t " << fname << "(vu1n::State &s, uint32_t entry)\n{\n";
        o << "    uint32_t dyn = 0;\n    (void)dyn;\n";
        o << "    switch (entry)\n    {\n";
        for (uint32_t n = lo; n < hi; ++n)
            o << "    case " << n << ": goto L" << n << ";\n";
        o << "    default: return static_cast<int32_t>(entry);\n    }\n";

        for (uint32_t n = lo; n < hi; ++n)
        {
            const Pair &p = g_pairs[n];
            o << "L" << n << ":\n    {\n";
            if (p.info.reserved)
            {
                ++g_unsupported;
                o << "    return " << n << "; // reserved/unsupported instruction\n    }\n";
                continue;
            }
            const uint32_t op = lowerOp(p);
            const bool branch = !p.info.iBit && isBranchOp(op);
            const bool slotOk = n + 1 < kPairs && !g_pairs[n + 1].info.reserved &&
                                !(!g_pairs[n + 1].info.iBit && isBranchOp(lowerOp(g_pairs[n + 1])));
            if (branch)
            {
                if (!slotOk)
                {
                    ++g_unsupported;
                    o << "    return " << n << "; // branch with unsupported delay slot\n    }\n";
                    continue;
                }
                const uint32_t l = p.lower;
                const int it = (l >> 16) & 0xF, is = (l >> 11) & 0xF;
                const uint32_t tgt = ((8u * n + 8u + static_cast<int32_t>(imm11(l)) * 8) & kPcMask) / 8u;
                emitBody(o, n);
                o << "    bool take = ";
                switch (op)
                {
                case 0x20: case 0x21: case 0x24: case 0x25: o << "true"; break;
                case 0x28: o << "static_cast<int16_t>(" << viRead(n, is) << ") == static_cast<int16_t>(" << viRead(n, it) << ")"; break;
                case 0x29: o << "static_cast<int16_t>(" << viRead(n, is) << ") != static_cast<int16_t>(" << viRead(n, it) << ")"; break;
                case 0x2C: o << "static_cast<int16_t>(" << viRead(n, is) << ") < 0"; break;
                case 0x2D: o << "static_cast<int16_t>(" << viRead(n, is) << ") > 0"; break;
                case 0x2E: o << "static_cast<int16_t>(" << viRead(n, is) << ") <= 0"; break;
                case 0x2F: o << "static_cast<int16_t>(" << viRead(n, is) << ") >= 0"; break;
                }
                o << ";\n";
                if (op == 0x24 || op == 0x25)
                {
                    o << "    dyn = (static_cast<uint32_t>(static_cast<uint16_t>(s.vi[" << is << "])) * 8u & 0x3FFFu) >> 3;\n";
                    if (op == 0x25 && it != 0)
                        o << "    s.vi[" << it << "] = static_cast<int16_t>(" << (n + 2) << ");\n";
                }
                else if (op == 0x21 && it != 0)
                    o << "    s.vi[" << it << "] = static_cast<int16_t>(" << (n + 2) << ");\n";
                emitBody(o, n + 1);
                if (op == 0x24 || op == 0x25)
                    o << "    if (--s.budget < 0) return static_cast<int32_t>(dyn);\n    goto Ldyn;\n";
                else if (op == 0x20 || op == 0x21)
                    o << "    " << target(tgt) << "\n";
                else
                    o << "    if (take) { " << target(tgt) << " }\n    " << target(n + 2) << "\n";
                o << "    }\n";
                continue;
            }
            emitBody(o, n);
            if (p.info.eBit)
            {
                if (!slotOk)
                {
                    ++g_unsupported;
                    o << "    return " << n << "; // E-bit with an unsupported delay slot\n    }\n";
                    continue;
                }
                emitBody(o, n + 1);
                o << "    vu1n::flush(s);\n    return -1;\n    }\n";
                continue;
            }
            if (n + 1 < hi)
                o << "    }\n";
            else
                o << "    return " << (n + 1) << ";\n    }\n";
        }
        o << "Ldyn:\n    switch (dyn)\n    {\n";
        for (uint32_t n = lo; n < hi; ++n)
            o << "    case " << n << ": goto L" << n << ";\n";
        o << "    default: return static_cast<int32_t>(dyn);\n    }\n}\n";
        return o.str();
    }
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: vu1_gen <outdir> image.bin [image.bin ...]\n");
        return 2;
    }
    const std::string outdir = argv[1];
    VU1Interpreter vu;
    std::set<std::pair<uint32_t, uint64_t>> done;
    std::vector<std::pair<uint32_t, uint64_t>> order;
    unsigned written = 0;
    for (int a = 2; a < argc; ++a)
    {
        std::ifstream f(argv[a], std::ios::binary);
        std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (img.size() < kPairs * 8)
        {
            std::fprintf(stderr, "skip %s (size)\n", argv[a]);
            continue;
        }
        g_pairs.assign(kPairs, Pair{});
        uint32_t last = 0;
        for (uint32_t i = 0; i < kPairs; ++i)
        {
            std::memcpy(&g_pairs[i].lower, &img[i * 8], 4);
            std::memcpy(&g_pairs[i].upper, &img[i * 8 + 4], 4);
            g_pairs[i].info = vu.analyzePair(g_pairs[i].lower, g_pairs[i].upper);
            if (g_pairs[i].lower || g_pairs[i].upper)
                last = i + 1;
        }
        for (uint32_t seg = 0; seg * kSeg < last; ++seg)
        {
            const uint64_t h = vu1n::hashWindow(img.data(), seg);
            if (!done.insert({seg, h}).second)
                continue;
            order.push_back({seg, h});
            char fname[96], path[512];
            std::snprintf(fname, sizeof fname, "vu1_seg_%u_%016llx", seg, static_cast<unsigned long long>(h));
            std::snprintf(path, sizeof path, "%s/seg_%u_%016llx.cpp", outdir.c_str(), seg, static_cast<unsigned long long>(h));
            std::ofstream out(path);
            out << emitSegment(seg * kSeg, std::min(kPairs, (seg + 1) * kSeg), fname, argv[a]);
            ++written;
        }
    }
    std::ostringstream r;
    r << "// Generated by vu1_gen. Do not edit.\n#include \"runtime/ps2_vu1_native.h\"\n\nextern \"C\"\n{\n";
    for (auto &k : order)
        r << "int32_t vu1_seg_" << k.first << "_" << std::hex << std::setfill('0') << std::setw(16) << k.second << std::dec << "(vu1n::State &, uint32_t);\n";
    r << "}\n\nnamespace vu1n\n{\nSegFn findSegment(uint32_t seg, uint64_t hash)\n{\n    switch (seg)\n    {\n";
    std::map<uint32_t, std::vector<uint64_t>> bySeg;
    for (auto &k : order)
        bySeg[k.first].push_back(k.second);
    for (auto &kv : bySeg)
    {
        r << "    case " << kv.first << ":\n";
        for (uint64_t h : kv.second)
            r << "        if (hash == 0x" << std::hex << std::setfill('0') << std::setw(16) << h << std::dec << "ull) return &vu1_seg_" << kv.first << "_" << std::hex << std::setfill('0') << std::setw(16) << h << std::dec << ";\n";
        r << "        return nullptr;\n";
    }
    r << "    default: return nullptr;\n    }\n}\n} // namespace vu1n\n";
    std::ofstream(outdir + "/vu1_registry.cpp") << r.str();
    std::fprintf(stderr, "vu1_gen: %u distinct segments written to %s (%u unsupported pairs)\n", written, outdir.c_str(), g_unsupported);
    return 0;
}
