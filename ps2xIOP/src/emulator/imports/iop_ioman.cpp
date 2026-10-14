#include "iop_ioman.h"

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"
#include "../services/iop_rpc.h"
#include "ps2x/iop/iop_host.h"

#include <algorithm>
#include <cctype>

namespace ps2x::iop::detail
{
    namespace
    {
        // ioman error codes are negated errno values.
        constexpr int32_t kEnoent = -2;
        constexpr int32_t kEbadf = -9;
        constexpr int32_t kEinval = -22;
        constexpr int32_t kEmfile = -24;
        constexpr uint32_t kOpenWriteBit = 0x2u; // O_RDONLY = 1, O_WRONLY = 2, O_RDWR = 3
        constexpr size_t kMaxOpenFiles = 64u;
        constexpr uint32_t kMaxReadChunk = 2u * 1024u * 1024u;
    }

    IopIoman::IopIoman(IopHost &host, IopMemory &memory) noexcept
        : m_host(host), m_memory(memory)
    {
    }

    IopIoman::~IopIoman()
    {
        closeAllFiles();
    }

    void IopIoman::reset()
    {
        m_devices.clear();
        closeAllFiles();
    }

    void IopIoman::closeAllFiles()
    {
        for (auto &[fd, file] : m_files)
            m_host.closeHostFile(file.handle);
        m_files.clear();
        m_nextFd = 3;
    }

    bool IopIoman::isRegisteredDevicePath(const std::string &path) const
    {
        const size_t colon = path.find(':');
        if (colon == std::string::npos)
            return false;
        std::string device = path.substr(0, colon);
        while (!device.empty() && std::isdigit(static_cast<unsigned char>(device.back())))
            device.pop_back();
        return std::any_of(m_devices.begin(), m_devices.end(),
                           [&](const Device &candidate)
                           { return candidate.name == device; });
    }

    bool IopIoman::dispatchImport(uint16_t ordinal, IopCpuState &cpu, IopGuestExecutor &executor)
    {
        constexpr size_t kMaxDevices = 16u;
        const uint32_t a0 = cpu.gpr[4];
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };

