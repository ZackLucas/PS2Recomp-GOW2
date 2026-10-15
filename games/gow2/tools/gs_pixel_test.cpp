// gow2_gs_pixel_test: GPU per-pixel pipeline (shaders/gs_pixel.glsl) vs GSCpuBackend::WritePixel,
// exercised through the public Submit(POINT) path. Cases run in order on one GPU thread so the
// read-modify-write sequence is identical; the whole 4 MiB VRAM is compared after each run.
#include "gs_tables_gl.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_memory.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

static std::string slurp(const std::string &path)
{
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(2); }
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

struct Case
{
    int32_t x, y;
    uint32_t z, r, g, b, a, fog;
    uint32_t fbp, fbw, fpsm, fbmsk, zbp, zpsm, zmask, test, alphaLo, fix, fba;
    int32_t sx0, sy0, sx1, sy1;
    uint32_t abe, fge, pabe, fogR, fogG, fogB, pad0, pad1;
};

int main()
{
    if (!gl::initContext()) { fprintf(stderr, "no GL context\n"); return 1; }
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    GSMem::InitLookupTables();
    gl::bindGsTables();
    const std::string dir = GOW2_SHADER_DIR;

    const std::string src =
        "#version 450\nlayout(local_size_x = 1) in;\n"
        "layout(std430, binding = 0) buffer Vram { uint vram[]; };\n"
        "struct Case { int x, y; uint z, r, g, b, a, fog; uint fbp, fbw, fpsm, fbmsk, zbp, zpsm, zmask, test, alphaLo, fix, fba;\n"
        "              int sx0, sy0, sx1, sy1; uint abe, fge, pabe, fogR, fogG, fogB, pad0, pad1; };\n"
        "layout(std430, binding = 1) readonly buffer Cases { Case c[]; };\n" +
        slurp(dir + "/gs_vram_tables.glsl") + slurp(dir + "/gs_vram.glsl") + slurp(dir + "/gs_pixel_deps.glsl") +
        slurp(dir + "/gs_pixel.glsl") +
        "void main(){\n"
        "  for (uint i = 0u; i < uint(c.length()); ++i) {\n"
        "    GsPixelState s;\n"
        "    s.fbp = c[i].fbp; s.fbw = c[i].fbw; s.fpsm = c[i].fpsm; s.fbmsk = c[i].fbmsk;\n"
        "    s.zbp = c[i].zbp; s.zpsm = c[i].zpsm; s.zmask = c[i].zmask; s.test = c[i].test;\n"
        "    s.alphaLo = c[i].alphaLo; s.fix = c[i].fix; s.fba = c[i].fba;\n"
        "    s.sx0 = c[i].sx0; s.sy0 = c[i].sy0; s.sx1 = c[i].sx1; s.sy1 = c[i].sy1;\n"
        "    s.abe = c[i].abe; s.fge = c[i].fge; s.pabe = c[i].pabe;\n"
        "    s.fogR = c[i].fogR; s.fogG = c[i].fogG; s.fogB = c[i].fogB;\n"
        "    gsWritePixel(s, c[i].x, c[i].y, c[i].z, c[i].r, c[i].g, c[i].b, c[i].a, c[i].fog);\n"
        "  }\n}\n";
    GLuint prog = gl::computeProgram(src.c_str());

    const uint32_t fmts[] = {0x00, 0x01, 0x02, 0x0A, 0x30, 0x31, 0x32, 0x3A};
    const size_t kVram = GSMem::MEMORY_SIZE;
    size_t totalBad = 0, totalCases = 0, totalChanged = 0;
    const int kRuns = 24, kCases = 3000;

    GLuint bufs[2];
    glGenBuffers(2, bufs);

    for (int run = 0; run < kRuns; ++run)
    {
        std::mt19937 rng(9000 + run);
        std::vector<uint8_t> initial(kVram);
        for (size_t i = 0; i < kVram; i += 4) { uint32_t v = rng(); memcpy(&initial[i], &v, 4); }
        std::vector<uint8_t> cpuVram = initial;
        GSCpuBackend cpu;
        cpu.Initialize(cpuVram.data(), static_cast<uint32_t>(kVram));

        std::vector<Case> cases(kCases);
        for (auto &c : cases)
        {
            c.x = rng() % 24; c.y = rng() % 24;
            c.z = (rng() & 1u) ? (rng() % 16u) : (rng() % (1u << 24));
            c.r = rng() & 0xFF; c.g = rng() & 0xFF; c.b = rng() & 0xFF; c.a = rng() & 0xFF; c.fog = rng() & 0xFF;
            c.fbp = rng() % 4u; c.fbw = rng() % 3u; c.fpsm = fmts[rng() % 8];
            c.fbmsk = (rng() % 4 == 0) ? rng() : 0u;
            c.zbp = rng() % 8u; c.zpsm = fmts[rng() % 8]; c.zmask = (rng() % 4 == 0);
            const uint32_t ate = rng() & 1u, atst = rng() % 8u, aref = (rng() % 3 == 0) ? c.a : (rng() & 0xFF);
            const uint32_t afail = rng() % 4u, date = rng() & 1u, datm = rng() & 1u, zte = 1u, ztst = rng() % 4u;
            c.test = ate | (atst << 1) | (aref << 4) | (afail << 12) | (date << 14) | (datm << 15) | (zte << 16) | (ztst << 17);
            c.alphaLo = (rng() % 3) | ((rng() % 3) << 2) | ((rng() % 3) << 4) | ((rng() % 3) << 6);
            c.fix = rng() & 0xFF;
            c.fba = rng() & 1u;
            c.sx0 = rng() % 8; c.sy0 = rng() % 8; c.sx1 = 12 + rng() % 16; c.sy1 = 12 + rng() % 16;
            c.abe = rng() & 1u; c.fge = (rng() % 4 == 0); c.pabe = (rng() % 4 == 0);
            c.fogR = rng() & 0xFF; c.fogG = rng() & 0xFF; c.fogB = rng() & 0xFF;
            c.pad0 = c.pad1 = 0;
        }

        for (const Case &c : cases)
        {
            GSPrimitiveBatch batch{};
            batch.vertexCount = 1;
            GSVertex &v = batch.vertices[0];
            v.x = static_cast<float>(c.x); v.y = static_cast<float>(c.y); v.z = static_cast<double>(c.z);
            v.r = static_cast<uint8_t>(c.r); v.g = static_cast<uint8_t>(c.g); v.b = static_cast<uint8_t>(c.b);
            v.a = static_cast<uint8_t>(c.a); v.fog = static_cast<uint8_t>(c.fog);
            GSDrawState &st = batch.state;
            st.prim.type = GS_PRIM_POINT; st.prim.abe = c.abe != 0; st.prim.fge = c.fge != 0;
            st.pabe = c.pabe != 0; st.fogR = static_cast<uint8_t>(c.fogR); st.fogG = static_cast<uint8_t>(c.fogG);
            st.fogB = static_cast<uint8_t>(c.fogB);
            GSContext &cx = st.context;
            cx.frame.fbp = c.fbp; cx.frame.fbw = c.fbw; cx.frame.psm = static_cast<uint8_t>(c.fpsm); cx.frame.fbmsk = c.fbmsk;
            cx.zbuf.zbp = c.zbp; cx.zbuf.psm = static_cast<uint8_t>(c.zpsm); cx.zbuf.zmask = c.zmask != 0;
            cx.scissor.x0 = static_cast<uint16_t>(c.sx0); cx.scissor.y0 = static_cast<uint16_t>(c.sy0);
            cx.scissor.x1 = static_cast<uint16_t>(c.sx1); cx.scissor.y1 = static_cast<uint16_t>(c.sy1);
            cx.test = c.test;
            cx.alpha = static_cast<uint64_t>(c.alphaLo) | (static_cast<uint64_t>(c.fix) << 32);
            cx.fba = c.fba;
            cpu.Submit(batch);
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[0]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, kVram, initial.data(), GL_DYNAMIC_COPY);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[1]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, cases.size() * sizeof(Case), cases.data(), GL_STATIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufs[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bufs[1]);
        glUseProgram(prog);
        glDispatchCompute(1, 1, 1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        std::vector<uint8_t> gpuVram(kVram);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[0]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, kVram, gpuVram.data());

        size_t bad = 0, changed = 0;
        for (size_t i = 0; i < kVram; i += 4)
        {
            uint32_t a, b, c0;
            memcpy(&a, &cpuVram[i], 4); memcpy(&b, &gpuVram[i], 4); memcpy(&c0, &initial[i], 4);
            changed += (a != c0);
            if (a != b)
            {
                if (bad < 3) printf("  MISMATCH run=%d word=%zu cpu=%08x gpu=%08x initial=%08x\n", run, i / 4, a, b, c0);
                ++bad;
            }
        }
        totalBad += bad; totalCases += cases.size(); totalChanged += changed;
        if (bad) printf("run %d: %zu mismatching words (%zu words changed by CPU)\n", run, bad, changed);
    }
    printf("%d runs x %d cases = %zu pixel writes, %zu words changed in total, %zu mismatching words\n",
           kRuns, kCases, totalCases, totalChanged, totalBad);
    printf(totalBad ? "FAIL\n" : "PASS: GPU pixel pipeline matches GSCpuBackend::WritePixel\n");
    return totalBad ? 1 : 0;
}
