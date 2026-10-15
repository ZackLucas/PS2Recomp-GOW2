// gow2_gs_gpu_replay: replays a PCSX2 GS dump through the runtime's GS frontend with a backend that
// feeds BOTH the CPU rasterizer and the GPU executor, then compares them frame by frame.
//
// usage: gow2_gs_gpu_replay dump.gs[.zst] [--vram-offset N] [--ref ref.rgba W H] [--out prefix] [--frames N]
//        [--time-only]
#include "gs_gpu_exec.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace
{
using Clock = std::chrono::steady_clock;

struct Packet { uint8_t kind, arg; size_t offset; uint32_t size; };
struct Dump
{
    std::vector<uint8_t> bytes;
    uint32_t stateSize = 0;
    size_t stateOffset = 0, regsOffset = 0;
    std::vector<Packet> packets;
};

template <typename T> T rd(const std::vector<uint8_t> &b, size_t off) { T v; std::memcpy(&v, b.data() + off, sizeof(T)); return v; }

bool readAll(const std::string &path, std::vector<uint8_t> &out)
{
    const bool zst = path.size() > 4 && path.compare(path.size() - 4, 4, ".zst") == 0;
    FILE *f = zst ? popen(("zstd -dc '" + path + "'").c_str(), "r") : fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    zst ? pclose(f) : fclose(f);
    return !out.empty();
}

bool parse(Dump &d)
{
    const auto &b = d.bytes;
    if (b.size() < 48 || rd<uint32_t>(b, 0) != 0xFFFFFFFFu) return false;
    const uint32_t headerSize = rd<uint32_t>(b, 4);
    d.stateSize = rd<uint32_t>(b, 12);
    size_t p = 8 + headerSize;
    d.stateOffset = p; p += d.stateSize;
    d.regsOffset = p; p += 8192;
    while (p < b.size())
    {
        const uint8_t id = b[p++];
        Packet pk{id, 0, 0, 0};
        if (id == 0) { pk.arg = b[p]; pk.size = rd<uint32_t>(b, p + 1); p += 5; pk.offset = p; p += pk.size; }
        else if (id == 1) pk.arg = b[p++];
        else if (id == 2) { pk.size = rd<uint32_t>(b, p); p += 4; }
        else if (id == 3) { pk.offset = p; pk.size = 8192; p += 8192; }
        else return false;
        if (p > b.size()) return false;
        d.packets.push_back(pk);
    }
    return p == b.size();
}

void applyPrivRegs(GSRegisters &r, const uint8_t *regs)
{
    auto q = [&](size_t off) { uint64_t v; std::memcpy(&v, regs + off, 8); return v; };
    r.pmode = q(0x00); r.smode1 = q(0x10); r.smode2 = q(0x20); r.srfsh = q(0x30);
    r.synch1 = q(0x40); r.synch2 = q(0x50); r.syncv = q(0x60); r.dispfb1 = q(0x70);
    r.display1 = q(0x80); r.dispfb2 = q(0x90); r.display2 = q(0xA0); r.extbuf = q(0xB0);
    r.extdata = q(0xC0); r.extwrite = q(0xD0); r.bgcolor = q(0xE0);
    r.csr.store(q(0x1000)); r.imr = q(0x1010); r.busdir = q(0x1040); r.siglblid = q(0x1080);
}

void restoreEnv(GS &gs, const uint8_t *s)
{
    auto q = [&](size_t off) { uint64_t v; std::memcpy(&v, s + off, 8); return v; };
    static const struct { size_t off; uint8_t reg; } kGlobal[] = {
        {12, 0x1A}, {20, 0x1C}, {28, 0x22}, {36, 0x3B}, {44, 0x3D}, {52, 0x44}, {60, 0x45},
        {68, 0x46}, {76, 0x49}, {84, 0x50}, {100, 0x51}, {108, 0x52}};
    for (const auto &e : kGlobal) gs.writeRegister(e.reg, q(e.off));
    static const uint8_t kCtx[12] = {0x18, 0x06, 0x14, 0x08, 0x34, 0x36, 0x40, 0x42, 0x47, 0x4A, 0x4C, 0x4E};
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 12; ++i) gs.writeRegister(static_cast<uint8_t>(kCtx[i] + c), q(124 + c * 96 + i * 8));
    gs.writeRegister(0x00, q(4));
}