        switch (ordinal)
        {
        case 4: // open(const char *name, int mode)
        {
            const std::string path = m_memory.readString(a0, 256u);
            // Devices registered by guest drivers (AddDrv) would need their ops run on the IOP.
            if (path.empty() || isRegisteredDevicePath(path))
                return false;
            // Only reads are served; some modules pass 0 instead of O_RDONLY.
            if ((cpu.gpr[5] & kOpenWriteBit) != 0u)
            {
                m_host.log(LogLevel::Warning, "[IOP ioman] open for writing not supported: '" + path + "'");
                setV0(static_cast<uint32_t>(kEinval));
                return true;
            }
            if (m_files.size() >= kMaxOpenFiles)
            {
                setV0(static_cast<uint32_t>(kEmfile));
                return true;
            }

            const std::string translated = m_host.translateGuestPath(path);
            const uint64_t handle = m_host.openHostFile(translated.empty() ? std::string_view(path)
                                                                           : std::string_view(translated));
            uint64_t size = 0u;
            if (handle == 0u || !m_host.hostFileSize(handle, size))
            {
                if (handle != 0u)
                    m_host.closeHostFile(handle);
                m_host.log(LogLevel::Warning, "[IOP ioman] open failed: '" + path + "'");
                setV0(static_cast<uint32_t>(kEnoent));
                return true;
            }

            const int32_t fd = m_nextFd++;
            m_files.emplace(fd, OpenFile{handle, 0u, size});
            m_host.log(LogLevel::Info, "[IOP ioman] open '" + path + "' fd=" + std::to_string(fd) +
                                           " size=" + std::to_string(size));
            setV0(static_cast<uint32_t>(fd));
            return true;
        }
        case 5: // close(int fd)
        {
            const auto file = m_files.find(static_cast<int32_t>(a0));
            if (file == m_files.end())
                return false;
            m_host.closeHostFile(file->second.handle);
            m_files.erase(file);
            setV0(0u);
            return true;
        }
        case 6: // read(int fd, void *buffer, int size)
        {
            const auto file = m_files.find(static_cast<int32_t>(a0));
            if (file == m_files.end())
                return false;
            const int32_t requested = static_cast<int32_t>(cpu.gpr[6]);
            if (requested < 0)
            {
                setV0(static_cast<uint32_t>(kEinval));
                return true;
            }

            uint32_t destination = cpu.gpr[5];
            uint32_t remaining = static_cast<uint32_t>(requested);
            uint32_t total = 0u;
            std::vector<uint8_t> chunk;
            while (remaining != 0u)
            {
                chunk.resize(std::min(remaining, kMaxReadChunk));
                size_t bytesRead = 0u;
                if (!m_host.readHostFile(file->second.handle, file->second.position, chunk.data(), chunk.size(), bytesRead))
                {
                    setV0(total != 0u ? total : static_cast<uint32_t>(kEbadf));
                    return true;
                }
                if (bytesRead == 0u)
                    break;
                if (!m_memory.writeRam(destination, chunk.data(), bytesRead))
                {
                    setV0(static_cast<uint32_t>(kEinval));
                    return true;
                }
                file->second.position += bytesRead;
                destination += static_cast<uint32_t>(bytesRead);
                remaining -= static_cast<uint32_t>(bytesRead);
                total += static_cast<uint32_t>(bytesRead);
                if (bytesRead < chunk.size())
                    break;
            }
            setV0(total);
            return true;
        }
        case 8: // lseek(int fd, int offset, int whence)
        {
            const auto file = m_files.find(static_cast<int32_t>(a0));
            if (file == m_files.end())
                return false;
            const int64_t offset = static_cast<int32_t>(cpu.gpr[5]);
            int64_t base = 0;
            switch (cpu.gpr[6])
            {
            case 0: // SEEK_SET
                base = 0;
                break;
            case 1: // SEEK_CUR
                base = static_cast<int64_t>(file->second.position);
                break;
            case 2: // SEEK_END
                base = static_cast<int64_t>(file->second.size);
                break;
            default:
                setV0(static_cast<uint32_t>(kEinval));
                return true;
            }
            const int64_t position = base + offset;
            if (position < 0 || position > 0x7FFFFFFF)
            {
                setV0(static_cast<uint32_t>(kEinval));
                return true;
            }
            file->second.position = static_cast<uint64_t>(position);
            setV0(static_cast<uint32_t>(position));
            return true;
        }
        case 20: // AddDrv
        {
            if (a0 == 0u || m_devices.size() >= kMaxDevices)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const uint32_t nameAddress = m_memory.read32(a0);
            const uint32_t operations = m_memory.read32(a0 + 16u);
            const std::string name = m_memory.readString(nameAddress, 64u);
            if (nameAddress == 0u || operations == 0u || name.empty())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            m_devices.push_back({a0, cpu.gpr[28], name});
            const uint32_t init = m_memory.read32(operations);
            if (init != 0u)
            {
                const int32_t result = static_cast<int32_t>(
                    executor.executeGuestFunction(init, a0, 0u, 0u, 0u, cpu.gpr[28]));
                if (result < 0)
                {
                    m_devices.pop_back();
                    setV0(0xFFFFFFFFu);
                    return true;
                }
            }

            setV0(0u);
            return true;
        }
        case 21: // DelDrv
        {
            const std::string name = m_memory.readString(a0, 64u);
            const auto device = std::find_if(
                m_devices.begin(), m_devices.end(),
                [&](const Device &candidate)
                { return candidate.name == name; });
            if (device == m_devices.end())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const uint32_t operations = m_memory.read32(device->address + 16u);
            const uint32_t deinit = operations != 0u ? m_memory.read32(operations + 4u) : 0u;
            if (deinit != 0u)
                (void)executor.executeGuestFunction(deinit, device->address, 0u, 0u, 0u, device->gp);
            m_devices.erase(device);
            setV0(0u);
            return true;
        }
        default:
            return false;
        }
    }
}
