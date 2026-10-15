// gow2_gpu_bench: measures what a GPU-side GS would cost on this machine.
//   Part A: host<->GPU transfer costs (bandwidth, many small uploads, readback latency).
//   Part B: draws the real triangles of a dump frame (exported by gsdump.py tris) with the same
//           state changes, textured/blended/depth-tested, into a 512x448 RGBA8 target.
// Headless: EGL device platform + pbuffer, OpenGL 4.5 core.
#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

static bool initContext()
{
    auto queryDevices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (queryDevices && getPlatformDisplay)
    {
        EGLDeviceEXT devs[8]; EGLint n = 0;
        queryDevices(8, devs, &n);
        for (int i = 0; i < n && dpy == EGL_NO_DISPLAY; ++i)
        {
            EGLDisplay d = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devs[i], nullptr);
            EGLint maj, min;
            if (d != EGL_NO_DISPLAY && eglInitialize(d, &maj, &min)) dpy = d;
        }
    }
    if (dpy == EGL_NO_DISPLAY)
    {
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint maj, min;
        if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) return false;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) return false;
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg; EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &ncfg) || ncfg == 0) return false;
    const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
                              EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (ctx == EGL_NO_CONTEXT) return false;
    return eglMakeCurrent(dpy, surf, surf, ctx) == EGL_TRUE;
}

// ---------------------------------------------------------------------------------------------
static void partA()
{
    printf("\n== Part A: host <-> GPU transfers ==\n");
    GLuint buf; glGenBuffers(1, &buf);
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, 128 << 20, nullptr, GL_STREAM_DRAW);
    std::vector<uint8_t> src(128 << 20, 0x5a);

    printf("buffer upload (pageable memory, glBufferSubData + glFinish):\n");
    for (size_t kb : {64u, 1024u, 8192u, 65536u})
    {
        std::vector<double> t;
        for (int i = 0; i < 12; ++i)
        {
            auto t0 = Clock::now();
            glBufferSubData(GL_ARRAY_BUFFER, 0, kb * 1024, src.data());
            glFinish();
            t.push_back(msSince(t0));
        }
        const double ms = median(t);
        printf("  %6zu KB: %8.3f ms  %7.2f GB/s\n", kb, ms, (kb * 1024.0 / 1e9) / (ms / 1000.0));
    }

    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024, 1024, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    printf("texture upload (glTexSubImage2D RGBA8):\n");
    for (int dim : {64, 256, 512, 1024})
    {
        std::vector<double> t;
        for (int i = 0; i < 12; ++i)
        {
            auto t0 = Clock::now();
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, dim, dim, GL_RGBA, GL_UNSIGNED_BYTE, src.data());
            glFinish();
            t.push_back(msSince(t0));
        }
        const double ms = median(t);
        const double bytes = double(dim) * dim * 4;
        printf("  %4dx%-4d (%6.0f KB): %8.3f ms  %7.2f GB/s\n", dim, dim, bytes / 1024, ms, bytes / 1e9 / (ms / 1000.0));
    }

    printf("many small uploads (1000 x 64x64 RGBA = 16 MB total, one glFinish at the end):\n");
    {
        std::vector<double> t;
        for (int i = 0; i < 6; ++i)
        {
            auto t0 = Clock::now();
            for (int k = 0; k < 1000; ++k)
                glTexSubImage2D(GL_TEXTURE_2D, 0, (k % 16) * 64, ((k / 16) % 16) * 64, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, src.data());
            glFinish();
            t.push_back(msSince(t0));
        }
        const double ms = median(t);
        printf("  %8.3f ms total, %6.2f us per call, %6.2f GB/s\n", ms, ms * 1000.0 / 1000.0, 16.0 * 1.048576e6 / 1e9 / (ms / 1000.0));
    }

    printf("readback of a 512x448 RGBA frame (0.9 MB):\n");
    {
        GLuint fbo, rb; glGenFramebuffers(1, &fbo); glGenRenderbuffers(1, &rb);
        glBindRenderbuffer(GL_RENDERBUFFER, rb); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 512, 448);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
        glClearColor(0.2f, 0.4f, 0.6f, 1.f); glClear(GL_COLOR_BUFFER_BIT); glFinish();
        std::vector<uint8_t> dst(512 * 448 * 4);
        std::vector<double> t;
        for (int i = 0; i < 30; ++i)
        {
            glClear(GL_COLOR_BUFFER_BIT);           // makes the readback depend on fresh GPU work
            auto t0 = Clock::now();
            glReadPixels(0, 0, 512, 448, GL_RGBA, GL_UNSIGNED_BYTE, dst.data());
            t.push_back(msSince(t0));
        }
        printf("  synchronous glReadPixels (stall):      %8.3f ms median\n", median(t));
        GLuint pbo; glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo); glBufferData(GL_PIXEL_PACK_BUFFER, dst.size(), nullptr, GL_STREAM_READ);
        t.clear();
        for (int i = 0; i < 30; ++i)
        {
            glClear(GL_COLOR_BUFFER_BIT);
            auto t0 = Clock::now();
            glReadPixels(0, 0, 512, 448, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);   // async into PBO
            t.push_back(msSince(t0));                                            // cost of *issuing* it
        }
        printf("  async PBO glReadPixels, issue cost:    %8.3f ms median\n", median(t));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
}

