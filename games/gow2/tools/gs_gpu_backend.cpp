// GSRasterBackend that rasterizes on the GPU (GpuGsExecutor) from a dedicated worker thread.
//
// The guest thread only enqueues commands (primitives, CLUT loads, transfers, uploads) in order; the worker owns
// the OpenGL context and executes them. Presentation (and any VRAM read) is a synchronous round trip: the worker
// drains the queue, copies the 4 MiB VRAM back into the frontend's local-memory storage and a CPU view converts
// it to the display picture exactly as the CPU backend does.
//
// Enabled at run time with PS2_GS_BACKEND=gpu (see ps2xRuntime/src/main.cpp). Unsupported on the GPU for now:
// line primitives and local->host transfers (counted by the executor and reported with PS2_GS_GPU_STATS=1).
#include "gl_context.h"
#include "gs_gpu_exec.h"
#include "runtime/gs/gs_backend.h"
#include "runtime/gs/gs_cpu_backend.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#ifndef GOW2_SHADER_DIR
#define GOW2_SHADER_DIR "games/gow2/tools/shaders"
#endif

// The live runner already owns a GLX context (raylib/GLFW) and EGL refuses to make a context current next to it,
// so the runner build creates the GPU context as a hidden GLFW window (created on the main thread, used by the
// worker). GLFW is linked statically inside raylib; only the entry points we need are declared here.
#ifdef GOW2_GLFW_CONTEXT
extern "C"
{
struct GLFWwindow;
void glfwWindowHint(int hint, int value);
GLFWwindow *glfwCreateWindow(int width, int height, const char *title, void *monitor, GLFWwindow *share);
void glfwMakeContextCurrent(GLFWwindow *window);
void glfwDestroyWindow(GLFWwindow *window);
}
#endif

namespace
{
struct ClutCmd { GSTex0Reg tex0; GSTexClutReg texclut; };
struct XferCmd { GSTransferCommand cmd; };
struct UploadCmd { std::vector<uint8_t> data; };
struct ControlCmd { std::function<void()> fn; };
using Command = std::variant<GSPrimitiveBatch, ClutCmd, XferCmd, UploadCmd, ControlCmd>;

class GpuGsBackend final : public GSRasterBackend
{
public:
    GpuGsBackend()
    {
        m_stats = std::getenv("PS2_GS_GPU_STATS") != nullptr;
        if (const char *d = std::getenv("PS2_GS_SHADER_DIR")) m_shaderDir = d;
        // PS2_GS_PRESENT_PROFILE=<json>[:A[:N]]: times the stages of N presentations starting at present A (default 0, 60)
        // and writes a small JSON. Off by default; when on, the VRAM read is split in glFinish + copy (same total work).
        if (const char *e = std::getenv("PS2_GS_PRESENT_PROFILE"); e && *e)
        {
            std::string v = e;
            const size_t c1 = v.find(':');
            m_profPath = v.substr(0, c1);
            if (c1 != std::string::npos)
            {
                m_profStart = std::strtoull(v.c_str() + c1 + 1, nullptr, 10);
                const size_t c2 = v.find(':', c1 + 1);
                if (c2 != std::string::npos) m_profCount = std::strtoull(v.c_str() + c2 + 1, nullptr, 10);
            }
            m_profOn = !m_profPath.empty() && m_profCount > 0;
        }
    }
    ~GpuGsBackend() override { stopWorker(); }

    void Initialize(uint8_t *vram, uint32_t size) override
    {
        stopWorker();
        m_vram = vram;
        m_vramSize = size;
        m_quit = false;
#ifdef GOW2_GLFW_CONTEXT
        glfwWindowHint(0x00020004, 0);        // GLFW_VISIBLE
        glfwWindowHint(0x00022001, 0x00030001); // GLFW_CLIENT_API = GLFW_OPENGL_API
        glfwWindowHint(0x00022002, 4);        // CONTEXT_VERSION_MAJOR
        glfwWindowHint(0x00022003, 5);        // CONTEXT_VERSION_MINOR
        glfwWindowHint(0x00022008, 0x00032001); // OPENGL_PROFILE = CORE
        m_glfwWindow = glfwCreateWindow(16, 16, "ps2-gs-gpu", nullptr, nullptr);
        if (!m_glfwWindow)
        {
            std::fprintf(stderr, "[gpu-gs] glfwCreateWindow for the GPU context failed; GS drawing disabled\n");
            m_failed = true;
            return;
        }
#endif
        std::promise<bool> ready;
        std::future<bool> up = ready.get_future();
        m_worker = std::thread([this, &ready] { workerMain(ready); });
        if (!up.get())
        {
            std::fprintf(stderr, "[gpu-gs] failed to create the OpenGL context; GS drawing disabled\n");
            m_failed = true;
        }
    }

