// GPU execution of the GS backend command stream (VRAM as a buffer on the GPU).
//
// Consumes the same calls the frontend makes on a GSRasterBackend (Submit / LoadClut /
// BeginTransfer / UploadImage) and executes them with the shaders validated by the gow2_gs_*_test
// suites. Draws are batched; a batch is split (with a memory barrier) whenever
//   - the render-target mapping changes (FBP/FBW/FPSM/ZBP/ZPSM), because the per-pixel interlock
//     only orders fragments at the same screen position, or
//   - a primitive samples memory an earlier primitive of the batch wrote (render-to-texture), or
//   - any non-draw command (CLUT load, transfer) arrives.
#pragma once
#include "gs_prim_pack.h"
#include "gs_tables_gl.h"
#include "runtime/gs/gs_types.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

class GpuGsExecutor
{
public:
    struct Stats
    {
        uint64_t prims = 0, draws = 0, splitsSignature = 0, splitsHazard = 0, clutLoads = 0, uploads = 0, uploadChunks = 0;
        uint64_t unsupportedLines = 0, unsupportedTransfers = 0, selfFeedback = 0;
        double gpuMsDraw = 0, gpuMsUpload = 0, gpuMsClut = 0, hostMsPack = 0;   // only with GT_PROFILE
    };

    explicit GpuGsExecutor(const std::string &shaderDir)
    {
        m_serialize = getenv("GT_SERIALIZE") != nullptr;
        m_profile = getenv("GT_PROFILE") != nullptr;
        glGenQueries(1, &m_query);
        gl::bindGsTables();
        auto slurp = [](const std::string &path)
        {
            std::ifstream f(path);
            if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(2); }
            std::stringstream ss; ss << f.rdbuf();
            return ss.str();
        };
        const std::string vram = slurp(shaderDir + "/gs_vram_tables.glsl") + slurp(shaderDir + "/gs_vram.glsl");
        const std::string common = slurp(shaderDir + "/gs_draw_common.glsl");

        const std::string vs = "#version 450\n" + common + slurp(shaderDir + "/gs_draw.vert");
        const std::string fs =
            "#version 450\n#extension GL_ARB_fragment_shader_interlock : require\n"
            "#pragma optionNV(fastmath off)\n#pragma optionNV(fastprecision off)\n"
            "layout(std430, binding = 0) coherent buffer Vram { uint vram[]; };\n"
            "layout(std430, binding = 2) coherent readonly buffer Clut { uint clut[]; };\n" +
            vram + slurp(shaderDir + "/gs_clut.glsl") + slurp(shaderDir + "/gs_pixel.glsl") + common +
            slurp(shaderDir + "/gs_texture.glsl") + slurp(shaderDir + "/gs_draw.frag");
        m_drawProg = gl::link(gl::compile(GL_VERTEX_SHADER, vs.c_str()), gl::compile(GL_FRAGMENT_SHADER, fs.c_str()));
        m_uBase = glGetUniformLocation(m_drawProg, "uBaseInstance");

        std::string load = slurp(shaderDir + "/gs_clut_load.glsl");
        load.replace(load.find("#include_gs_vram_here"), 21, vram);
        m_clutProg = gl::computeProgram(("#version 450\n" + load).c_str());

