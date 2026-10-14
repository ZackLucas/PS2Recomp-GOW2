#pragma once

#include <cstdint>

namespace ps2x::iop::detail
{
    struct IopCpuState;
    class IopMemory;

    class IopSysclib
    {
    public:
        explicit IopSysclib(IopMemory &memory) noexcept;

        [[nodiscard]] bool dispatchImport(uint16_t ordinal, IopCpuState &cpu);

    private:
        // Tokenizes from address `start` (0 = continue from *savePtr) like strtok_r.
        uint32_t tokenize(uint32_t start, uint32_t delimiters, uint32_t &savePtr);

        IopMemory &m_memory;
        uint32_t m_strtokNext = 0u;
    };
}