void writePpmLocal(const std::string &path, const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < size_t(w) * h; ++i) fwrite(&rgba[i * 4], 1, 3, f);
    fclose(f);
}

// Backend that runs every command on the CPU rasterizer (the reference) and on the GPU executor.
class TeeBackend final : public GSRasterBackend
{
public:
    TeeBackend(GpuGsExecutor &gpu, std::vector<uint8_t> &gpuViewVram) : m_gpu(gpu), m_view(gpuViewVram) {}

    void Initialize(uint8_t *vram, uint32_t size) override
    {
        m_vramPtr = vram;
        if (const char *w = getenv("GT_WATCH_WORD")) m_watch = static_cast<size_t>(atol(w));
        m_flushEach = getenv("GT_CPU_FLUSH_EACH") != nullptr;
        m_stats = getenv("GT_STATS") != nullptr;
        if (const char *m = getenv("GT_MODE")) { m_cpuOn = std::string(m) == "both"; m_gpuOn = std::string(m) != "nullonly"; }
        m_cpu.Initialize(vram, size);
        if (m_gpuOn) m_gpu.initialize(vram);
    }
    void Reset() override { if (m_cpuOn) m_cpu.Reset(); if (m_gpuOn) m_gpu.reset(); }
    void Submit(const GSPrimitiveBatch &b) override
    {
        uint32_t before = 0;
        if (m_watch != ~size_t(0)) std::memcpy(&before, m_vramPtr + m_watch * 4, 4);
        if (m_stats)
        {
            const GSContext &c = b.state.context;
            char k[200];
            if (b.state.prim.tme)
            {
                const bool pal = c.tex0.psm == 0x13 || c.tex0.psm == 0x14 || c.tex0.psm == 0x1B || c.tex0.psm == 0x24 || c.tex0.psm == 0x2C;
                snprintf(k, sizeof k, "tex psm=%02x%s tfx=%u tcc=%u filt=%d", c.tex0.psm,
                         pal ? (std::string(" CLUT cpsm=") + (c.tex0.cpsm == 0 ? "00" : c.tex0.cpsm == 1 ? "01" : c.tex0.cpsm == 2 ? "02" : "0a") +
                                " csm=" + std::to_string(c.tex0.csm) + " csa=" + std::to_string(c.tex0.csa)).c_str() : "",
                         c.tex0.tfx, c.tex0.tcc, int(b.state.linearFilter));
                m_hist[k]++;
            }
            snprintf(k, sizeof k, "blend abe=%d a=%llu b=%llu c=%llu d=%llu fix=%llu pabe=%d | test ate=%llu atst=%llu afail=%llu date=%llu ztst=%llu",
                     int(b.state.prim.abe), (unsigned long long)(c.alpha & 3), (unsigned long long)((c.alpha >> 2) & 3),
                     (unsigned long long)((c.alpha >> 4) & 3), (unsigned long long)((c.alpha >> 6) & 3),
                     (unsigned long long)((c.alpha >> 32) & 0xFF), int(b.state.pabe), (unsigned long long)(c.test & 1),
                     (unsigned long long)((c.test >> 1) & 7), (unsigned long long)((c.test >> 12) & 3),
                     (unsigned long long)((c.test >> 14) & 1), (unsigned long long)((c.test >> 17) & 3));
            m_hist[k]++;
            snprintf(k, sizeof k, "target fbp=%u fbw=%u fpsm=%02x fbmsk=%08x | z zbp=%u zpsm=%02x zmask=%d | fge=%d fba=%llu",
                     c.frame.fbp, c.frame.fbw, c.frame.psm, c.frame.fbmsk, c.zbuf.zbp, c.zbuf.psm, int(c.zbuf.zmask),
                     int(b.state.prim.fge), (unsigned long long)(c.fba & 1));
            m_hist[k]++;
        }
        if (const char *tr = getenv("GT_TEXRANGE"))   // "a-b": log textured prims whose TBP0 (blocks) lies in [a,b]
        {
            unsigned a = 0, bb = 0;
            if (sscanf(tr, "%u-%u", &a, &bb) == 2 && b.state.prim.tme && b.state.context.tex0.tbp0 >= a && b.state.context.tex0.tbp0 <= bb)
            {
                const GSContext &c = b.state.context;
                char k[240];
                snprintf(k, sizeof k, "tbp=%u tbw=%u psm=%02x %ux%u tfx=%u tcc=%u cbp=%u cpsm=%02x csm=%u csa=%u cld=%u fst=%d filt=%d clamp=%llx -> fb p=%u psm=%02x",
                         c.tex0.tbp0, c.tex0.tbw, c.tex0.psm, b.state.textureWidth, b.state.textureHeight, c.tex0.tfx, c.tex0.tcc,
                         c.tex0.cbp, c.tex0.cpsm, c.tex0.csm, c.tex0.csa, c.tex0.cld, int(b.state.prim.fst), int(b.state.linearFilter),
                         (unsigned long long)c.clamp, c.frame.fbp, c.frame.psm);
                m_hist[k]++;
                if (m_primIndex < 200000 && m_texFirst.find(k) == m_texFirst.end()) m_texFirst[k] = m_primIndex;
            }
        }
        if (const char *lp = getenv("GT_LIST_PACKET"))   // list every primitive issued by this packet
        {
            unsigned la = 0, lb = 0;
            if (sscanf(lp, "%u-%u", &la, &lb) != 2) { la = lb = static_cast<unsigned>(atol(lp)); }
            if (m_curPacket >= la && m_curPacket <= lb)
            {
                const GSContext &c = b.state.context;
                printf("  [sc %u-%u,%u-%u ofs %u,%u zbp=%u zpsm=%02x]\n", c.scissor.x0, c.scissor.x1, c.scissor.y0, c.scissor.y1, c.xyoffset.ofx, c.xyoffset.ofy, c.zbuf.zbp, c.zbuf.psm);
                printf("  pkt%zu prim#%llu %s ctx%d FB p=%u bw=%u psm=%02x msk=%08x | tme=%d abe=%d fst=%d iip=%d | alpha=%llx fix=%llu test=%llx | tex tbp=%u tbw=%u psm=%02x %ux%u tfx=%u tcc=%u cld=%u cbp=%u | v0=(%.1f,%.1f) v1=(%.1f,%.1f)\n",
                       m_curPacket, (unsigned long long)m_primIndex, b.state.prim.type == GS_PRIM_SPRITE ? "SPRITE" : b.state.prim.type == GS_PRIM_TRISTRIP ? "TSTRIP" : b.state.prim.type == GS_PRIM_TRIANGLE ? "TRI   " : "OTHER ",
                       int(b.state.prim.ctxt), c.frame.fbp, c.frame.fbw, c.frame.psm, c.frame.fbmsk, int(b.state.prim.tme), int(b.state.prim.abe),
                       int(b.state.prim.fst), int(b.state.prim.iip), (unsigned long long)(c.alpha & 0xFF), (unsigned long long)((c.alpha >> 32) & 0xFF),
                       (unsigned long long)c.test, c.tex0.tbp0, c.tex0.tbw, c.tex0.psm, b.state.textureWidth, b.state.textureHeight, c.tex0.tfx,
                       c.tex0.tcc, c.tex0.cld, c.tex0.cbp, b.vertices[0].x, b.vertices[0].y, b.vertices[1].x, b.vertices[1].y);
            }
        }
        if (m_flushEach) m_cpu.TextureFlush();   // debugging: make the CPU texture page cache fully coherent
        if (m_cpuOn) m_cpu.Submit(b);
        if (m_watch != ~size_t(0))
        {
            uint32_t after;
            std::memcpy(&after, m_vramPtr + m_watch * 4, 4);
            if (after != before)
            {
                const GSContext &c = b.state.context;
                printf("  [watch word %zu] prim#%llu type=%d vc=%d %08x -> %08x | FB p=%u bw=%u psm=%02x fbmsk=%08x Z p=%u psm=%02x zmask=%d\n"
                       "      tme=%d fst=%d abe=%d pabe=%d iip=%d test=%llx alpha=%llx fba=%llu | tex tbp=%u tbw=%u psm=%02x %ux%u tcc=%u tfx=%u lin=%d\n",
                       m_watch, (unsigned long long)m_primIndex, int(b.state.prim.type), int(b.vertexCount), before, after,
                       c.frame.fbp, c.frame.fbw, c.frame.psm, c.frame.fbmsk, c.zbuf.zbp, c.zbuf.psm, int(c.zbuf.zmask),
                       int(b.state.prim.tme), int(b.state.prim.fst), int(b.state.prim.abe), int(b.state.pabe), int(b.state.prim.iip),
                       (unsigned long long)c.test, (unsigned long long)c.alpha, (unsigned long long)c.fba,
                       c.tex0.tbp0, c.tex0.tbw, c.tex0.psm, b.state.textureWidth, b.state.textureHeight, c.tex0.tcc, c.tex0.tfx,
                       int(b.state.linearFilter));
                for (int v = 0; v < b.vertexCount; ++v)
                    printf("      v%d x=%.3f y=%.3f z=%.0f rgba=%u,%u,%u,%u st=%g,%g q=%g uv=%u,%u\n", v, b.vertices[v].x, b.vertices[v].y,
                           b.vertices[v].z, b.vertices[v].r, b.vertices[v].g, b.vertices[v].b, b.vertices[v].a, b.vertices[v].s,
                           b.vertices[v].t, b.vertices[v].q, b.vertices[v].u, b.vertices[v].v);
            }
        }
        if (const char *iso = getenv("GT_ISOLATE"))
        {
            if (m_primIndex == static_cast<uint64_t>(atoll(iso)))
            {
                std::vector<uint8_t> g(PS2_GS_VRAM_SIZE);
                m_gpu.readVram(g.data());   // GT_SERIALIZE=1 makes this the state right after this primitive
                GSCpuBackend gv;
                gv.Initialize(g.data(), static_cast<uint32_t>(g.size()));
                const GSContext &c = b.state.context;
                printf("  [isolate prim#%llu] texels (tbp=%u tbw=%u psm=%02x) CPU / GPU for x=0..3, y=253..255:\n",
                       (unsigned long long)m_primIndex, c.tex0.tbp0, c.tex0.tbw, c.tex0.psm);
                for (uint32_t y = 253; y <= 255; ++y)
                {
                    printf("      y=%u:", y);
                    for (uint32_t x = 0; x < 4; ++x)
                        printf("  %06x/%06x", m_cpu.ReadVram(c.tex0.psm, c.tex0.tbp0, c.tex0.tbw, x, y) & 0xFFFFFF,
                               gv.ReadVram(c.tex0.psm, c.tex0.tbp0, c.tex0.tbw, x, y) & 0xFFFFFF);
                    printf("\n");
                }
                printf("  output pixels (FB psm=%02x p=%u bw=%u) CPU / GPU for y=126..127, x=0..4:\n", c.frame.psm, c.frame.fbp, c.frame.fbw);
                for (uint32_t y = 126; y <= 127; ++y)
                {
                    printf("      y=%u:", y);
                    for (uint32_t x = 0; x < 5; ++x)
                        printf("  %06x/%06x", m_cpu.ReadVram(c.frame.psm, c.frame.fbp << 5, c.frame.fbw, x, y) & 0xFFFFFF,
                               gv.ReadVram(c.frame.psm, c.frame.fbp << 5, c.frame.fbw, x, y) & 0xFFFFFF);
                    printf("\n");
                }
            }
        }
        if (const char *snap = getenv("GT_SNAP"))
        {
            std::string list = std::string(",") + snap + ",";
            if (list.find("," + std::to_string(m_primIndex) + ",") != std::string::npos)
            {
                uint32_t fbp = 8, fbw = 8, psm = 0, w = 512, h = 448;
                if (const char *fb = getenv("GT_SNAP_FB")) sscanf(fb, "%u,%u,%x,%u,%u", &fbp, &fbw, &psm, &w, &h);
                std::vector<uint8_t> img(size_t(w) * h * 4);
                for (uint32_t y = 0; y < h; ++y)
                    for (uint32_t x = 0; x < w; ++x)
                    {
                        const uint32_t px = m_cpu.ReadVram(psm, fbp << 5, fbw, x, y);
                        uint8_t *o = &img[(size_t(y) * w + x) * 4];
                        o[0] = px & 0xFF; o[1] = (px >> 8) & 0xFF; o[2] = (px >> 16) & 0xFF; o[3] = 255;
                    }
                char name[256];
                snprintf(name, sizeof name, "%ssnap_p%u_prim%llu.ppm", getenv("GT_SNAP_PREFIX") ? getenv("GT_SNAP_PREFIX") : "", fbp,
                         (unsigned long long)m_primIndex);
                writePpmLocal(name, img, w, h);
                printf("  [snap] wrote %s\n", name);
            }
        }
        ++m_primIndex;
        if (!m_gpuOn) return;
        const auto t0 = Clock::now();
        m_gpu.submit(b);
        m_gpuMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }
    void LoadClut(const GSTex0Reg &t, const GSTexClutReg &c) override
    {
        if (m_cpuOn) m_cpu.LoadClut(t, c);
        if (!m_gpuOn) return;
        const auto t0 = Clock::now();
        m_gpu.loadClut(t, c);
        m_gpuMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }
    void BeginTransfer(const GSTransferCommand &c) override
    {
        if (const char *lx = getenv("GT_LIST_XFER"))   // "a-b": log host->local transfers issued by packets a..b
        {
            unsigned a = 0, bb = 0;
            if (sscanf(lx, "%u-%u", &a, &bb) == 2 && m_curPacket >= a && m_curPacket <= bb)
                printf("  [xfer pkt %zu] dir=%u dbp=%u dbw=%u dpsm=%02x dst=(%u,%u) size=%ux%u\n", m_curPacket, c.direction,
                       c.bitbltbuf.dbp, c.bitbltbuf.dbw, c.bitbltbuf.dpsm, c.trxpos.dsax, c.trxpos.dsay, c.trxreg.rrw, c.trxreg.rrh);
        }
        if (m_cpuOn) m_cpu.BeginTransfer(c);
        if (!m_gpuOn) return;
        const auto t0 = Clock::now();
        m_gpu.beginTransfer(c);
        m_gpuMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }
    void UploadImage(const uint8_t *d, uint32_t n) override
    {
        if (m_cpuOn) m_cpu.UploadImage(d, n);
        if (!m_gpuOn) return;
        const auto t0 = Clock::now();
        m_gpu.uploadImage(d, n);
        m_gpuMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }
    void Flush() override { m_cpu.Flush(); }
    void TextureFlush() override { m_cpu.TextureFlush(); }
    void Sync(GSSyncReason r) override { m_cpu.Sync(r); }
    PresentationFrame Present(const GSPresentationRequest &req) override
    {
        if (!m_cpuOn) return {};
        PresentationFrame cpuFrame = m_cpu.Present(req);
        m_gpu.readVram(m_view.data());
        GSCpuBackend view;
        view.Initialize(m_view.data(), static_cast<uint32_t>(m_view.size()));
        m_gpuFrame = view.Present(req);
        // Present returns rows with a 640 pixel stride; the frontend packs them when it hands the frame
        // out, so do the same here to compare like with like.
        if (!m_gpuFrame.pixels.empty() && m_gpuFrame.width != 0u)
        {
            constexpr size_t kStride = 640u * 4u;
            const size_t rowBytes = static_cast<size_t>(m_gpuFrame.width) * 4u;
            std::vector<uint8_t> packed(rowBytes * m_gpuFrame.height);
            for (uint32_t y = 0; y < m_gpuFrame.height; ++y)
                std::memcpy(packed.data() + y * rowBytes, m_gpuFrame.pixels.data() + y * kStride, rowBytes);
            m_gpuFrame.pixels.swap(packed);
        }
        return cpuFrame;
    }
    bool ClearFramebuffer(const GSContext &c, uint32_t rgba) override { return m_cpu.ClearFramebuffer(c, rgba); }
    uint32_t ConsumeLocalToHostBytes(uint8_t *d, uint32_t n) override { return m_cpu.ConsumeLocalToHostBytes(d, n); }
    uint32_t ReadVram(uint32_t p, uint32_t b, uint32_t w, uint32_t x, uint32_t y) const override { return m_cpu.ReadVram(p, b, w, x, y); }
    void WriteVram(uint32_t p, uint32_t b, uint32_t w, uint32_t x, uint32_t y, uint32_t v) override { m_cpu.WriteVram(p, b, w, x, y, v); }
    void SnapshotVram(std::vector<uint8_t> &o) const override { m_cpu.SnapshotVram(o); }
    GSTransferSnapshot GetTransferSnapshot() const override { return m_cpu.GetTransferSnapshot(); }

