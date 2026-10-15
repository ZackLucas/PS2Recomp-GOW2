// gow2_gs_replay: replays a PCSX2 GS dump (.gs / .gs.zst) through the runtime's GS
// and compares the presented frame with a reference image.
//
// Dump layout (see games/gow2/scripts/gsdump.py for the derivation):
//   u32 0xFFFFFFFF, u32 headerSize, header{state_version,state_size,serial_off,serial_size,
//   crc,ss_w,ss_h,ss_off,ss_size}+serial+screenshot, state blob, 8192 B priv regs, packets.
// Packets: 0 Transfer{u8 path,u32 size,data} 1 VSync{u8 field} 2 ReadFIFO{u32} 3 Registers{8192 B}.
//
// usage: gow2_gs_replay dump.gs[.zst] [--ref ref.rgba W H] [--out prefix]
//                       [--vram-offset N | --scan-vram-offset] [--frames N]
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct Packet
    {
        uint8_t kind;
        uint8_t arg;
        size_t offset;
        uint32_t size;
    };

    struct Dump
    {
        std::vector<uint8_t> bytes;
        uint32_t stateVersion = 0, stateSize = 0, crc = 0;
        std::string serial;
        size_t stateOffset = 0, regsOffset = 0;
        std::vector<Packet> packets;
    };

    bool readAll(const std::string &path, std::vector<uint8_t> &out)
    {
        const bool zst = path.size() > 4 && path.compare(path.size() - 4, 4, ".zst") == 0;
        FILE *f = zst ? popen(("zstd -dc '" + path + "'").c_str(), "r") : fopen(path.c_str(), "rb");
        if (!f)
            return false;
        uint8_t buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            out.insert(out.end(), buf, buf + n);
        zst ? pclose(f) : fclose(f);
        return !out.empty();
    }

    template <typename T>
    T rd(const std::vector<uint8_t> &b, size_t off)
    {
        T v;
        std::memcpy(&v, b.data() + off, sizeof(T));
        return v;
    }

    bool parse(Dump &d)
    {
        const auto &b = d.bytes;
        if (b.size() < 48 || rd<uint32_t>(b, 0) != 0xFFFFFFFFu)
            return false;
        const uint32_t headerSize = rd<uint32_t>(b, 4);
        const size_t base = 8;
        d.stateVersion = rd<uint32_t>(b, base + 0);
        d.stateSize = rd<uint32_t>(b, base + 4);
        const uint32_t serialOff = rd<uint32_t>(b, base + 8), serialSize = rd<uint32_t>(b, base + 12);
        d.crc = rd<uint32_t>(b, base + 16);
        d.serial.assign(reinterpret_cast<const char *>(b.data()) + base + serialOff, serialSize);
        size_t p = base + headerSize;
        d.stateOffset = p;
        p += d.stateSize;
        d.regsOffset = p;
        p += 8192;
        while (p < b.size())
        {
            const uint8_t id = b[p++];
            Packet pk{id, 0, 0, 0};
            if (id == 0)
            {
                pk.arg = b[p];
                pk.size = rd<uint32_t>(b, p + 1);
                p += 5;
                pk.offset = p;
                p += pk.size;
            }
            else if (id == 1)
                pk.arg = b[p++];
            else if (id == 2)
            {
                pk.size = rd<uint32_t>(b, p);
                p += 4;
            }
            else if (id == 3)
            {
                pk.offset = p;
                pk.size = 8192;
                p += 8192;
            }
            else
                return false;
            if (p > b.size())
                return false;
            d.packets.push_back(pk);
        }
        return p == b.size();
    }

    void applyPrivRegs(GSRegisters &r, const uint8_t *regs)
    {
        auto q = [&](size_t off) { uint64_t v; std::memcpy(&v, regs + off, 8); return v; };
        r.pmode = q(0x00);   r.smode1 = q(0x10);  r.smode2 = q(0x20);  r.srfsh = q(0x30);
        r.synch1 = q(0x40);  r.synch2 = q(0x50);  r.syncv = q(0x60);   r.dispfb1 = q(0x70);
        r.display1 = q(0x80); r.dispfb2 = q(0x90); r.display2 = q(0xA0); r.extbuf = q(0xB0);
        r.extdata = q(0xC0); r.extwrite = q(0xD0); r.bgcolor = q(0xE0);
        r.csr.store(q(0x1000)); r.imr = q(0x1010); r.busdir = q(0x1040); r.siglblid = q(0x1080);
    }

    // GS freeze blob (state_version 9): version, then the env registers, then two contexts.
    void restoreEnv(GS &gs, const uint8_t *s)
    {
        auto q = [&](size_t off) { uint64_t v; std::memcpy(&v, s + off, 8); return v; };
        static const struct { size_t off; uint8_t reg; } kGlobal[] = {
            {12, 0x1A}, {20, 0x1C}, {28, 0x22}, {36, 0x3B}, {44, 0x3D}, {52, 0x44}, {60, 0x45},
            {68, 0x46}, {76, 0x49}, {84, 0x50}, {100, 0x51}, {108, 0x52}}; // TRXDIR (92) skipped: it starts a transfer
        for (const auto &e : kGlobal)
            gs.writeRegister(e.reg, q(e.off));
        static const uint8_t kCtxRegs[12] = {0x18, 0x06, 0x14, 0x08, 0x34, 0x36, 0x40, 0x42, 0x47, 0x4A, 0x4C, 0x4E};
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 12; ++i)
                gs.writeRegister(static_cast<uint8_t>(kCtxRegs[i] + c), q(124 + c * 96 + i * 8));
        gs.writeRegister(0x00, q(4)); // PRIM last
    }

    struct Diff
    {
        double meanAbs = 0.0;
        double pctOver8 = 0.0;
        uint64_t pixels = 0;
    };

    Diff compare(const std::vector<uint8_t> &a, const std::vector<uint8_t> &ref)
    {
        Diff d;
        const size_t n = std::min(a.size(), ref.size()) / 4;
        uint64_t sum = 0, over = 0;
        for (size_t i = 0; i < n; ++i)
        {
            int m = 0;
            for (int c = 0; c < 3; ++c)
            {
                const int e = std::abs(int(a[i * 4 + c]) - int(ref[i * 4 + c]));
                sum += e;
                m = std::max(m, e);
            }
            over += (m > 8);
        }
        d.pixels = n;
        d.meanAbs = n ? double(sum) / (3.0 * n) : 0.0;
        d.pctOver8 = n ? 100.0 * double(over) / n : 0.0;
        return d;
    }

    void writePpm(const std::string &path, const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h)
    {
        FILE *f = fopen(path.c_str(), "wb");
        if (!f)
            return;
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (size_t i = 0; i < size_t(w) * h; ++i)
            fwrite(&rgba[i * 4], 1, 3, f);
        fclose(f);
    }

    // Build a fresh GS from the dump's initial state and present it (no packets replayed).
    bool presentInitial(const Dump &d, size_t vramOffset, std::vector<uint8_t> &vram, GSRegisters &priv,
                        std::vector<uint8_t> &pixels, uint32_t &w, uint32_t &h)
    {
        const uint8_t *state = d.bytes.data() + d.stateOffset;
        if (vramOffset + PS2_GS_VRAM_SIZE > d.stateSize)
            return false;
        std::memcpy(vram.data(), state + vramOffset, PS2_GS_VRAM_SIZE);
        applyPrivRegs(priv, d.bytes.data() + d.regsOffset);
        GS gs;
        gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &priv);
        restoreEnv(gs, state);
        gs.latchHostPresentationFrame();
        return gs.copyLatchedHostPresentationFrame(pixels, w, h);
    }
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s dump.gs[.zst] [--ref ref.rgba W H] [--out prefix] "
                        "[--vram-offset N | --scan-vram-offset] [--frames N]\n", argv[0]);
        return 2;
    }
    std::string refPath, outPrefix;
    uint32_t refW = 0, refH = 0, maxFrames = ~0u;
    long vramOffset = -1;
    bool scan = false;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--ref" && i + 3 < argc) { refPath = argv[++i]; refW = atoi(argv[++i]); refH = atoi(argv[++i]); }
        else if (a == "--out" && i + 1 < argc) outPrefix = argv[++i];
        else if (a == "--vram-offset" && i + 1 < argc) vramOffset = atol(argv[++i]);
        else if (a == "--scan-vram-offset") scan = true;
        else if (a == "--frames" && i + 1 < argc) maxFrames = atoi(argv[++i]);
    }

    Dump d;
    if (!readAll(argv[1], d.bytes) || !parse(d))
    {
        fprintf(stderr, "failed to read/parse %s\n", argv[1]);
        return 1;
    }
    size_t transfers = 0, vsyncs = 0, regsBlocks = 0, gifBytes = 0;
    for (const auto &p : d.packets)
    {
        transfers += p.kind == 0; vsyncs += p.kind == 1; regsBlocks += p.kind == 3;
        gifBytes += p.kind == 0 ? p.size : 0;
    }
    printf("serial=%s crc=%08x state_version=%u state_size=%u\n", d.serial.c_str(), d.crc, d.stateVersion, d.stateSize);
    printf("packets=%zu transfers=%zu vsync=%zu regs=%zu gif_bytes=%zu\n", d.packets.size(), transfers, vsyncs, regsBlocks, gifBytes);

    std::vector<uint8_t> ref;
    if (!refPath.empty())
    {
        FILE *f = fopen(refPath.c_str(), "rb");
        if (!f) { fprintf(stderr, "cannot open ref %s\n", refPath.c_str()); return 1; }
        ref.resize(size_t(refW) * refH * 4);
        if (fread(ref.data(), 1, ref.size(), f) != ref.size()) { fprintf(stderr, "short ref\n"); return 1; }
        fclose(f);
    }

    std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0);
    GSRegisters priv{};

    if (scan)
    {
        // The freeze blob has a few hundred bytes of registers around the 4 MB VRAM; locate the VRAM
        // by finding the offset whose displayed image best matches the reference.
        if (ref.empty()) { fprintf(stderr, "--scan-vram-offset needs --ref\n"); return 1; }
        struct Cand { size_t off; double err; };
        std::vector<Cand> cands;
        const size_t maxOff = d.stateSize - PS2_GS_VRAM_SIZE;
        for (size_t off = 0; off <= maxOff; ++off)
        {
            std::vector<uint8_t> px; uint32_t w = 0, h = 0;
            if (!presentInitial(d, off, vram, priv, px, w, h) || w != refW || h != refH)
                continue;
            cands.push_back({off, compare(px, ref).meanAbs});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) { return a.err < b.err; });
        for (size_t i = 0; i < std::min<size_t>(8, cands.size()); ++i)
            printf("offset %4zu  mean_abs_err=%.2f\n", cands[i].off, cands[i].err);
        if (cands.empty()) printf("no candidate produced a %ux%u frame\n", refW, refH);
        return 0;
    }

    if (vramOffset < 0)
    {
        fprintf(stderr, "pass --vram-offset N (use --scan-vram-offset to find it)\n");
        return 1;
    }

    // Replay.
    const uint8_t *state = d.bytes.data() + d.stateOffset;
    std::memcpy(vram.data(), state + vramOffset, PS2_GS_VRAM_SIZE);
    applyPrivRegs(priv, d.bytes.data() + d.regsOffset);
    GS gs;
    gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &priv);
    restoreEnv(gs, state);

    using Clock = std::chrono::steady_clock;
    auto segStart = Clock::now();
    uint32_t frame = 0;
    size_t segTransfers = 0, segBytes = 0;
    std::vector<uint8_t> px; uint32_t w = 0, h = 0;
    for (const auto &p : d.packets)
    {
        if (p.kind == 0)
        {
            gs.processGIFPacket(d.bytes.data() + p.offset, p.size);
            ++segTransfers; segBytes += p.size;
        }
        else if (p.kind == 3)
            applyPrivRegs(priv, d.bytes.data() + p.offset);
        else if (p.kind == 1)
        {
            gs.latchHostPresentationFrame();
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - segStart).count();
            const bool ok = gs.copyLatchedHostPresentationFrame(px, w, h);
            printf("frame %u: %zu transfers, %zu bytes GIF, %.1f ms (%.1f%% of a 60 Hz frame)",
                   frame, segTransfers, segBytes, ms, ms * 100.0 / (1000.0 / 60.0));
            if (ok && !ref.empty() && w == refW && h == refH)
            {
                const Diff df = compare(px, ref);
                printf("  vs ref: mean_abs=%.2f  >8: %.2f%%", df.meanAbs, df.pctOver8);
            }
            else if (ok)
                printf("  presented %ux%u", w, h);
            printf("\n");
            if (ok && !outPrefix.empty())
                writePpm(outPrefix + std::to_string(frame) + ".ppm", px, w, h);
            ++frame;
            if (frame >= maxFrames) break;
            segTransfers = segBytes = 0;
            segStart = Clock::now();
        }
    }
    return 0;
}