    void Reset() override { runControl([this] { m_exec->reset(); }); }

    void Submit(const GSPrimitiveBatch &batch) override { push(Command(std::in_place_type<GSPrimitiveBatch>, batch)); }
    void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) override { push(Command(ClutCmd{tex0, texclut})); }
    void BeginTransfer(const GSTransferCommand &command) override { push(Command(XferCmd{command})); }
    void UploadImage(const uint8_t *data, uint32_t sizeBytes) override
    {
        if (!data || sizeBytes == 0u) return;
        push(Command(UploadCmd{std::vector<uint8_t>(data, data + sizeBytes)}));
    }

    // Commands are executed in order by one worker, so flushes and texture flushes need no action here.
    void Flush() override {}
    void TextureFlush() override {}
    void Sync(GSSyncReason reason) override
    {
        if (reason == GSSyncReason::Reset) runControl([] {});
    }

    PresentationFrame Present(const GSPresentationRequest &request) override
    {
        PresentationFrame out;
        if (m_profOn) return presentProfiled(request);
        runControl([&]
                   {
                       m_exec->readVram(m_vram);   // flushes pending draws first
                       out = m_view.Present(request);
                       reportStats();
                   });
        return out;
    }

    // Same effect as the CPU backend: fill the scissor rectangle of the frame buffer with a flat colour, honouring
    // FBMSK. Done as a flat sprite through the normal draw path (no Z test, no Z write, no blending, no texture).
    bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override
    {
        if (context.frame.fbw == 0u) return false;
        const uint8_t psm = context.frame.psm;
        if (psm != GS_PSM_CT32 && psm != GS_PSM_CT24 && psm != GS_PSM_CT16 && psm != GS_PSM_CT16S) return false;

        GSPrimitiveBatch b{};
        b.vertexCount = 2;
        b.state.context = context;
        b.state.context.xyoffset.ofx = 0;
        b.state.context.xyoffset.ofy = 0;
        b.state.context.test = 1ull << 17;   // ZTST = ALWAYS, alpha test and destination alpha test off
        b.state.context.zbuf.zmask = true;
        b.state.prim.type = GS_PRIM_SPRITE;
        const uint32_t x0 = context.scissor.x0, y0 = context.scissor.y0;
        const uint32_t x1 = std::max<uint32_t>(x0, context.scissor.x1), y1 = std::max<uint32_t>(y0, context.scissor.y1);
        uint8_t a = static_cast<uint8_t>(rgba >> 24u);
        if ((context.fba & 1ull) != 0ull && psm != GS_PSM_CT24) a |= 0x80u;
        for (int i = 0; i < 2; ++i)
        {
            b.vertices[i].x = static_cast<float>(i == 0 ? x0 : x1 + 1u);
            b.vertices[i].y = static_cast<float>(i == 0 ? y0 : y1 + 1u);
            b.vertices[i].r = static_cast<uint8_t>(rgba);
            b.vertices[i].g = static_cast<uint8_t>(rgba >> 8u);
            b.vertices[i].b = static_cast<uint8_t>(rgba >> 16u);
            b.vertices[i].a = a;
        }
        Submit(b);
        return true;
    }

