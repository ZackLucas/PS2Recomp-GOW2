// gow2_gs_draw_test: GPU primitive rendering (gs_draw.vert/frag) vs GSCpuBackend::Submit.
// Random triangles, sprites and points with fully random state (texture formats, wrap modes,
// filtering, texture functions, blend, tests, masks). Primitives are submitted in the same order
// to both; the whole 4 MiB VRAM is compared after each run.
#include "gs_prim_pack.h"
#include "gs_tables_gl.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_memory.h"

#include <algorithm>
#include <array>
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

int main(int argc, char **argv)
{
    const int kRuns = argc > 1 ? atoi(argv[1]) : 40;
    const int kPrims = argc > 2 ? atoi(argv[2]) : 400;
    if (!gl::initContext()) { fprintf(stderr, "no GL context\n"); return 1; }
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    GSMem::InitLookupTables();
    gl::bindGsTables();
    const std::string dir = GOW2_SHADER_DIR;

    const std::string vram = slurp(dir + "/gs_vram_tables.glsl") + slurp(dir + "/gs_vram.glsl");

    // CLUT load program (same as the CLUT test)
    std::string load = slurp(dir + "/gs_clut_load.glsl");
    load.replace(load.find("#include_gs_vram_here"), 21, vram);
    GLuint loadProg = gl::computeProgram(("#version 450\n" + load).c_str());

    const std::string common = slurp(dir + "/gs_draw_common.glsl");
    const std::string vsSrc = "#version 450\n" + common + slurp(dir + "/gs_draw.vert");
    const std::string fsSrc =
        "#version 450\n#extension GL_ARB_fragment_shader_interlock : require\n"
        "#pragma optionNV(fastmath off)\n#pragma optionNV(fastprecision off)\n"
        "layout(std430, binding = 0) coherent buffer Vram { uint vram[]; };\n"
        "layout(std430, binding = 2) coherent readonly buffer Clut { uint clut[]; };\n" +
        vram + slurp(dir + "/gs_clut.glsl") + slurp(dir + "/gs_pixel.glsl") + common +
        slurp(dir + "/gs_texture.glsl") + slurp(dir + "/gs_draw.frag");
    GLuint prog = gl::link(gl::compile(GL_VERTEX_SHADER, vsSrc.c_str()), gl::compile(GL_FRAGMENT_SHADER, fsSrc.c_str()));

    GLuint fbo, vao;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, 2048);
    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, 2048);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "FBO incomplete\n"); return 1; }
    glViewport(0, 0, 2048, 2048);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);

    GLuint bufs[3];   // vram, clut, prims
    glGenBuffers(3, bufs);

    // Elimination filters (environment): GT_TYPES=pstfq (point,sprite,triangle...), GT_NOTEX, GT_SIMPLEFMT,
    // GT_FLAT, GT_NOTESTS, GT_NOBLEND, GT_NOFOG, GT_NOSCISSOR, GT_NOFILTER, GT_TFX=n, GT_TEXFMT=hex
    auto envOn = [](const char *n) { const char *e = getenv(n); return e && *e && *e != '0'; };
    const char *typesEnv = getenv("GT_TYPES");
    const std::string types = typesEnv ? typesEnv : "pstfq";
    const bool noTex = envOn("GT_NOTEX"), simpleFmt = envOn("GT_SIMPLEFMT"), flat = envOn("GT_FLAT"),
               noTests = envOn("GT_NOTESTS"), noBlend = envOn("GT_NOBLEND"), noFog = envOn("GT_NOFOG"),
               noFilter = envOn("GT_NOFILTER");
    const char *tfxEnv = getenv("GT_TFX"), *texFmtEnv = getenv("GT_TEXFMT");
    const uint32_t fmts[] = {0x00, 0x01, 0x02, 0x0A, 0x30, 0x31, 0x32, 0x3A};
    const uint32_t texFmts[] = {0x00, 0x01, 0x02, 0x0A, 0x13, 0x14, 0x1B, 0x24, 0x2C, 0x30, 0x31, 0x32, 0x3A};
    const uint32_t cpsms[] = {0x00, 0x01, 0x02, 0x0A};
    const size_t kVram = GSMem::MEMORY_SIZE;
    size_t totalBad = 0, totalChanged = 0, totalPrims = 0;
    size_t byType[7] = {};

    for (int run = 0; run < kRuns; ++run)
    {
        std::mt19937 rng((getenv("GT_SEED") ? static_cast<uint32_t>(atoi(getenv("GT_SEED"))) : 31000u) + run);
        auto R = [&](uint32_t n) { return static_cast<uint32_t>(rng() % n); };
        std::vector<uint8_t> initial(kVram);
        for (size_t i = 0; i < kVram; i += 4) { uint32_t v = rng(); memcpy(&initial[i], &v, 4); }
        std::vector<uint8_t> cpuVram = initial;
        GSCpuBackend cpu;
        cpu.Initialize(cpuVram.data(), static_cast<uint32_t>(kVram));

        // GPU buffers
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[0]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, kVram, initial.data(), GL_DYNAMIC_COPY);
        std::vector<uint32_t> zeroClut(512, 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[1]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 512 * 4, zeroClut.data(), GL_DYNAMIC_COPY);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufs[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bufs[1]);

        // a few CLUT loads on both sides
        for (int l = 0; l < 3; ++l)
        {
            GSTex0Reg t{};
            const uint32_t srcs[] = {0x13, 0x14};
            t.psm = static_cast<uint8_t>(srcs[R(2)]);
            t.cbp = 8192 + R(1000); t.cpsm = static_cast<uint8_t>(cpsms[R(4)]);
            t.csm = static_cast<uint8_t>(R(2)); t.csa = static_cast<uint8_t>(R(32)); t.cld = 1;
            GSTexClutReg tc{};
            tc.cbw = static_cast<uint8_t>(1 + R(4)); tc.cou = static_cast<uint8_t>(R(8)); tc.cov = static_cast<uint16_t>(R(16));
            cpu.LoadClut(t, tc);
            glUseProgram(loadProg);
            auto u = [&](const char *n, uint32_t v) { glUniform1ui(glGetUniformLocation(loadProg, n), v); };
            u("uPsm", t.psm); u("uCbp", t.cbp); u("uCpsm", t.cpsm); u("uCsm", t.csm); u("uCsa", t.csa);
            u("uCbw", tc.cbw); u("uCou", tc.cou); u("uCov", tc.cov);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufs[0]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bufs[1]);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // Render-target mapping (FB/Z base, width, formats). It changes every few dozen primitives, like a
        // game switching passes; the GPU path must split its draw and add a barrier at each change.
        uint32_t sigFbp = 0, sigFbw = 1, sigFpsm = 0, sigZbp = 8, sigZpsm = 0x30;
        std::vector<GpuPrim> packed;
        std::vector<std::array<uint32_t, 5>> sigs;
        std::string runLog;
        packed.reserve(kPrims);
        for (int i = 0; i < kPrims; ++i)
        {
            GSPrimitiveBatch batch{};
            GSDrawState &st = batch.state;
            GSContext &cx = st.context;

            GSPrimType type;
            for (;;)
            {
                const uint32_t roll = R(100);
                type = roll < 45 ? GS_PRIM_TRIANGLE : roll < 55 ? GS_PRIM_TRISTRIP : roll < 60 ? GS_PRIM_TRIFAN
                       : roll < 88 ? GS_PRIM_SPRITE : GS_PRIM_POINT;
                const char c = type == GS_PRIM_POINT ? 'p' : type == GS_PRIM_SPRITE ? 's' : type == GS_PRIM_TRIANGLE ? 't'
                               : type == GS_PRIM_TRISTRIP ? 'f' : 'q';
                if (types.find(c) != std::string::npos) break;
            }
            st.prim.type = type;
            batch.vertexCount = (type == GS_PRIM_POINT) ? 1 : (type == GS_PRIM_SPRITE) ? 2 : 3;
            st.prim.iip = R(2) != 0; st.prim.tme = R(10) < 7; st.prim.fge = R(4) == 0;
            st.prim.abe = R(2) != 0; st.prim.fst = R(2) != 0;
            st.pabe = R(4) == 0;
            st.fogR = static_cast<uint8_t>(rng()); st.fogG = static_cast<uint8_t>(rng()); st.fogB = static_cast<uint8_t>(rng());
            st.texa.ta0 = static_cast<uint8_t>(rng()); st.texa.ta1 = static_cast<uint8_t>(rng()); st.texa.aem = R(2) != 0;

            if (i % 25 == 0)
            {
                sigFbp = R(3); sigFbw = 1 + R(2); sigFpsm = fmts[R(8)];
                sigZbp = 8 + R(8); sigZpsm = fmts[R(8)];
            }
            cx.frame.fbp = sigFbp; cx.frame.fbw = sigFbw; cx.frame.psm = static_cast<uint8_t>(sigFpsm);
            cx.frame.fbmsk = (R(5) == 0) ? static_cast<uint32_t>(rng()) : 0u;
            cx.zbuf.zbp = sigZbp; cx.zbuf.psm = static_cast<uint8_t>(sigZpsm); cx.zbuf.zmask = R(4) == 0;
            cx.scissor.x0 = static_cast<uint16_t>(R(6)); cx.scissor.y0 = static_cast<uint16_t>(R(6));
            cx.scissor.x1 = static_cast<uint16_t>(40 + R(24)); cx.scissor.y1 = static_cast<uint16_t>(40 + R(24));
            cx.xyoffset.ofx = static_cast<uint16_t>(R(8) << 4); cx.xyoffset.ofy = static_cast<uint16_t>(R(8) << 4);

            const uint32_t ate = R(2), atst = R(8), aref = rng() & 0xFF, afail = R(4), date = R(2), datm = R(2), ztst = R(4);
            cx.test = ate | (atst << 1) | (static_cast<uint64_t>(aref) << 4) | (static_cast<uint64_t>(afail) << 12) |
                      (static_cast<uint64_t>(date) << 14) | (static_cast<uint64_t>(datm) << 15) | (1ull << 16) |
                      (static_cast<uint64_t>(ztst) << 17);
            cx.alpha = static_cast<uint64_t>(R(3) | (R(3) << 2) | (R(3) << 4) | (R(3) << 6)) |
                       (static_cast<uint64_t>(rng() & 0xFF) << 32);
            cx.fba = R(2);

            cx.tex0.tbp0 = 4096 + R(1024); cx.tex0.tbw = static_cast<uint8_t>(1 + R(2));
            cx.tex0.psm = static_cast<uint8_t>(texFmts[R(13)]);
            cx.tex0.tw = static_cast<uint8_t>(3 + R(5)); cx.tex0.th = static_cast<uint8_t>(3 + R(5));
            cx.tex0.tcc = static_cast<uint8_t>(R(2)); cx.tex0.tfx = static_cast<uint8_t>(R(4));
            cx.tex0.cbp = 8192 + R(1000); cx.tex0.cpsm = static_cast<uint8_t>(cpsms[R(4)]);
            cx.tex0.csm = static_cast<uint8_t>(R(2)); cx.tex0.csa = static_cast<uint8_t>(R(32));
            const uint32_t mmag = R(2), mmin = R(8);
            cx.tex1 = (static_cast<uint64_t>(mmag) << 5) | (static_cast<uint64_t>(mmin) << 6);
            st.linearFilter = mmag != 0u || mmin == 1u || (mmin & 0x4u) != 0u;
            const uint64_t wms = R(4), wmt = R(4);
            cx.clamp = wms | (wmt << 2) | (static_cast<uint64_t>(R(128)) << 4) | (static_cast<uint64_t>(R(128)) << 14) |
                       (static_cast<uint64_t>(R(128)) << 24) | (static_cast<uint64_t>(R(128)) << 34);
            st.textureWidth = static_cast<uint16_t>(1u << cx.tex0.tw);
            st.textureHeight = static_cast<uint16_t>(1u << cx.tex0.th);

            for (int v = 0; v < batch.vertexCount; ++v)
            {
                GSVertex &gv = batch.vertices[static_cast<size_t>(v)];
                gv.x = static_cast<float>(R(80 * 16)) / 16.0f - 8.0f;
                gv.y = static_cast<float>(R(80 * 16)) / 16.0f - 8.0f;
                if (type == GS_PRIM_SPRITE && v == 1)
                {
                    gv.x = batch.vertices[0].x + static_cast<float>(R(40 * 16)) / 16.0f;
                    gv.y = batch.vertices[0].y + static_cast<float>(R(40 * 16)) / 16.0f;
                }
                gv.z = static_cast<double>(R(1u << 24));
                gv.r = static_cast<uint8_t>(rng()); gv.g = static_cast<uint8_t>(rng());
                gv.b = static_cast<uint8_t>(rng()); gv.a = static_cast<uint8_t>(rng());
                gv.s = static_cast<float>(R(2000)) / 1000.0f - 0.5f;
                gv.t = static_cast<float>(R(2000)) / 1000.0f - 0.5f;
                gv.q = R(50) == 0 ? 0.0f : 0.5f + static_cast<float>(R(1500)) / 1000.0f;
                gv.u = static_cast<uint16_t>(R(128 * 16)); gv.v = static_cast<uint16_t>(R(128 * 16));
                gv.fog = static_cast<uint8_t>(rng());
            }

            if (noTex) st.prim.tme = false;
            if (flat) st.prim.iip = false;
            if (noBlend) { st.prim.abe = false; st.pabe = false; }
            if (noFog) st.prim.fge = false;
            if (noFilter) st.linearFilter = false;
            if (noTests) { cx.test = (1ull << 16) | (1ull << 17); cx.frame.fbmsk = 0; cx.zbuf.zmask = false; }   // ZTE, ZTST=ALWAYS
            if (simpleFmt) { cx.frame.psm = 0x00; cx.zbuf.psm = 0x30; }
            if (tfxEnv) cx.tex0.tfx = static_cast<uint8_t>(atoi(tfxEnv));
            if (texFmtEnv) cx.tex0.psm = static_cast<uint8_t>(strtoul(texFmtEnv, nullptr, 16));
            if (kPrims <= 4)
            {
                char buf[512];
                snprintf(buf, sizeof buf,
                         "  prim#%d type=%d ofx=%d ofy=%d scissor=(%u,%u)-(%u,%u) fbp=%u fbw=%u fpsm=%02x zbp=%u zpsm=%02x test=%llx\n",
                         i, static_cast<int>(type), cx.xyoffset.ofx >> 4, cx.xyoffset.ofy >> 4, cx.scissor.x0, cx.scissor.y0,
                         cx.scissor.x1, cx.scissor.y1, cx.frame.fbp, cx.frame.fbw, cx.frame.psm, cx.zbuf.zbp, cx.zbuf.psm,
                         static_cast<unsigned long long>(cx.test));
                runLog += buf;
                for (int v = 0; v < batch.vertexCount; ++v)
                {
                    snprintf(buf, sizeof buf, "      v%d x=%.4f y=%.4f z=%.0f rgba=%u,%u,%u,%u\n", v, batch.vertices[v].x,
                             batch.vertices[v].y, batch.vertices[v].z, batch.vertices[v].r, batch.vertices[v].g,
                             batch.vertices[v].b, batch.vertices[v].a);
                    runLog += buf;
                }
            }
            cpu.Submit(batch);
            packed.push_back(gpuprim::pack(batch));
            sigs.push_back({cx.frame.fbp, cx.frame.fbw, cx.frame.psm, cx.zbuf.zbp, cx.zbuf.psm});
            ++byType[static_cast<size_t>(type)];
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[2]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, packed.size() * sizeof(GpuPrim), packed.data(), GL_STATIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufs[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bufs[1]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, bufs[2]);
        glUseProgram(prog);
        const GLint uBase = glGetUniformLocation(prog, "uBaseInstance");
        size_t draws = 0;
        for (size_t first = 0; first < packed.size();)
        {
            size_t last = first + 1;
            while (last < packed.size() && sigs[last] == sigs[first]) ++last;
            glUniform1ui(uBase, static_cast<GLuint>(first));
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(last - first));
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);   // target mapping changes: make writes visible
            first = last;
            ++draws;
        }
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        (void)draws;

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
        totalBad += bad; totalChanged += changed; totalPrims += packed.size();
        if (bad && kPrims <= 4) printf("%s", runLog.c_str());
        printf("run %2d: %d primitives, %6zu words changed, %zu mismatching\n", run, kPrims, changed, bad);
    }
    printf("%zu primitives (tri %zu, strip %zu, fan %zu, sprite %zu, point %zu), %zu words changed, %zu mismatching words\n",
           totalPrims, byType[3], byType[4], byType[5], byType[6], byType[0], totalChanged, totalBad);
    printf(totalBad ? "FAIL\n" : "PASS: GPU primitive rendering matches GSCpuBackend::Submit\n");
    return totalBad ? 1 : 0;
}