// ---------------------------------------------------------------------------------------------
#pragma pack(push, 1)
struct SetupRec { uint16_t tw, th; uint8_t tme, abe, a, b, c, d, fix, tfx, zte, ztst; uint16_t pad; };
#pragma pack(pop)
struct Vtx { float x, y, z, r, g, b, a, u, v; };

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof log, nullptr, log); fprintf(stderr, "shader: %s\n", log); exit(1); }
    return s;
}

static void partB(const char *trisPath, int frames)
{
    printf("\n== Part B: real draws of one dump frame ==\n");
    FILE *f = fopen(trisPath, "rb");
    if (!f) { printf("cannot open %s\n", trisPath); return; }
    char magic[4]; uint32_t nSetups, nTris;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "GWTR", 4) || fread(&nSetups, 4, 1, f) != 1 || fread(&nTris, 4, 1, f) != 1) return;
    std::vector<SetupRec> setups(nSetups);
    if (fread(setups.data(), sizeof(SetupRec), nSetups, f) != nSetups) return;
    std::vector<uint32_t> triSetup(nTris);
    std::vector<Vtx> verts(size_t(nTris) * 3);
    for (uint32_t i = 0; i < nTris; ++i)
    {
        if (fread(&triSetup[i], 4, 1, f) != 1 || fread(&verts[size_t(i) * 3], sizeof(Vtx), 3, f) != 3) return;
    }
    fclose(f);

    // batches = runs of consecutive triangles that share a setup (what a real renderer can merge)
    struct Batch { uint32_t setup, first, count; };
    std::vector<Batch> batches;
    for (uint32_t i = 0; i < nTris; ++i)
    {
        if (batches.empty() || batches.back().setup != triSetup[i]) batches.push_back({triSetup[i], i * 3, 0});
        batches.back().count += 3;
    }
    size_t texBytes = 0;
    for (const auto &s : setups) texBytes += size_t(s.tw) * s.th * 4;
    printf("triangles=%u setups=%u batches(draw calls)=%zu vertex bytes=%.2f MB decoded texture bytes (all setups)=%.1f MB\n",
           nTris, nSetups, batches.size(), verts.size() * sizeof(Vtx) / 1e6, texBytes / 1e6);

    const char *vs = R"(#version 450
layout(location=0) in vec3 pos; layout(location=1) in vec4 col; layout(location=2) in vec2 uv;
uniform vec2 target; uniform vec2 texSize;
out vec4 vCol; out vec2 vUv;
void main(){
  vec2 p = pos.xy / target * 2.0 - 1.0;
  gl_Position = vec4(p.x, p.y, 1.0 - pos.z / 16777216.0 * 2.0, 1.0);
  vCol = col / 128.0; vUv = uv / texSize;
})";
    const char *fs = R"(#version 450