    uint32_t ConsumeLocalToHostBytes(uint8_t *, uint32_t) override { return 0u; }   // local->host not supported on the GPU

    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override
    {
        uint32_t v = 0u;
        const_cast<GpuGsBackend *>(this)->runControl(
            [&]
            {
                m_exec->readVram(m_vram);
                v = m_view.ReadVram(psm, base, bw, x, y);
            });
        return v;
    }

    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override
    {
        runControl(
            [&]
            {
                m_exec->readVram(m_vram);
                m_view.WriteVram(psm, base, bw, x, y, value);
                m_exec->uploadVram(m_vram);
            });
    }

    void SnapshotVram(std::vector<uint8_t> &out) const override
    {
        const_cast<GpuGsBackend *>(this)->runControl(
            [&]
            {
                m_exec->readVram(m_vram);
                out.assign(m_vram, m_vram + m_vramSize);
            });
    }

    GSTransferSnapshot GetTransferSnapshot() const override { return {}; }

private:
    static constexpr size_t kMaxQueued = 400000;   // back-pressure so a slow GPU cannot grow the queue without bound

    void push(Command &&c)
    {
        if (m_failed) return;
        std::unique_lock<std::mutex> lk(m_mx);
        m_space.wait(lk, [&] { return m_in.size() < kMaxQueued || m_quit; });
        m_in.push_back(std::move(c));
        m_work.notify_one();
    }

    void runControl(std::function<void()> fn)
    {
        if (m_failed || !m_worker.joinable()) return;
        std::promise<void> done;
        std::future<void> f = done.get_future();
        push(Command(ControlCmd{[&fn, &done] { fn(); done.set_value(); }}));
        f.wait();
    }