        std::string up = slurp(shaderDir + "/gs_upload.glsl");
        up.replace(up.find("#include_gs_vram_here"), 21, vram);
        m_upProg = gl::computeProgram(("#version 450\n" + up).c_str());

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, 2048);
        glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, 2048);
        glViewport(0, 0, 2048, 2048);
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(4, m_buf);   // vram, clut, prims, upload source
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buf[kVram]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, kVramBytes, nullptr, GL_DYNAMIC_COPY);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buf[kClut]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 512 * 4, nullptr, GL_DYNAMIC_COPY);
    }

    void initialize(const uint8_t *vram)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buf[kVram]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, kVramBytes, vram);
        reset();
    }

    void reset()
    {
        const std::vector<uint32_t> zero(512, 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buf[kClut]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 512 * 4, zero.data());
        m_clutCbp = {0, 0};
        m_pending.clear();
        m_written.clear();
        m_xfer = {};
    }

    void submit(const GSPrimitiveBatch &batch)
    {
        if (batch.vertexCount == 0u) return;
        if (batch.state.prim.type == GS_PRIM_LINE || batch.state.prim.type == GS_PRIM_LINESTRIP)
        {
            ++m_stats.unsupportedLines;
            return;
        }
        const GSContext &c = batch.state.context;
        const std::array<uint32_t, 5> sig = {c.frame.fbp, c.frame.fbw, c.frame.psm, c.zbuf.zbp, c.zbuf.psm};
        if (!m_pending.empty() && sig != m_sig)
        {
            ++m_stats.splitsSignature;
            flushDraws();
        }
        if (!m_pending.empty() && batch.state.prim.tme && readsWrittenMemory(batch))
        {
            ++m_stats.splitsHazard;
            flushDraws();
        }
        m_sig = sig;
        noteWrites(batch);
        // A primitive that samples memory it also renders into: the CPU model reads what it just wrote, the GPU
        // reads the pre-draw contents (what a hardware renderer does). Counted so it is never a silent surprise.
        if (batch.state.prim.tme && readsWrittenMemory(batch, m_written.size() - 2)) ++m_stats.selfFeedback;
        m_pending.push_back(gpuprim::pack(batch));
        ++m_stats.prims;
        if (m_serialize) flushDraws();   // debugging: one draw per primitive
    }

    void loadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
    {
        const auto four = [](uint8_t p) { return p == 0x14 || p == 0x24 || p == 0x2C; };
        const auto eight = [](uint8_t p) { return p == 0x13 || p == 0x1B; };
        if (!four(tex0.psm) && !eight(tex0.psm)) return;
        switch (tex0.cld)
        {
        case 1: break;
        case 2: m_clutCbp[0] = tex0.cbp; break;
        case 3: m_clutCbp[1] = tex0.cbp; break;
        case 4: if (m_clutCbp[0] == tex0.cbp) return; m_clutCbp[0] = tex0.cbp; break;
        case 5: if (m_clutCbp[1] == tex0.cbp) return; m_clutCbp[1] = tex0.cbp; break;
        default: return;
        }
        flushDraws();
        bindBuffers();
        glUseProgram(m_clutProg);
        auto u = [&](const char *n, uint32_t v) { glUniform1ui(glGetUniformLocation(m_clutProg, n), v); };
        u("uPsm", tex0.psm); u("uCbp", tex0.cbp); u("uCpsm", tex0.cpsm); u("uCsm", tex0.csm); u("uCsa", tex0.csa);
        u("uCbw", texclut.cbw); u("uCou", texclut.cou); u("uCov", texclut.cov);
        {
            ScopedGpuTimer t(*this, m_stats.gpuMsClut);
            glDispatchCompute(1, 1, 1);
        }
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        ++m_stats.clutLoads;
    }

    void beginTransfer(const GSTransferCommand &cmd)
    {
        flushDraws();
        m_xfer.cmd = cmd;
        m_xfer.copied = 0;
        m_xfer.total = static_cast<uint32_t>(cmd.trxreg.rrw) * cmd.trxreg.rrh;
        m_xfer.active = (cmd.direction == 0u) && m_xfer.total != 0u;
        if (cmd.direction == 1u || cmd.direction == 2u) ++m_stats.unsupportedTransfers;
    }

    void uploadImage(const uint8_t *data, uint32_t sizeBytes)
    {
        if (!data || sizeBytes == 0u || !m_xfer.active) return;
        const uint32_t dpsm = m_xfer.cmd.bitbltbuf.dpsm;
        uint32_t bits;
        switch (dpsm)
        {
        case 0x00: case 0x30: bits = 32; break;
        case 0x01: case 0x31: bits = 24; break;
        case 0x02: case 0x0A: case 0x32: case 0x3A: bits = 16; break;
        case 0x13: case 0x1B: bits = 8; break;
        case 0x14: case 0x24: case 0x2C: bits = 4; break;
        default: return;
        }
        const uint64_t chunkPixels = static_cast<uint64_t>(sizeBytes) * 8u / bits;
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(chunkPixels, m_xfer.total - m_xfer.copied));
        if (n == 0u) return;

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buf[kSrc]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (sizeBytes + 15u) & ~15u, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeBytes, data);
        bindBuffers();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_buf[kSrc]);
        glUseProgram(m_upProg);
        auto u = [&](const char *name, uint32_t v) { glUniform1ui(glGetUniformLocation(m_upProg, name), v); };
        u("uDbp", m_xfer.cmd.bitbltbuf.dbp); u("uDbw", std::max<uint32_t>(m_xfer.cmd.bitbltbuf.dbw, 1u));
        u("uPsm", dpsm); u("uDsax", m_xfer.cmd.trxpos.dsax); u("uDsay", m_xfer.cmd.trxpos.dsay);
        u("uRrw", m_xfer.cmd.trxreg.rrw); u("uStartPixel", m_xfer.copied); u("uCount", n);
        {
            ScopedGpuTimer t(*this, m_stats.gpuMsUpload);
            glDispatchCompute((n + 63u) / 64u, 1, 1);
        }
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        m_xfer.copied += n;
        if (m_xfer.copied >= m_xfer.total) { m_xfer.active = false; ++m_stats.uploads; }
        ++m_stats.uploadChunks;
    }

    // GPU-side timing of a scope (stalls: only with GT_PROFILE).
    struct ScopedGpuTimer
    {
        GpuGsExecutor &e; double &acc; bool on;
        ScopedGpuTimer(GpuGsExecutor &ex, double &a) : e(ex), acc(a), on(ex.m_profile) { if (on) glBeginQuery(GL_TIME_ELAPSED, e.m_query); }
        ~ScopedGpuTimer()
        {
            if (!on) return;
            glEndQuery(GL_TIME_ELAPSED);
            GLuint64 ns = 0;
            glGetQueryObjectui64v(e.m_query, GL_QUERY_RESULT, &ns);
            acc += static_cast<double>(ns) / 1.0e6;
        }
    };

    void flushDraws()
    {
        if (m_pending.empty()) { m_written.clear(); return; }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buf[kPrims]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, m_pending.size() * sizeof(GpuPrim), m_pending.data(), GL_STREAM_DRAW);
        bindBuffers();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_buf[kPrims]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glBindVertexArray(m_vao);
        glUseProgram(m_drawProg);
        glUniform1ui(m_uBase, 0);
        {
            ScopedGpuTimer t(*this, m_stats.gpuMsDraw);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(m_pending.size()));
        }
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        ++m_stats.draws;
        m_pending.clear();
        m_written.clear();
    }

    // Completes all queued work and copies the VRAM back (4 MiB).
    void readVram(uint8_t *out)
    {
        flushDraws();
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buf[kVram]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, kVramBytes, out);
    }

    // Replaces the whole VRAM with a host copy (after finishing queued draws).
    void uploadVram(const uint8_t *vram)
    {
        flushDraws();
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buf[kVram]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, kVramBytes, vram);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    void finish() { flushDraws(); glFinish(); }
    const Stats &stats() const { return m_stats; }