in vec4 vCol; in vec2 vUv; uniform sampler2D tex; uniform int tme, tfx, useFix; uniform float fixA;
out vec4 o;
void main(){
  vec4 c = vCol;
  if (tme != 0) {
    vec4 t = texture(tex, vUv);
    if (tfx == 0) c = vec4(t.rgb * vCol.rgb, t.a * vCol.a);
    else if (tfx == 1) c = t;
    else c = vec4(t.rgb * vCol.rgb + vCol.a, t.a);
  }
  if (useFix != 0) c.a = fixA;
  o = c;
})";
    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vs)); glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog); glUseProgram(prog);
    glUniform2f(glGetUniformLocation(prog, "target"), 512.f, 448.f);
    const GLint uTex = glGetUniformLocation(prog, "texSize"), uTme = glGetUniformLocation(prog, "tme"),
                uTfx = glGetUniformLocation(prog, "tfx"), uUseFix = glGetUniformLocation(prog, "useFix"),
                uFixA = glGetUniformLocation(prog, "fixA");

    // render target 512x448 RGBA8 + D24
    GLuint fbo, color, depth;
    glGenFramebuffers(1, &fbo); glGenRenderbuffers(1, &color); glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, color); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 512, 448);
    glBindRenderbuffer(GL_RENDERBUFFER, depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 448);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    glViewport(0, 0, 512, 448);

    // one texture per setup with a texture (random content: timing, not correctness)
    std::vector<GLuint> texs(nSetups, 0);
    std::vector<uint8_t> noise(1024 * 1024 * 4);
    for (size_t i = 0; i < noise.size(); ++i) noise[i] = uint8_t((i * 2654435761u) >> 13);
    for (uint32_t i = 0; i < nSetups; ++i)
    {
        if (!setups[i].tme || !setups[i].tw) continue;
        glGenTextures(1, &texs[i]); glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, setups[i].tw, setups[i].th, 0, GL_RGBA, GL_UNSIGNED_BYTE, noise.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    GLuint vao, vbo; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vtx), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)28);
    GLuint query; glGenQueries(1, &query);

    auto drawFrame = [&](bool countSamples) -> uint64_t {
        glClearColor(0, 0, 0, 1); glClearDepth(0.0); glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_CULL_FACE);
        if (countSamples) glBeginQuery(GL_SAMPLES_PASSED, query);
        for (const auto &b : batches)
        {
            const SetupRec &s = setups[b.setup];
            if (s.zte) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GEQUAL); } else glDisable(GL_DEPTH_TEST);
            if (s.abe)
            {
                glEnable(GL_BLEND);
                if (s.a == 0 && s.b == 2 && s.d == 1) glBlendFunc(GL_SRC_ALPHA, GL_ONE);            // additive
                else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);                              // (Cs-Cd)*A+Cd
            }
            else glDisable(GL_BLEND);
            glUniform1i(uTme, s.tme); glUniform1i(uTfx, s.tfx);
            glUniform1i(uUseFix, (s.abe && s.c == 2) ? 1 : 0); glUniform1f(uFixA, s.fix / 128.0f);
            if (s.tme && texs[b.setup])
            {
                glBindTexture(GL_TEXTURE_2D, texs[b.setup]);
                glUniform2f(uTex, float(s.tw), float(s.th));
            }
            glDrawArrays(GL_TRIANGLES, b.first, b.count);
        }
        uint64_t samples = 0;
        if (countSamples)
        {
            glEndQuery(GL_SAMPLES_PASSED);
            GLuint64 n = 0; glGetQueryObjectui64v(query, GL_QUERY_RESULT, &n); samples = n;
        }
        return samples;
    };

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("framebuffer incomplete\n"); return; }
    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(Vtx), verts.data());
    drawFrame(false); glFinish();                    // warm-up (shader compile, caches)
    const uint64_t samples = drawFrame(true); glFinish();
    if (glGetError() != GL_NO_ERROR) printf("GL error after warm-up draw\n");
    {   // sanity: read back a few pixels so a "fast" number cannot come from drawing nothing
        std::vector<uint8_t> px(512 * 448 * 4);
        glReadPixels(0, 0, 512, 448, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        uint64_t nonBlack = 0;
        for (size_t i = 0; i < px.size(); i += 4) nonBlack += (px[i] | px[i + 1] | px[i + 2]) != 0;
        printf("sanity: %.1f%% of the 512x448 target is non-black after one frame\n", nonBlack * 100.0 / (512.0 * 448.0));
    }

    std::vector<double> tFrame, tUpload, tDraw;
    for (int i = 0; i < frames; ++i)
    {
        auto t0 = Clock::now();
        glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(Vtx), verts.data());   // vertices cross PCIe each frame
        glFinish();
        const double up = msSince(t0);
        auto t1 = Clock::now();
        drawFrame(false);
        glFinish();
        const double dr = msSince(t1);
        tUpload.push_back(up); tDraw.push_back(dr); tFrame.push_back(up + dr);
    }
    printf("fragments shaded per frame (passed depth): %.2f M  (%.1f x the 512x448 screen)\n",
           samples / 1e6, double(samples) / (512.0 * 448.0));
    printf("per frame, median of %d: vertex upload %.3f ms + draw %.3f ms = %.3f ms\n",
           frames, median(tUpload), median(tDraw), median(tFrame));
    printf("                         min %.3f ms  max %.3f ms\n",
           *std::min_element(tFrame.begin(), tFrame.end()), *std::max_element(tFrame.begin(), tFrame.end()));

    // worst case for textures: re-decode+upload every texture every frame
    std::vector<double> tTex;
    for (int i = 0; i < 6; ++i)
    {
        auto t0 = Clock::now();
        for (uint32_t s = 0; s < nSetups; ++s)
        {
            if (!texs[s]) continue;
            glBindTexture(GL_TEXTURE_2D, texs[s]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, setups[s].tw, setups[s].th, GL_RGBA, GL_UNSIGNED_BYTE, noise.data());
        }
        glFinish();
        tTex.push_back(msSince(t0));
    }
    printf("worst case, re-upload all %.1f MB of decoded textures each frame: %.3f ms (%.1f%% of a 60 Hz frame)\n",
           texBytes / 1e6, median(tTex), median(tTex) * 100.0 / (1000.0 / 60.0));
}

int main(int argc, char **argv)
{
    if (!initContext()) { fprintf(stderr, "failed to create an OpenGL 4.5 context via EGL\n"); return 1; }
    printf("GL_RENDERER: %s\nGL_VERSION : %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    partA();
    if (argc > 1) partB(argv[1], argc > 2 ? atoi(argv[2]) : 30);
    return 0;
}
