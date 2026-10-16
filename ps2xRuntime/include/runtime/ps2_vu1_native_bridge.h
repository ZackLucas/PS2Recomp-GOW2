#ifndef PS2_VU1_NATIVE_BRIDGE_H
#define PS2_VU1_NATIVE_BRIDGE_H

#include "runtime/ps2_vu1.h"
#include "runtime/ps2_vu1_native.h"

#include <cstdint>
#include <vector>

class PS2Memory;

// Runs a VU1 microprogram call with natively translated code when every segment it needs is known, and tells the
// caller to use the interpreter otherwise. The translated code lives in a plugin (shared library) so the game-derived
// C++ never enters the repository: PS2_VU1_NATIVE=<path to libgow2_vu1_native_plugin.so>.
class Vu1NativeBridge
{
public:
    bool enabled() const { return m_find != nullptr; }
    void loadFromEnvironment();

    // True when the call ran natively and `vu` holds the resulting state; false means "nothing changed, interpret".
    bool tryRun(VU1Interpreter &vu, PS2Memory &memory, const uint8_t *code, uint64_t codeGeneration,
                uint8_t *data, uint32_t dataSize, uint32_t startPC, uint32_t top, uint32_t itop);

    struct Stats
    {
        uint64_t native = 0, fallbackUnknown = 0, fallbackFlags = 0, fallbackBudget = 0;
    };
    const Stats &stats() const { return m_stats; }

private:
    using FindFn = vu1n::SegFn (*)(uint32_t seg, uint64_t hash);
    FindFn m_find = nullptr;
    vu1n::SegTable m_table{};
    uint64_t m_tableGeneration = ~0ull;
    std::vector<uint8_t> m_snapshot;
    std::vector<std::vector<uint8_t>> m_packets;
    Stats m_stats;
};

#endif
