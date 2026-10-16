#include "runtime/ps2_vu1_native_bridge.h"
#include "runtime/ps2_memory.h"

#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

void Vu1NativeBridge::loadFromEnvironment()
{
#if defined(__unix__) || defined(__APPLE__)
    const char *path = std::getenv("PS2_VU1_NATIVE");
    if (!path || !*path)
        return;
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib)
    {
        std::fprintf(stderr, "[VU1 native] cannot load %s: %s\n", path, dlerror());
        return;
    }
    m_find = reinterpret_cast<FindFn>(dlsym(lib, "vu1_native_find_segment"));
    std::fprintf(stderr, m_find ? "[VU1 native] plugin loaded: %s\n" : "[VU1 native] %s has no vu1_native_find_segment\n", path);
#endif
}

namespace
{
    struct PacketSink
    {
        std::vector<std::vector<uint8_t>> *list;
    };
    void collect(void *ctx, const uint8_t *p, uint32_t n)
    {
        static_cast<PacketSink *>(ctx)->list->emplace_back(p, p + n);
    }
} // namespace

bool Vu1NativeBridge::tryRun(VU1Interpreter &vu, PS2Memory &memory, const uint8_t *code, uint64_t codeGeneration,
                             uint8_t *data, uint32_t dataSize, uint32_t startPC, uint32_t top, uint32_t itop)
{
    if (!m_find || dataSize != PS2_VU1_DATA_SIZE)
        return false;
    VU1State &st = vu.state();
    // The translation ignores D/T-bit stops: let the interpreter handle programs that can be stopped by them.
    if (st.dBitEnabled || st.tBitEnabled)
    {
        ++m_stats.fallbackFlags;
        return false;
    }
    if (m_tableGeneration != codeGeneration)
    {
        for (uint32_t seg = 0; seg < vu1n::kCodePairs / vu1n::kSegPairs; ++seg)
            m_table.fn[seg] = m_find(seg, vu1n::hashWindow(code, seg));
        m_tableGeneration = codeGeneration;
    }
    const uint32_t entry = (startPC & (vu1n::kCodePairs * 8 - 1)) / 8;
    if (!m_table.fn[entry / vu1n::kSegPairs])
    {
        ++m_stats.fallbackUnknown;
        return false;
    }

    m_snapshot.assign(data, data + dataSize);
    m_packets.clear();
    PacketSink sink{&m_packets};

    vu1n::State a{};
    std::memcpy(a.vf, st.vf, sizeof(a.vf));
    std::memcpy(a.acc, st.acc, sizeof(a.acc));
    a.q = st.q; a.p = st.p; a.i = st.i; a.r = st.r;
    a.mac = st.mac; a.status = st.status; a.clip = st.clip; a.workingClip = st.clip;
    for (int r = 0; r < 16; ++r)
        a.vi[r] = st.vi[r];
    a.top = top; a.itop = itop;
    a.mem = data; a.memSize = dataSize;
    a.budget = 65536;
    a.sink = collect; a.sinkCtx = &sink;

    const int prev = std::fegetround();
    std::fesetround(FE_TOWARDZERO);
    const int32_t rc = vu1n::runProgram(a, m_table, entry);
    std::fesetround(prev);

    if (rc != -1)
    {
        // Reached code with no translation (or ran away): undo and let the interpreter redo the whole call.
        std::memcpy(data, m_snapshot.data(), dataSize);
        ++(rc >= 0 ? m_stats.fallbackUnknown : m_stats.fallbackBudget);
        return false;
    }

    std::memcpy(st.vf, a.vf, sizeof(st.vf));
    std::memcpy(st.acc, a.acc, sizeof(st.acc));
    st.q = a.q; st.p = a.p; st.i = a.i; st.r = a.r;
    st.mac = a.mac; st.status = a.status; st.clip = a.clip;
    for (int r = 0; r < 16; ++r)
        st.vi[r] = a.vi[r];
    st.top = top; st.itop = itop;
    st.ebit = false; st.haltAfterDelaySlot = false; st.stoppedByD = false; st.stoppedByT = false;
    st.branchPending = false;
    for (const auto &pkt : m_packets)
        memory.submitGifPacket(GifPathId::Path1, pkt.data(), static_cast<uint32_t>(pkt.size()));
    if ((++m_stats.native & 0x3FFF) == 0 && std::getenv("PS2_VU1_NATIVE_STATS"))
        std::fprintf(stderr, "[VU1 native] native=%llu unknown=%llu dbit=%llu budget=%llu\n", (unsigned long long)m_stats.native,
                     (unsigned long long)m_stats.fallbackUnknown, (unsigned long long)m_stats.fallbackFlags,
                     (unsigned long long)m_stats.fallbackBudget);
    return true;
}
