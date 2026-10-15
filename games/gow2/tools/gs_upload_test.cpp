// gow2_gs_upload_test: GPU image upload (shaders/gs_upload.glsl) vs GSCpuBackend::UploadImage.
// Random host->local transfers for every destination storage mode; the whole 4 MiB VRAM is compared.
#include "gl_context.h"
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

static uint32_t bitsOf(uint32_t psm)
{
    switch (psm)
    {
    case 0x00: case 0x30: return 32;
    case 0x01: case 0x31: return 24;
    case 0x02: case 0x0A: case 0x32: case 0x3A: return 16;
    case 0x13: case 0x1B: return 8;
    default: return 4;   // 0x14, 0x24, 0x2C
    }
}

int main()
{
    if (!gl::initContext()) { fprintf(stderr, "no GL context\n"); return 1; }
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    GSMem::InitLookupTables();

    const std::string dir = GOW2_SHADER_DIR;
    std::string upload = slurp(dir + "/gs_upload.glsl");
    const std::string marker = "#include_gs_vram_here";
    upload.replace(upload.find(marker), marker.size(), slurp(dir + "/gs_vram_tables.glsl") + slurp(dir + "/gs_vram.glsl"));
    GLuint prog = gl::computeProgram(("#version 450\n" + upload).c_str());

    const uint32_t psms[] = {0x00, 0x01, 0x02, 0x0A, 0x13, 0x14, 0x1B, 0x24, 0x2C, 0x30, 0x31, 0x32, 0x3A};
    std::mt19937 rng(777);
    const size_t kVram = GSMem::MEMORY_SIZE;
    size_t totalBad = 0;

    GLuint vramBuf, srcBuf;
    glGenBuffers(1, &vramBuf); glGenBuffers(1, &srcBuf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, vramBuf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, kVram, nullptr, GL_DYNAMIC_COPY);
    glUseProgram(prog);
    const GLint uDbp = glGetUniformLocation(prog, "uDbp"), uDbw = glGetUniformLocation(prog, "uDbw"),
                uPsm = glGetUniformLocation(prog, "uPsm"), uDsax = glGetUniformLocation(prog, "uDsax"),
                uDsay = glGetUniformLocation(prog, "uDsay"), uRrw = glGetUniformLocation(prog, "uRrw"),
                uStart = glGetUniformLocation(prog, "uStartPixel"), uCount = glGetUniformLocation(prog, "uCount");

    for (uint32_t psm : psms)
    {
        std::vector<uint8_t> initial(kVram);
        for (size_t i = 0; i < kVram; i += 4) { uint32_t v = rng(); memcpy(&initial[i], &v, 4); }

        std::vector<uint8_t> cpuVram = initial;
        GSCpuBackend cpu;
        cpu.Initialize(cpuVram.data(), static_cast<uint32_t>(cpuVram.size()));

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, vramBuf);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, kVram, initial.data());

        const int kTransfers = 60;
        for (int t = 0; t < kTransfers; ++t)
        {
            const uint32_t dbw = 1 + rng() % 16u;
            const uint32_t dbp = rng() % 16384u;
            const uint32_t dsax = rng() % (dbw * 64u);
            const uint32_t rrw = 1 + rng() % std::min<uint32_t>(200u, dbw * 64u - dsax);
            const uint32_t dsay = rng() % 256u;
            const uint32_t rrh = 1 + rng() % 64u;
            const uint32_t total = rrw * rrh;
            const uint32_t bytes = (total * bitsOf(psm) + 7) / 8;
            std::vector<uint8_t> data((bytes + 15) & ~15u);          // GIF data is qword-padded
            for (auto &b : data) b = static_cast<uint8_t>(rng());

            GSTransferCommand cmd{};
            cmd.bitbltbuf.dbp = dbp; cmd.bitbltbuf.dbw = static_cast<uint8_t>(dbw); cmd.bitbltbuf.dpsm = static_cast<uint8_t>(psm);
            cmd.trxpos.dsax = static_cast<uint16_t>(dsax); cmd.trxpos.dsay = static_cast<uint16_t>(dsay);
            cmd.trxreg.rrw = static_cast<uint16_t>(rrw); cmd.trxreg.rrh = static_cast<uint16_t>(rrh);
            cmd.direction = 0;
            cpu.BeginTransfer(cmd);
            cpu.UploadImage(data.data(), static_cast<uint32_t>(data.size()));

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, srcBuf);
            glBufferData(GL_SHADER_STORAGE_BUFFER, data.size(), data.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vramBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, srcBuf);
            glUniform1ui(uDbp, dbp); glUniform1ui(uDbw, dbw); glUniform1ui(uPsm, psm);
            glUniform1ui(uDsax, dsax); glUniform1ui(uDsay, dsay); glUniform1ui(uRrw, rrw);
            glUniform1ui(uStart, 0); glUniform1ui(uCount, total);
            glDispatchCompute((total + 63) / 64, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        std::vector<uint8_t> gpuVram(kVram);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, vramBuf);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, kVram, gpuVram.data());

        size_t bad = 0, changed = 0;
        for (size_t i = 0; i < kVram; i += 4)
        {
            uint32_t a, b, c;
            memcpy(&a, &cpuVram[i], 4); memcpy(&b, &gpuVram[i], 4); memcpy(&c, &initial[i], 4);
            changed += (a != c);
            if (a != b)
            {
                if (bad < 3) printf("  MISMATCH psm=%02x word=%zu cpu=%08x gpu=%08x (initial %08x)\n", psm, i / 4, a, b, c);
                ++bad;
            }
        }
        printf("psm 0x%02x: %d transfers, %zu words changed by CPU, %zu mismatching words\n", psm, kTransfers, changed, bad);
        totalBad += bad;
    }
    printf(totalBad ? "FAIL: %zu mismatching words\n" : "PASS: GPU upload matches GSCpuBackend::UploadImage\n", totalBad);
    return totalBad ? 1 : 0;
}
