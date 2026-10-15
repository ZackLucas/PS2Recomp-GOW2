// gow2_gs_clut_test: GPU CLUT load + palette lookup vs GSCpuBackend (LoadClut + LookupCLUT).
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

struct Query { uint32_t index, cpsm, csm, csa, sourcePsm, ta0, aem, ta1; };

int main()
{
    if (!gl::initContext()) { fprintf(stderr, "no GL context\n"); return 1; }
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    GSMem::InitLookupTables();
    gl::bindGsTables();
    const std::string dir = GOW2_SHADER_DIR;
    const std::string vramSrc = slurp(dir + "/gs_vram_tables.glsl") + slurp(dir + "/gs_vram.glsl");

    std::string load = slurp(dir + "/gs_clut_load.glsl");
    load.replace(load.find("#include_gs_vram_here"), 21, vramSrc);
    GLuint loadProg = gl::computeProgram(("#version 450\n" + load).c_str());

    const std::string lookup =
        "#version 450\nlayout(local_size_x = 256) in;\n"
        "layout(std430, binding = 0) buffer Vram { uint vram[]; };\n"
        "layout(std430, binding = 2) buffer Clut { uint clut[]; };\n"
        "struct Q { uint index, cpsm, csm, csa, sourcePsm, ta0, aem, ta1; };\n"
        "layout(std430, binding = 3) readonly buffer Queries { Q q[]; };\n"
        "layout(std430, binding = 4) writeonly buffer Results { uint r[]; };\n" +
        vramSrc + slurp(dir + "/gs_clut.glsl") +
        "void main(){ uint i = gl_GlobalInvocationID.x; if (i >= q.length()) return;\n"
        "  r[i] = gsClutLookup(q[i].index, q[i].cpsm, q[i].csm, q[i].csa, q[i].sourcePsm, q[i].ta0, q[i].aem, q[i].ta1); }\n";
    GLuint lookupProg = gl::computeProgram(lookup.c_str());

    std::mt19937 rng(4242);
    const size_t kVram = GSMem::MEMORY_SIZE;
    std::vector<uint8_t> vram(kVram);
    for (size_t i = 0; i < kVram; i += 4) { uint32_t v = rng(); memcpy(&vram[i], &v, 4); }

    GSCpuBackend cpu;
    cpu.Initialize(vram.data(), static_cast<uint32_t>(kVram));

    GLuint bufs[4];
    glGenBuffers(4, bufs);   // vram, clut, queries, results
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, kVram, vram.data(), GL_STATIC_DRAW);
    std::vector<uint32_t> zeroClut(512, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 512 * 4, zeroClut.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufs[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bufs[1]);

    const uint32_t srcPsms[] = {0x13, 0x14, 0x1B, 0x24, 0x2C};
    const uint32_t cpsms[] = {0x00, 0x01, 0x02, 0x0A};
    uint32_t gpuCbp[2] = {0, 0};
    size_t loads = 0, skipped = 0, lookups = 0, bad = 0;
    const int kIterations = 6000;

    for (int it = 0; it < kIterations; ++it)
    {
        GSTex0Reg tex0{};
        tex0.psm = static_cast<uint8_t>(srcPsms[rng() % 5]);
        tex0.cbp = rng() % 16384u;
        tex0.cpsm = static_cast<uint8_t>(cpsms[rng() % 4]);
        tex0.csm = static_cast<uint8_t>(rng() & 1u);
        tex0.csa = static_cast<uint8_t>(rng() % 32u);
        tex0.cld = static_cast<uint8_t>(rng() % 8u);
        if (rng() % 4 == 0) tex0.cbp = (it & 1) ? 77u : 78u;        // make CLD 4/5 hit the "same CBP" case
        GSTexClutReg clut{};
        clut.cbw = static_cast<uint8_t>(rng() % 17u);
        clut.cou = static_cast<uint8_t>(rng() % 64u);
        clut.cov = static_cast<uint16_t>(rng() % 200u);
        GSTexaReg texa{};
        texa.ta0 = static_cast<uint8_t>(rng()); texa.ta1 = static_cast<uint8_t>(rng()); texa.aem = (rng() & 1u) != 0u;

        cpu.LoadClut(tex0, clut);

        // host-side CLD control flow (identical to GSCpuBackend::LoadClut)
        bool doLoad = false;
        switch (tex0.cld)
        {
        case 1: doLoad = true; break;
        case 2: gpuCbp[0] = tex0.cbp; doLoad = true; break;
        case 3: gpuCbp[1] = tex0.cbp; doLoad = true; break;
        case 4: if (gpuCbp[0] != tex0.cbp) { gpuCbp[0] = tex0.cbp; doLoad = true; } break;
        case 5: if (gpuCbp[1] != tex0.cbp) { gpuCbp[1] = tex0.cbp; doLoad = true; } break;
        default: break;
        }
        if (doLoad)
        {
            ++loads;
            glUseProgram(loadProg);
            auto u = [&](const char *n, uint32_t v) { glUniform1ui(glGetUniformLocation(loadProg, n), v); };
            u("uPsm", tex0.psm); u("uCbp", tex0.cbp); u("uCpsm", tex0.cpsm); u("uCsm", tex0.csm); u("uCsa", tex0.csa);
            u("uCbw", clut.cbw); u("uCou", clut.cou); u("uCov", clut.cov);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        else ++skipped;

        // lookups: every index x every source format with the loaded parameters, plus random ones
        std::vector<Query> qs;
        for (uint32_t sp : srcPsms)
            for (uint32_t i = 0; i < 256; ++i)
                qs.push_back({i, tex0.cpsm, tex0.csm, tex0.csa, sp, texa.ta0, texa.aem ? 1u : 0u, texa.ta1});
        for (int i = 0; i < 256; ++i)
            qs.push_back({rng() % 256u, cpsms[rng() % 4], rng() & 1u, rng() % 32u, srcPsms[rng() % 5], texa.ta0,
                          texa.aem ? 1u : 0u, texa.ta1});

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[2]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, qs.size() * sizeof(Query), qs.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[3]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, qs.size() * 4, nullptr, GL_STREAM_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, bufs[2]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, bufs[3]);
        glUseProgram(lookupProg);
        glDispatchCompute(static_cast<GLuint>((qs.size() + 255) / 256), 1, 1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        std::vector<uint32_t> gpu(qs.size());
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, gpu.size() * 4, gpu.data());

        for (size_t i = 0; i < qs.size(); ++i)
        {
            GSTexaReg tx{}; tx.ta0 = static_cast<uint8_t>(qs[i].ta0); tx.aem = qs[i].aem != 0; tx.ta1 = static_cast<uint8_t>(qs[i].ta1);
            const uint32_t ref = cpu.LookupClutForTest(tx, static_cast<uint8_t>(qs[i].index), static_cast<uint8_t>(qs[i].cpsm),
                                                      static_cast<uint8_t>(qs[i].csm), static_cast<uint8_t>(qs[i].csa),
                                                      static_cast<uint8_t>(qs[i].sourcePsm));
            ++lookups;
            if (ref != gpu[i])
            {
                if (bad < 5)
                    printf("  MISMATCH it=%d idx=%u cpsm=%02x csm=%u csa=%u src=%02x cpu=%08x gpu=%08x (tex0 psm=%02x cld=%u)\n",
                           it, qs[i].index, qs[i].cpsm, qs[i].csm, qs[i].csa, qs[i].sourcePsm, ref, gpu[i], tex0.psm, tex0.cld);
                ++bad;
            }
        }
    }
    printf("%d iterations: %zu CLUT loads, %zu skipped by CLD, %zu lookups, %zu mismatches\n", kIterations, loads, skipped, lookups, bad);
    printf(bad ? "FAIL\n" : "PASS: GPU CLUT load + lookup match GSCpuBackend\n");
    return bad ? 1 : 0;
}