    void stopWorker()
    {
        if (!m_worker.joinable()) return;
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_quit = true;
        }
        m_work.notify_all();
        m_space.notify_all();
        m_worker.join();
        m_in.clear();
    }

    void workerMain(std::promise<bool> &ready)
    {
#ifdef GOW2_GLFW_CONTEXT
        glfwMakeContextCurrent(m_glfwWindow);
        const bool haveContext = true;
#else
        const bool haveContext = gl::initContext();
#endif
        if (!haveContext)
        {
            ready.set_value(false);
            return;
        }
        m_exec = std::make_unique<GpuGsExecutor>(m_shaderDir.empty() ? std::string(GOW2_SHADER_DIR) : m_shaderDir);
        m_exec->initialize(m_vram);
        m_view.Initialize(m_vram, m_vramSize);
        ready.set_value(true);

        std::vector<Command> local;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lk(m_mx);
                m_work.wait(lk, [&] { return !m_in.empty() || m_quit; });
                if (m_in.empty() && m_quit) break;
                local.swap(m_in);
            }
            m_space.notify_all();
            for (Command &c : local)
            {
                if (auto *p = std::get_if<GSPrimitiveBatch>(&c)) m_exec->submit(*p);
                else if (auto *cl = std::get_if<ClutCmd>(&c)) m_exec->loadClut(cl->tex0, cl->texclut);
                else if (auto *x = std::get_if<XferCmd>(&c)) m_exec->beginTransfer(x->cmd);
                else if (auto *u = std::get_if<UploadCmd>(&c)) m_exec->uploadImage(u->data.data(), static_cast<uint32_t>(u->data.size()));
                else if (auto *ct = std::get_if<ControlCmd>(&c)) ct->fn();
            }
            local.clear();
        }
        m_exec.reset();
    }

    // Same work as Present(), with a clock between the stages: wait for the worker (earlier queued commands + wake-up),
    // draw submit + GPU completion (flushDraws + glFinish), the 4 MiB VRAM copy, the CPU image assembly, wake-up back.
    PresentationFrame presentProfiled(const GSPresentationRequest &request)
    {
        using clk = std::chrono::steady_clock;
        PresentationFrame out;
        const uint64_t idx = m_profIdx++;
        size_t backlog = 0;
        {
            std::lock_guard<std::mutex> lk(m_mx);
            backlog = m_in.size();
        }
        clk::time_point t1, t2, t3, t4;
        const clk::time_point t0 = clk::now();
        runControl([&]
                   {
                       t1 = clk::now();
                       m_exec->finish();
                       t2 = clk::now();
                       m_exec->readVram(m_vram);   // draws already flushed: barrier + glGetBufferSubData only
                       t3 = clk::now();
                       out = m_view.Present(request);
                       t4 = clk::now();
                       reportStats();
                   });
        const clk::time_point t5 = clk::now();
        auto ms = [](clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        if (idx >= m_profStart && m_profRows.size() < m_profCount && m_profHavePrev)
        {
            m_profRows.push_back({ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4), ms(t4, t5), ms(t0, t5),
                                  ms(m_profPrev, t0), static_cast<double>(backlog)});
            if (m_profRows.size() == m_profCount) writeProfile();
        }
        m_profPrev = t0;
        m_profHavePrev = true;
        return out;
    }

    void writeProfile()
    {
        static const char *names[8] = {"wait_worker", "gpu_submit_finish", "vram_copy_4mib", "cpu_assemble", "wake_back",
                                       "present_total", "period_since_prev_present", "queue_backlog_cmds"};
        FILE *f = std::fopen(m_profPath.c_str(), "w");
        if (!f) { std::fprintf(stderr, "[gpu-gs] cannot write %s\n", m_profPath.c_str()); return; }
        std::fprintf(f, "{\n  \"presents_recorded\": %zu,\n  \"first_present_index\": %llu,\n", m_profRows.size(),
                     (unsigned long long)m_profStart);
        for (int c = 0; c < 8; ++c)
        {
            std::vector<double> v;
            for (const auto &r : m_profRows) v.push_back(r[c]);
            std::sort(v.begin(), v.end());
            double sum = 0;
            for (double x : v) sum += x;
            std::fprintf(f, "  \"%s%s\": {\"mean\": %.3f, \"median\": %.3f, \"p95\": %.3f, \"max\": %.3f}%s\n", names[c],
                         c == 7 ? "" : "_ms", sum / v.size(), v[v.size() / 2], v[(v.size() * 95) / 100 >= v.size() ? v.size() - 1 : (v.size() * 95) / 100],
                         v.back(), c == 7 ? "" : ",");
        }
        std::fprintf(f, "}\n");
        std::fclose(f);
        std::fprintf(stderr, "[gpu-gs] present profile written to %s (%zu presents)\n", m_profPath.c_str(), m_profRows.size());
    }

    void reportStats()
    {
        if (!m_stats || (++m_presents % 60u) != 0u) return;
        const GpuGsExecutor::Stats &s = m_exec->stats();
        std::fprintf(stderr,
                     "[gpu-gs] presents=%llu prims=%llu draws=%llu splits(sig=%llu hazard=%llu) uploads=%llu clut=%llu | "
                     "unsupported: lines=%llu local->host=%llu\n",
                     (unsigned long long)m_presents, (unsigned long long)s.prims, (unsigned long long)s.draws,
                     (unsigned long long)s.splitsSignature, (unsigned long long)s.splitsHazard, (unsigned long long)s.uploads,
                     (unsigned long long)s.clutLoads, (unsigned long long)s.unsupportedLines,
                     (unsigned long long)s.unsupportedTransfers);
    }

#ifdef GOW2_GLFW_CONTEXT
    GLFWwindow *m_glfwWindow = nullptr;
#endif
    uint8_t *m_vram = nullptr;
    uint32_t m_vramSize = 0;
    std::string m_shaderDir;
    bool m_stats = false;
    bool m_failed = false;
    uint64_t m_presents = 0;
    bool m_profOn = false;
    bool m_profHavePrev = false;
    std::string m_profPath;
    uint64_t m_profStart = 0, m_profCount = 60, m_profIdx = 0;
    std::chrono::steady_clock::time_point m_profPrev;
    std::vector<std::array<double, 8>> m_profRows;

    std::thread m_worker;
    std::mutex m_mx;
    std::condition_variable m_work, m_space;
    std::vector<Command> m_in;
    bool m_quit = false;

    // Worker-thread only (after Initialize):
    std::unique_ptr<GpuGsExecutor> m_exec;
    GSCpuBackend m_view;
};
} // namespace

// Hook looked up by ps2xRuntime/src/main.cpp (weak symbol): returns a new backend owned by the caller.
extern "C" GSRasterBackend *ps2_create_gpu_gs_backend()
{
    return new GpuGsBackend();
}
