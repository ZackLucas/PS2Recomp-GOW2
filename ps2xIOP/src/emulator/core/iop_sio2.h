#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace ps2x::iop::detail
{
    // Controller state as seen on the wire: buttons are active-low, sticks centred at 0x80.
    struct IopPadInput
    {
        uint16_t buttons = 0xFFFFu;
        uint8_t rightX = 0x80u;
        uint8_t rightY = 0x80u;
        uint8_t leftX = 0x80u;
        uint8_t leftY = 0x80u;
    };

    // SIO2 serial interface (0x1F808200-0x1F8082FF) with a DualShock 2 on port 0.
    // sio2man queues per-device commands in SEND3, pushes the command bytes through the
    // input FIFO, sets CTRL bit 0 and waits for the IOP interrupt; responses are then
    // read back from the output FIFO.
    class IopSio2
    {
    public:
        using PadReader = std::function<bool(int port, IopPadInput &input)>;

        static constexpr uint32_t Base = 0x1F808200u;
        static constexpr uint32_t End = 0x1F808300u;
        static constexpr int InterruptLine = 17;

        void reset();
        void setPadReader(PadReader reader) { m_padReader = std::move(reader); }

        [[nodiscard]] static bool contains(uint32_t physical) noexcept { return physical >= Base && physical < End; }

        [[nodiscard]] uint32_t read32(uint32_t physical);
        // Returns true when the write started a transfer (the caller raises InterruptLine).
        [[nodiscard]] bool write32(uint32_t physical, uint32_t value);
        [[nodiscard]] uint8_t readFifo();
        void writeFifo(uint8_t value);

    private:
        struct PadPort
        {
            bool config = false;
            bool analog = false;
            bool pressure = false;
            std::array<uint8_t, 6> vibration{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        };

        void execute();
        [[nodiscard]] bool exchange(int port, const std::vector<uint8_t> &command, std::vector<uint8_t> &response);
        void padExchange(PadPort &pad, const IopPadInput &input, const std::vector<uint8_t> &command, std::vector<uint8_t> &response);

        std::array<uint32_t, 16> m_send3{};
        std::array<uint32_t, 4> m_send1{};
        std::array<uint32_t, 4> m_send2{};
        uint32_t m_ctrl = 0u;
        uint32_t m_recv1 = 0x1D100u;
        uint32_t m_recv2 = 0xFu;
        uint32_t m_recv3 = 0u;
        std::deque<uint8_t> m_fifoIn;
        std::deque<uint8_t> m_fifoOut;
        std::array<PadPort, 2> m_pads{};
        PadReader m_padReader;
    };
}
