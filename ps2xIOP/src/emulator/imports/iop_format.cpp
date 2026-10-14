#include "iop_format.h"

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <memory>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr size_t kMaxStringArgument = 4096u;
        constexpr size_t kMaxOutput = 64u * 1024u;
    }

    std::string formatIopString(const IopMemory &memory, std::string_view format, const IopVarargReader &nextArg)
    {
        std::string out;
        size_t i = 0;
        while (i < format.size() && out.size() < kMaxOutput)
        {
            const char c = format[i++];
            if (c != '%')
            {
                out.push_back(c);
                continue;
            }
            if (i >= format.size())
            {
                out.push_back('%');
                break;
            }
            if (format[i] == '%')
            {
                out.push_back('%');
                ++i;
                continue;
            }

            std::string spec = "%";
            while (i < format.size() && std::string_view("-+ #0").find(format[i]) != std::string_view::npos)
                spec.push_back(format[i++]);

            if (i < format.size() && format[i] == '*')
            {
                spec += std::to_string(static_cast<int32_t>(nextArg()));
                ++i;
            }
            else
            {
                while (i < format.size() && std::isdigit(static_cast<unsigned char>(format[i])))
                    spec.push_back(format[i++]);
            }

            if (i < format.size() && format[i] == '.')
            {
                spec.push_back(format[i++]);
                if (i < format.size() && format[i] == '*')
                {
                    spec += std::to_string(static_cast<int32_t>(nextArg()));
                    ++i;
                }
                else
                {
                    while (i < format.size() && std::isdigit(static_cast<unsigned char>(format[i])))
                        spec.push_back(format[i++]);
                }
            }

            bool wide = false; // 64-bit argument
            while (i < format.size() && std::string_view("hlLqjzt").find(format[i]) != std::string_view::npos)
            {
                if (format[i] == 'q' || format[i] == 'L' ||
                    (format[i] == 'l' && i + 1 < format.size() && format[i + 1] == 'l'))
                {
                    wide = true;
                    if (format[i] == 'l')
                        ++i;
                }
                ++i;
            }
            if (i >= format.size())
                break;

            const char conversion = format[i++];
            char buffer[128];
            switch (conversion)
            {
            case 'd':
            case 'i':
            {
                int64_t value = static_cast<int32_t>(nextArg());
                if (wide)
                {
                    const uint32_t high = nextArg();
                    value = static_cast<int64_t>((static_cast<uint64_t>(high) << 32) | static_cast<uint32_t>(value));
                }
                std::snprintf(buffer, sizeof(buffer), (spec + "lld").c_str(), static_cast<long long>(value));
                out += buffer;
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'o':
            {
                uint64_t value = nextArg();
                if (wide)
                    value |= static_cast<uint64_t>(nextArg()) << 32;
                std::snprintf(buffer, sizeof(buffer), (spec + "ll" + conversion).c_str(),
                              static_cast<unsigned long long>(value));
                out += buffer;
                break;
            }
            case 'p':
                std::snprintf(buffer, sizeof(buffer), "0x%08x", nextArg());
                out += buffer;
                break;
            case 'c':
                std::snprintf(buffer, sizeof(buffer), (spec + 'c').c_str(), static_cast<int>(nextArg() & 0xFFu));
                out += buffer;
                break;
            case 's':
            {
                const uint32_t address = nextArg();
                const std::string text = address != 0u ? memory.readString(address, kMaxStringArgument) : "(null)";
                const int needed = std::snprintf(nullptr, 0, (spec + 's').c_str(), text.c_str());
                if (needed > 0)
                {
                    std::unique_ptr<char[]> formatted(new char[static_cast<size_t>(needed) + 1u]);
                    std::snprintf(formatted.get(), static_cast<size_t>(needed) + 1u, (spec + 's').c_str(), text.c_str());
                    out += formatted.get();
                }
                break;
            }
            case 'n':
                (void)nextArg();
                break;
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
                // Doubles take an aligned register pair under o32.
                (void)nextArg();
                (void)nextArg();
                out += "?";
                break;
            default:
                out += spec;
                out.push_back(conversion);
                break;
            }
        }
        return out;
    }

    IopVarargReader makeCallVarargs(const IopCpuState &cpu, const IopMemory &memory, uint32_t firstSlot)
    {
        auto slot = std::make_shared<uint32_t>(firstSlot);
        const uint32_t sp = cpu.gpr[29];
        std::array<uint32_t, 4> registers{cpu.gpr[4], cpu.gpr[5], cpu.gpr[6], cpu.gpr[7]};
        return [slot, sp, registers, &memory]() -> uint32_t
        {
            const uint32_t index = (*slot)++;
            return index < 4u ? registers[index] : memory.read32(sp + index * 4u);
        };
    }

    IopVarargReader makeVaListVarargs(const IopMemory &memory, uint32_t vaList)
    {
        auto cursor = std::make_shared<uint32_t>(vaList);
        return [cursor, &memory]() -> uint32_t
        {
            const uint32_t value = memory.read32(*cursor);
            *cursor += 4u;
            return value;
        };
    }
}
