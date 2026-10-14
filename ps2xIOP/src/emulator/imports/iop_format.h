#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace ps2x::iop::detail
{
    struct IopCpuState;
    class IopMemory;

    // Yields successive 32-bit variadic argument slots.
    using IopVarargReader = std::function<uint32_t()>;

    // printf-style formatting for IOP modules (%d %i %u %x %X %o %c %s %p %%, flags,
    // width, precision and h/hh/l/ll/z/j/t length modifiers). The IOP has no FPU, so
    // floating point conversions only consume their argument.
    [[nodiscard]] std::string formatIopString(const IopMemory &memory,
                                              std::string_view format,
                                              const IopVarargReader &nextArg);

    // Variadic arguments of a call under the o32 ABI: slot n lives in $a0+n for n < 4
    // and at sp + 4 * n otherwise. firstSlot is the index of the first variadic argument.
    [[nodiscard]] IopVarargReader makeCallVarargs(const IopCpuState &cpu, const IopMemory &memory, uint32_t firstSlot);

    // Variadic arguments behind a va_list pointer.
    [[nodiscard]] IopVarargReader makeVaListVarargs(const IopMemory &memory, uint32_t vaList);
}