private:
    enum { kVram = 0, kClut, kPrims, kSrc };
    static constexpr size_t kVramBytes = 4u * 1024u * 1024u;

    struct Range { uint32_t begin, end; };   // in pages (8 KiB)

    void bindBuffers()
    {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_buf[kVram]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_buf[kClut]);
    }

    // Page range a w x h pixel region of the given format and buffer width can touch (conservative).
    static Range pagesFor(uint32_t basePage, uint32_t bw, uint32_t psm, uint32_t w, uint32_t h)
    {
        uint32_t pw = 64, ph = 32;
        switch (psm)
        {
        case 0x02: case 0x0A: case 0x32: case 0x3A: pw = 64; ph = 64; break;
        case 0x13: pw = 128; ph = 64; break;
        case 0x14: pw = 128; ph = 128; break;
        default: break;
        }
        const uint32_t perRow = std::max(1u, (std::max(bw, 1u) * 64u) / pw);
        const uint32_t cols = std::max(perRow, (w + pw - 1) / pw);
        const uint32_t rows = (h + ph - 1) / ph;
        return {basePage, basePage + rows * cols + 1u};
    }

    static bool overlaps(const Range &a, const Range &b) { return a.begin < b.end && b.begin < a.end; }

    void noteWrites(const GSPrimitiveBatch &b)
    {
        const GSContext &c = b.state.context;
        const uint32_t w = c.scissor.x1 + 1u, h = c.scissor.y1 + 1u;
        m_written.push_back(pagesFor(c.frame.fbp, c.frame.fbw, c.frame.psm, w, h));
        m_written.push_back(pagesFor(c.zbuf.zbp, c.frame.fbw, c.zbuf.psm, w, h));
    }

    // Does the primitive's texture overlap a range written by m_written[from..]? (default: any earlier write)
    bool readsWrittenMemory(const GSPrimitiveBatch &b, size_t from = 0) const
    {
        const GSContext &c = b.state.context;
        const Range t = pagesFor(c.tex0.tbp0 >> 5, c.tex0.tbw, c.tex0.psm, b.state.textureWidth, b.state.textureHeight);
        for (size_t i = from; i < m_written.size(); ++i)
            if (overlaps(t, m_written[i])) return true;
        return false;
    }

    struct Transfer
    {
        GSTransferCommand cmd{};
        uint32_t copied = 0, total = 0;
        bool active = false;
    };

    GLuint m_drawProg = 0, m_clutProg = 0, m_upProg = 0, m_fbo = 0, m_vao = 0;
    GLint m_uBase = -1;
    GLuint m_buf[4] = {};
    std::vector<GpuPrim> m_pending;
    std::vector<Range> m_written;
    std::array<uint32_t, 5> m_sig{};
    std::array<uint32_t, 2> m_clutCbp{};
    Transfer m_xfer;
    Stats m_stats;
    bool m_serialize = false, m_profile = false;
    GLuint m_query = 0;
};
