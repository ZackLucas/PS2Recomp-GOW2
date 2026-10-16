#ifndef PS2_VU1_NATIVE_BRIDGE_H
#define PS2_VU1_NATIVE_BRIDGE_H

#include "runtime/ps2_vu1.h"
#include "runtime/ps2_vu1_native.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

class PS2Memory;
class GS;

// Runs a VU1 microprogram call with natively translated code when every segment it needs is known, and tells the
// caller to use the interpreter otherwise. The translated code lives in a plugin (shared library) so the game-derived
// C++ never enters the repository: PS2_VU1_NATIVE=<path to libgow2_vu1_native_plugin.so>.
class Vu1NativeBridge
{
public:
    bool enabled() const { return m_find != nullptr; }
    void loadFromEnvironment();

    // True when the call ran natively and `vu` holds the resulting state; false means "nothing changed, interpret".
    // With PS2_VU1_NATIVE_VERIFY=1 it never takes over: it runs the call natively AND on a private interpreter,
    // compares memory/registers/packets, counts the result and returns false so the real interpreter runs it.
    bool tryRun(VU1Interpreter &vu, PS2Memory &memory, GS &gs, const uint8_t *code, uint64_t codeGeneration,
                uint8_t *data, uint32_t dataSize, uint32_t startPC, uint32_t top, uint32_t itop);

    struct Stats
    {
        uint64_t native = 0, fallbackUnknown = 0, fallbackFlags = 0, fallbackBudget = 0;
        uint64_t verified = 0, identical = 0, badMem = 0, badRegs = 0, badPackets = 0, notComparable = 0;
    };
    const Stats &stats() const { return m_stats; }

    // Per-entry-PC tallies of the calls made since the last report (PS2_PERF_STATS): prints calls, GIF packets and
    // VU cycles per guest VSync, then starts a new window.
    void reportWindow(std::FILE *out, uint64_t vsyncs);

private:
    using FindFn = vu1n::SegFn (*)(uint32_t seg, uint64_t hash);
    FindFn m_find = nullptr;
    vu1n::SegTable m_table{};
    uint64_t m_tableGeneration = ~0ull;
    std::vector<uint8_t> m_snapshot;
    std::vector<std::vector<uint8_t>> m_packets;
    Stats m_stats;
    struct EntryTally
    {
        uint64_t calls = 0, packets = 0, bytes = 0, cycles = 0;
    };
    std::map<uint32_t, EntryTally> m_tally;
    uint64_t m_nsPrep = 0, m_nsRun = 0, m_nsPost = 0, m_nsSubmit = 0; // wall time inside tryRun, per phase, since the last report
    bool m_verify = false;
    VU1Interpreter m_ref{VU1Interpreter::Unit::VU1};
    std::vector<uint8_t> m_refData;
    void verify(VU1Interpreter &vu, GS &gs, const uint8_t *code, uint8_t *data, uint32_t dataSize, uint32_t startPC,
                uint32_t top, uint32_t itop);
};

#endif