    // Dumps a color buffer (from the CPU reference state) as a PPM.
    void SnapshotBuffer(const std::string &path, uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h) const
    {
        std::vector<uint8_t> img(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
            {
                const uint32_t px = m_cpu.ReadVram(psm, fbp << 5, fbw, x, y);
                uint8_t *o = &img[(size_t(y) * w + x) * 4];
                o[0] = px & 0xFF; o[1] = (px >> 8) & 0xFF; o[2] = (px >> 16) & 0xFF; o[3] = 255;
            }
        writePpmLocal(path, img, w, h);
    }

    PresentationFrame m_gpuFrame;
    std::map<std::string, uint64_t> m_hist;
    std::map<std::string, uint64_t> m_texFirst;
    double m_gpuMs = 0.0;
    uint8_t *m_vramPtr = nullptr;
    size_t m_curPacket = 0;
    size_t m_watch = ~size_t(0);
    bool m_flushEach = false;
    bool m_stats = false;
    uint64_t m_primIndex = 0;
    bool m_cpuOn = true, m_gpuOn = true;

private:
    GSCpuBackend m_cpu;
    GpuGsExecutor &m_gpu;
    std::vector<uint8_t> &m_view;
};

double meanAbs(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, double *pctOver8 = nullptr)
{
    const size_t n = std::min(a.size(), b.size()) / 4;
    uint64_t sum = 0, over = 0;
    for (size_t i = 0; i < n; ++i)
    {
        int m = 0;
        for (int c = 0; c < 3; ++c) { const int e = std::abs(int(a[i * 4 + c]) - int(b[i * 4 + c])); sum += e; m = std::max(m, e); }
        over += (m > 8);
    }
    if (pctOver8) *pctOver8 = n ? 100.0 * double(over) / double(n) : 0.0;
    return n ? double(sum) / (3.0 * double(n)) : 0.0;
}

void writePpm(const std::string &path, const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < size_t(w) * h; ++i) fwrite(&rgba[i * 4], 1, 3, f);
    fclose(f);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s dump [--vram-offset N] [--ref f W H] [--out prefix] [--frames N]\n", argv[0]); return 2; }
    long vramOffset = 425;
    std::string refPath, outPrefix;
    uint32_t refW = 0, refH = 0, maxFrames = ~0u;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--vram-offset" && i + 1 < argc) vramOffset = atol(argv[++i]);
        else if (a == "--ref" && i + 3 < argc) { refPath = argv[++i]; refW = atoi(argv[++i]); refH = atoi(argv[++i]); }
        else if (a == "--out" && i + 1 < argc) outPrefix = argv[++i];
        else if (a == "--frames" && i + 1 < argc) maxFrames = atoi(argv[++i]);
    }

    Dump d;
    if (!readAll(argv[1], d.bytes) || !parse(d)) { fprintf(stderr, "failed to read/parse %s\n", argv[1]); return 1; }
    std::vector<uint8_t> ref;
    if (!refPath.empty())
    {
        FILE *f = fopen(refPath.c_str(), "rb");
        ref.resize(size_t(refW) * refH * 4);
        if (!f || fread(ref.data(), 1, ref.size(), f) != ref.size()) { fprintf(stderr, "cannot read ref\n"); return 1; }
        fclose(f);
    }

    if (!gl::initContext()) { fprintf(stderr, "no GL context\n"); return 1; }
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    GSMem::InitLookupTables();
    GpuGsExecutor gpu(GOW2_SHADER_DIR);

    std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE), gpuView(PS2_GS_VRAM_SIZE), gpuVram(PS2_GS_VRAM_SIZE);
    const uint8_t *state = d.bytes.data() + d.stateOffset;
    std::memcpy(vram.data(), state + vramOffset, vram.size());
    if (getenv("GT_ZERO_VRAM")) std::fill(vram.begin(), vram.end(), uint8_t(0));   // diagnostic: frame must not depend on stale VRAM
    if (const char *zp = getenv("GT_ZERO_PAGES"))   // "a-b[,c-d...]": zero 8 KiB pages [a,b) of the initial VRAM
    {
        std::string list = zp;
        size_t pos = 0;
        while (pos < list.size())
        {
            size_t end = list.find(',', pos);
            if (end == std::string::npos) end = list.size();
            unsigned a = 0, b = 0;
            if (sscanf(list.substr(pos, end - pos).c_str(), "%u-%u", &a, &b) == 2)
                for (unsigned pg = a; pg < b && pg < 512; ++pg) std::fill_n(vram.begin() + size_t(pg) * 8192, 8192, uint8_t(0));
            pos = end + 1;
        }
    }
    GSRegisters priv{};
    applyPrivRegs(priv, d.bytes.data() + d.regsOffset);

    GS gs;
    gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &priv);
    auto tee = std::make_unique<TeeBackend>(gpu, gpuView);
    TeeBackend *teePtr = tee.get();
    gs.setRasterBackend(std::move(tee));
    restoreEnv(gs, state);

    uint32_t frame = 0;
    double cpuMs = 0.0;
    auto segStart = Clock::now();
    size_t totalBad = 0;
    std::string snapPackets;
    if (const char *sp = getenv("GT_SNAP_PACKET")) snapPackets = std::string(",") + sp + ",";
    size_t packetIndex = 0;
    std::vector<uint64_t> primTargets;
    if (const char *pt = getenv("GT_PRIM_PACKET"))   // "n1,n2,...": report the packet after which primitive n was issued
    {
        for (const char *c = pt; *c;) { primTargets.push_back(strtoull(c, nullptr, 10)); while (*c && *c != ',') ++c; if (*c) ++c; }
    }
    for (const auto &p : d.packets)
    {
        if (p.kind == 0)
        {
            teePtr->m_curPacket = packetIndex;
            gs.processGIFPacket(d.bytes.data() + p.offset, p.size);
            for (auto it = primTargets.begin(); it != primTargets.end();)
                if (teePtr->m_primIndex > *it) { printf("  [map] primitive #%llu was issued by packet %zu\n", (unsigned long long)*it, packetIndex); it = primTargets.erase(it); }
                else ++it;
            if (const char *vp = getenv("GT_VRAM_PACKETS"))   // "a,b,...": write the CPU model's raw 4 MiB VRAM after these packets
            {
                if ((std::string(",") + vp + ",").find("," + std::to_string(packetIndex) + ",") != std::string::npos)
                {
                    const std::string fn = std::string(getenv("GT_VRAM_PREFIX") ? getenv("GT_VRAM_PREFIX") : "vram_") + std::to_string(packetIndex) + ".bin";
                    if (FILE *f = fopen(fn.c_str(), "wb")) { fwrite(teePtr->m_vramPtr, 1, 4u << 20, f); fclose(f); }
                }
            }
            if (!snapPackets.empty() && snapPackets.find("," + std::to_string(packetIndex) + ",") != std::string::npos)
            {
                const char *pre = getenv("GT_SNAP_PREFIX") ? getenv("GT_SNAP_PREFIX") : "";
                teePtr->SnapshotBuffer(std::string(pre) + "pkt" + std::to_string(packetIndex) + ".ppm", 8, 8, 0, 512, 448);
            }
        }
        else if (p.kind == 3)
            applyPrivRegs(priv, d.bytes.data() + p.offset);
        else if (p.kind == 1)
        {
            if (getenv("GT_MODE") && !std::string(getenv("GT_MODE")).compare("gpuonly")) gpu.finish();
            cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - segStart).count();

            const auto g0 = Clock::now();
            gpu.finish();
            const double gpuFinishMs = std::chrono::duration<double, std::milli>(Clock::now() - g0).count();
            const double gpuMs = teePtr->m_gpuMs + gpuFinishMs;

            if (!teePtr->m_cpuOn)
            {
                printf("frame %u: wall time %.2f ms for frontend%s\n", frame, cpuMs,
                       teePtr->m_gpuOn ? " + GPU (CPU rasterizer off; includes final glFinish)" : " only (no rasterization at all)");
                teePtr->m_gpuMs = 0.0;
                ++frame;
                if (frame >= maxFrames) break;
                segStart = Clock::now();
                continue;
            }
            gpu.readVram(gpuVram.data());
            size_t bad = 0, firstBad = 0;
            for (size_t i = 0; i < vram.size(); i += 4)
                if (std::memcmp(&vram[i], &gpuVram[i], 4) != 0) { if (!bad) firstBad = i / 4; ++bad; }
            totalBad += bad;
            if (bad && frame == 0)
            {
                size_t shown = 0;
                for (size_t i = 0; i < vram.size() && shown < 12; i += 4)
                    if (std::memcmp(&vram[i], &gpuVram[i], 4) != 0)
                    {
                        uint32_t c, g;
                        std::memcpy(&c, &vram[i], 4); std::memcpy(&g, &gpuVram[i], 4);
                        printf("    word %zu (byte %zu, page %zu, block %zu): cpu=%08x gpu=%08x\n", i / 4, i, i / 8192, i / 256, c, g);
                        ++shown;
                    }
            }

            gs.latchHostPresentationFrame();
            std::vector<uint8_t> cpuPx;
            uint32_t w = 0, h = 0;
            const bool haveCpu = gs.copyLatchedHostPresentationFrame(cpuPx, w, h);
            const std::vector<uint8_t> &gpuPx = teePtr->m_gpuFrame.pixels;

            printf("frame %u: GPU path %.1f ms (draw/upload calls incl. host cost; CPU path %.1f ms) | VRAM words differing CPU vs GPU: %zu",
                   frame, gpuMs, cpuMs, bad);
            if (bad) printf(" (first at word %zu)", firstBad);
            printf("\n");
            if (haveCpu && !gpuPx.empty() && !ref.empty() && teePtr->m_gpuFrame.width == refW && teePtr->m_gpuFrame.height == refH && w == refW)
            {
                double o1 = 0, o2 = 0, o3 = 0;
                const double eCpuRef = meanAbs(cpuPx, ref, &o1), eGpuRef = meanAbs(gpuPx, ref, &o2), eCpuGpu = meanAbs(cpuPx, gpuPx, &o3);
                printf("         presented %ux%u | CPU vs ref %.2f (%.1f%% >8) | GPU vs ref %.2f (%.1f%% >8) | CPU vs GPU %.2f (%.1f%% >8)\n",
                       w, h, eCpuRef, o1, eGpuRef, o2, eCpuGpu, o3);
            }
            if (!outPrefix.empty() && !gpuPx.empty())
                writePpm(outPrefix + "gpu" + std::to_string(frame) + ".ppm", gpuPx, teePtr->m_gpuFrame.width, teePtr->m_gpuFrame.height);

            teePtr->m_gpuMs = 0.0;
            ++frame;
            if (frame >= maxFrames) break;
            segStart = Clock::now();
        }
        ++packetIndex;
    }
    const auto &s = gpu.stats();
    printf("GPU executor: %llu primitives in %llu draws (splits: %llu target-mapping, %llu render-to-texture), %llu CLUT loads, "
           "%llu uploads in %llu chunks | unsupported: %llu line prims, %llu non-host->local transfers | primitives sampling memory they also write: %llu\n",
           (unsigned long long)s.prims, (unsigned long long)s.draws, (unsigned long long)s.splitsSignature,
           (unsigned long long)s.splitsHazard, (unsigned long long)s.clutLoads, (unsigned long long)s.uploads,
           (unsigned long long)s.uploadChunks, (unsigned long long)s.unsupportedLines, (unsigned long long)s.unsupportedTransfers,
           (unsigned long long)s.selfFeedback);
    if (getenv("GT_TEXRANGE"))
    {
        printf("--- textured primitives with TBP0 in range %s (frame 0 only) ---\n", getenv("GT_TEXRANGE"));
        for (auto &kv : teePtr->m_hist)
            printf("%6llu  first prim#%llu  %s\n", (unsigned long long)kv.second, (unsigned long long)teePtr->m_texFirst[kv.first], kv.first.c_str());
    }
    if (teePtr->m_stats)
    {
        std::vector<std::pair<uint64_t, std::string>> rows;
        for (auto &kv : teePtr->m_hist) rows.push_back({kv.second, kv.first});
        std::sort(rows.rbegin(), rows.rend());
        printf("--- primitive classes over %u frames (count: class) ---\n", frame);
        for (auto &r : rows) printf("%7llu  %s\n", (unsigned long long)r.first, r.second.c_str());
    }
    if (getenv("GT_PROFILE"))
        printf("GPU time by category over %u frames (stalling timers): draws %.2f ms, uploads %.2f ms, CLUT loads %.2f ms  -> per frame: draws %.2f, uploads %.2f, clut %.2f\n",
               frame, s.gpuMsDraw, s.gpuMsUpload, s.gpuMsClut, s.gpuMsDraw / frame, s.gpuMsUpload / frame, s.gpuMsClut / frame);
    printf(totalBad ? "RESULT: CPU and GPU VRAM differ (%zu words in total)\n" : "RESULT: GPU VRAM identical to CPU VRAM on every frame\n", totalBad);
    return totalBad ? 1 : 0;
}
