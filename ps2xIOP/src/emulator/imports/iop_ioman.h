#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps2x::iop::detail
{
    struct IopCpuState;
    class IopGuestExecutor;
    class IopMemory;
}

namespace ps2x::iop
{
    class IopHost;
}

namespace ps2x::iop::detail
{

    class IopIoman
    {
    public:
        IopIoman(IopHost &host, IopMemory &memory) noexcept;
        ~IopIoman();

        void reset();
        [[nodiscard]] bool dispatchImport(uint16_t ordinal, IopCpuState &cpu, IopGuestExecutor &executor);

    private:
        struct Device
        {
            uint32_t address = 0u;
            uint32_t gp = 0u;
            std::string name;
        };

        // Files opened through the host (cdrom0:, host0:...): ioman fd -> host handle.
        struct OpenFile
        {
            uint64_t handle = 0u;
            uint64_t position = 0u;
            uint64_t size = 0u;
        };

        bool isRegisteredDevicePath(const std::string &path) const;
        void closeAllFiles();

        IopHost &m_host;
        IopMemory &m_memory;
        std::vector<Device> m_devices;
        std::unordered_map<int32_t, OpenFile> m_files;
        int32_t m_nextFd = 3;
    };
}
