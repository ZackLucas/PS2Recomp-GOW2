#pragma once

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <utility>

namespace ps2x::iop::detail
{
    // Bounded, opt-in transport diagnostics. No guest buffers are dumped in full.
    template <typename Writer>
    void traceIop(Writer &&writer)
    {
        static const bool enabled = [] {
            const char *value = std::getenv("PS2_TRACE_IOP_RPC");
            return value && *value && *value != '0';
        }();
        if (!enabled)
            return;
        // Shared across Writer instantiations via the inline accessor below.
        extern std::atomic<unsigned> &iopTraceSequence();
        const unsigned sequence = ++iopTraceSequence();
        if (sequence > 4096u)
            return;
        std::ostringstream out;
        out << "[IOP trace] seq=" << std::dec << sequence << ' ';
        std::forward<Writer>(writer)(out);
        std::cerr << out.str() << '\n';
    }

    inline std::atomic<unsigned> &iopTraceSequence()
    {
        static std::atomic<unsigned> sequence{0};
        return sequence;
    }
}
