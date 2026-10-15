// gow2_gs_addr_test: checks the GLSL VRAM addressing (shaders/gs_vram.glsl) against the runtime's
// C++ implementation (GSMem::Read*) on millions of random reads, for every pixel storage mode.
#include "gs_tables_gl.h"
#include "runtime/gs/ps2_gs_memory.h"

#include <cstdint>
#include <cstdio>
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

struct Query { uint32_t psm, bp, bw, x, y, pad0, pad1, pad2; };

static uint32_t cpuRead(uint8_t *vram, const Query &q)
{
    using namespace GSMem;
    switch (q.psm)
    {
    case 0x00: return ReadCT32(vram, q.bp, q.bw, q.x, q.y);
    case 0x01: return ReadCT24(vram, q.bp, q.bw, q.x, q.y);
    case 0x02: return ReadCT16(vram, q.bp, q.bw, q.x, q.y);
    case 0x0A: return ReadCT16S(vram, q.bp, q.bw, q.x, q.y);
    case 0x13: return ReadP8(vram, q.bp, q.bw, q.x, q.y);
    case 0x14: return ReadP4(vram, q.bp, q.bw, q.x, q.y);
    case 0x1B: return ReadP8H(vram, q.bp, q.bw, q.x, q.y);
    case 0x24: return ReadP4HL(vram, q.bp, q.bw, q.x, q.y);
    case 0x2C: return ReadP4HH(vram, q.bp, q.bw, q.x, q.y);
    case 0x30: return ReadZ32(vram, q.bp, q.bw, q.x, q.y);
    case 0x31: return ReadZ24(vram, q.bp, q.bw, q.x, q.y);
    case 0x32: return ReadZ16(vram, q.bp, q.bw, q.x, q.y);
    case 0x3A: return ReadZ16S(vram, q.bp, q.bw, q.x, q.y);
    }
    return 0;
}

int main()
{
    if (!gl::initContext()) { fprintf(stderr, "no GL context\n"); return 1; }
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    GSMem::InitLookupTables();
    gl::bindGsTables();

    const std::string dir = GOW2_SHADER_DIR;
    const std::string src = "#version 450\nlayout(local_size_x = 256) in;\n"
        "layout(std430, binding = 0) buffer Vram { uint vram[]; };\n"
        "struct Q { uint psm, bp, bw, x, y, p0, p1, p2; };\n"
        "layout(std430, binding = 1) readonly buffer Queries { Q q[]; };\n"
        "layout(std430, binding = 2) writeonly buffer Results { uint r[]; };\n" +
        slurp(dir + "/gs_vram_tables.glsl") + slurp(dir + "/gs_vram.glsl") +
        "void main(){ uint i = gl_GlobalInvocationID.x; if (i >= q.length()) return;\n"
        "  r[i] = gsReadRaw(q[i].psm, q[i].bp, q[i].bw, q[i].x, q[i].y); }\n";
    GLuint prog = gl::computeProgram(src.c_str());

    std::mt19937 rng(12345);
    std::vector<uint8_t> vram(GSMem::MEMORY_SIZE);
    for (size_t i = 0; i < vram.size(); i += 4) { uint32_t v = rng(); memcpy(&vram[i], &v, 4); }

    const uint32_t psms[] = {0x00, 0x01, 0x02, 0x0A, 0x13, 0x14, 0x1B, 0x24, 0x2C, 0x30, 0x31, 0x32, 0x3A};
    const size_t perPsm = 1u << 20;
    std::vector<Query> queries;
    queries.reserve(perPsm * 13);
    for (uint32_t psm : psms)
        for (size_t i = 0; i < perPsm; ++i)
        {
            Query q{};
            q.psm = psm;
            q.bp = rng() % 16384u;                 // any block address, including ones that wrap
            q.bw = 1 + rng() % 16u;                // odd widths included on purpose
            q.x = rng() % 2048u;
            q.y = rng() % 2048u;
            queries.push_back(q);
        }

    GLuint bufs[3];
    glGenBuffers(3, bufs);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vram.size(), vram.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, queries.size() * sizeof(Query), queries.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[2]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, queries.size() * 4, nullptr, GL_DYNAMIC_READ);
    for (int i = 0; i < 3; ++i) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, bufs[i]);

    glUseProgram(prog);
    glDispatchCompute(GLuint((queries.size() + 255) / 256), 1, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    std::vector<uint32_t> gpu(queries.size());
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, gpu.size() * 4, gpu.data());

    size_t totalBad = 0;
    for (size_t p = 0; p < 13; ++p)
    {
        size_t bad = 0;
        for (size_t i = p * perPsm; i < (p + 1) * perPsm; ++i)
        {
            const uint32_t ref = cpuRead(vram.data(), queries[i]);
            if (ref != gpu[i])
            {
                if (bad < 3)
                    printf("  MISMATCH psm=%02x bp=%u bw=%u x=%u y=%u cpu=%08x gpu=%08x\n", queries[i].psm, queries[i].bp,
                           queries[i].bw, queries[i].x, queries[i].y, ref, gpu[i]);
                ++bad;
            }
        }
        printf("psm 0x%02x: %zu reads, %zu mismatches\n", psms[p], perPsm, bad);
        totalBad += bad;
    }
    printf(totalBad ? "FAIL: %zu mismatches\n" : "PASS: GPU addressing matches the C++ implementation\n", totalBad);
    return totalBad ? 1 : 0;
}
