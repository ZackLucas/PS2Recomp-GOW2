#include "iop_sio2.h"

#include <algorithm>
#include <initializer_list>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kSend3Base = 0x1F808200u;
        constexpr uint32_t kSend12Base = 0x1F808240u;
        constexpr uint32_t kFifoIn = 0x1F808260u;
        constexpr uint32_t kFifoOut = 0x1F808264u;
        constexpr uint32_t kCtrl = 0x1F808268u;
        constexpr uint32_t kRecv1 = 0x1F80826Cu;
        constexpr uint32_t kRecv2 = 0x1F808270u;
        constexpr uint32_t kRecv3 = 0x1F808274u;

        constexpr uint32_t kCtrlStart = 0x1u;
        constexpr uint32_t kCtrlResetFifos = 0xCu;

        constexpr uint32_t kRecv1DevicePresent = 0x1100u;
        constexpr uint32_t kRecv1NoDevice = 0x1D100u;

        constexpr uint8_t kPadAddress = 0x01u;
        constexpr uint8_t kModeDigital = 0x41u;
        constexpr uint8_t kModeAnalog = 0x73u;
        constexpr uint8_t kModePressure = 0x79u;
        constexpr uint8_t kModeConfig = 0xF3u;
    }

    void IopSio2::reset()
    {
        m_send3.fill(0u);
        m_send1.fill(0u);
        m_send2.fill(0u);
        m_ctrl = 0u;
        m_recv1 = kRecv1NoDevice;
        m_recv2 = 0xFu;
        m_recv3 = 0u;
        m_fifoIn.clear();
        m_fifoOut.clear();
        m_pads = {};
    }

    uint32_t IopSio2::read32(uint32_t physical)
    {
        if (physical >= kSend3Base && physical < kSend3Base + 0x40u)
            return m_send3[(physical - kSend3Base) / 4u];
        if (physical >= kSend12Base && physical < kSend12Base + 0x20u)
        {
            const uint32_t index = (physical - kSend12Base) / 8u;
            return ((physical - kSend12Base) & 4u) ? m_send2[index] : m_send1[index];
        }
        switch (physical)
        {
        case kFifoOut:
            return readFifo();
        case kCtrl:
            return m_ctrl;
        case kRecv1:
            return m_recv1;
        case kRecv2:
            return m_recv2;
        case kRecv3:
            return m_recv3;
        default:
            return 0u;
        }
    }

    bool IopSio2::write32(uint32_t physical, uint32_t value)
    {
        if (physical >= kSend3Base && physical < kSend3Base + 0x40u)
        {
            m_send3[(physical - kSend3Base) / 4u] = value;
            return false;
        }
        if (physical >= kSend12Base && physical < kSend12Base + 0x20u)
        {
            const uint32_t index = (physical - kSend12Base) / 8u;
            (((physical - kSend12Base) & 4u) ? m_send2 : m_send1)[index] = value;
            return false;
        }
        switch (physical)
        {
        case kFifoIn:
            writeFifo(static_cast<uint8_t>(value));
            return false;
        case kCtrl:
        {
            // START and the FIFO reset bits are self-clearing.
            m_ctrl = value & ~(kCtrlStart | kCtrlResetFifos);
            if ((value & kCtrlStart) != 0u)
            {
                execute();
                return true;
            }
            if ((value & kCtrlResetFifos) != 0u)
            {
                m_fifoIn.clear();
                m_fifoOut.clear();
            }
            return false;
        }
        default:
            return false;
        }
    }

    uint8_t IopSio2::readFifo()
    {
        if (m_fifoOut.empty())
            return 0xFFu;
        const uint8_t value = m_fifoOut.front();
        m_fifoOut.pop_front();
        return value;
    }

    void IopSio2::writeFifo(uint8_t value)
    {
        m_fifoIn.push_back(value);
    }

    void IopSio2::execute()
    {
        bool anyDevice = false;
        bool anyMissing = false;
        for (const uint32_t entry : m_send3)
        {
            if (entry == 0u)
                break;
            const int port = static_cast<int>(entry & 3u);
            const uint32_t inLength = (entry >> 8) & 0x1FFu;
            const uint32_t outLength = (entry >> 18) & 0x1FFu;

            std::vector<uint8_t> command;
            command.reserve(inLength);
            for (uint32_t i = 0; i < inLength && !m_fifoIn.empty(); ++i)
            {
                command.push_back(m_fifoIn.front());
                m_fifoIn.pop_front();
            }

            std::vector<uint8_t> response;
            const bool present = exchange(port, command, response);
            anyDevice |= present;
            anyMissing |= !present;
            response.resize(outLength, present ? 0x00u : 0xFFu);
            for (const uint8_t byte : response)
                m_fifoOut.push_back(byte);
        }
        m_fifoIn.clear();
        m_recv1 = (anyDevice && !anyMissing) ? kRecv1DevicePresent : kRecv1NoDevice;
        m_recv2 = 0xFu;
        m_recv3 = 0u;
    }

    bool IopSio2::exchange(int port, const std::vector<uint8_t> &command, std::vector<uint8_t> &response)
    {
        // Ports 0/1 are the controller slots, 2/3 the memory card slots (left empty).
        if (port > 1 || command.empty() || command[0] != kPadAddress)
            return false;

        IopPadInput input;
        if (!m_padReader || !m_padReader(port, input))
            return false;

        padExchange(m_pads[static_cast<size_t>(port)], input, command, response);
        return true;
    }

    void IopSio2::padExchange(PadPort &pad, const IopPadInput &input, const std::vector<uint8_t> &command, std::vector<uint8_t> &response)
    {
        const auto arg = [&](size_t index) -> uint8_t
        { return index < command.size() ? command[index] : 0u; };
        const uint8_t mode = pad.config ? kModeConfig : pad.analog ? (pad.pressure ? kModePressure : kModeAnalog)
                                                                   : kModeDigital;
        const uint8_t opcode = arg(1);
        response = {0xFFu, mode, 0x5Au};

        const auto appendPadData = [&]()
        {
            response.push_back(static_cast<uint8_t>(input.buttons & 0xFFu));
            response.push_back(static_cast<uint8_t>(input.buttons >> 8));
            if (mode == kModeDigital)
                return;
            response.push_back(input.rightX);
            response.push_back(input.rightY);
            response.push_back(input.leftX);
            response.push_back(input.leftY);
            if (mode != kModePressure)
                return;
            // Pressure bytes: right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2.
            static constexpr std::array<uint16_t, 12> kPressureOrder{0x0020u, 0x0080u, 0x0010u, 0x0040u, 0x1000u, 0x2000u,
                                                                     0x4000u, 0x8000u, 0x0400u, 0x0800u, 0x0100u, 0x0200u};
            for (const uint16_t mask : kPressureOrder)
                response.push_back((input.buttons & mask) == 0u ? 0xFFu : 0x00u);
        };
        const auto appendBytes = [&](std::initializer_list<uint8_t> bytes)
        { response.insert(response.end(), bytes.begin(), bytes.end()); };

        switch (opcode)
        {
        case 0x42: // read data (vibration motor bytes in the command are ignored)
            appendPadData();
            break;
        case 0x43: // enter/exit config mode
            if (pad.config)
                appendBytes({0, 0, 0, 0, 0, 0});
            else
                appendPadData();
            pad.config = arg(3) == 1u;
            break;
        case 0x44: // set analog/digital mode
            pad.analog = arg(3) == 1u;
            if (!pad.analog)
                pad.pressure = false;
            appendBytes({0, 0, 0, 0, 0, 0});
            break;
        case 0x45: // query model: DualShock 2, current LED state
            appendBytes({0x03, 0x02, static_cast<uint8_t>(pad.analog ? 1u : 0u), 0x02, 0x01, 0x00});
            break;
        case 0x46: // query act
            if (arg(3) == 0u)
                appendBytes({0x00, 0x00, 0x01, 0x02, 0x00, 0x0A});
            else
                appendBytes({0x00, 0x00, 0x01, 0x01, 0x01, 0x14});
            break;
        case 0x47: // query comb
            appendBytes({0x00, 0x00, 0x02, 0x00, 0x01, 0x00});
            break;
        case 0x4C: // query mode
            appendBytes({0x00, 0x00, 0x00, static_cast<uint8_t>(arg(3) == 0u ? 0x04u : 0x07u), 0x00, 0x00});
            break;
        case 0x4D: // vibration mapping: reply with the previous one
            response.insert(response.end(), pad.vibration.begin(), pad.vibration.end());
            for (size_t i = 0; i < pad.vibration.size(); ++i)
                pad.vibration[i] = arg(3 + i);
            break;
        case 0x4F: // enable pressure-sensitive buttons
            pad.pressure = true;
            pad.analog = true;
            appendBytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x5A});
            break;
        case 0x40: // set VREF
            appendBytes({0x00, 0x00, 0x02, 0x00, 0x00, 0x5A});
            break;
        case 0x41: // query button mask (all buttons pressure capable)
            appendBytes({0xFF, 0xFF, 0x03, 0x00, 0x00, 0x5A});
            break;
        default:
            appendBytes({0, 0, 0, 0, 0, 0});
            break;
        }
    }
}
