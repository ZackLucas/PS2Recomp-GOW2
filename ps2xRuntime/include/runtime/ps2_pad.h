#ifndef PS2_PAD_H
#define PS2_PAD_H

#include <cstddef>
#include <cstdint>

class PSPadBackend
{
public:
    PSPadBackend() = default;
    ~PSPadBackend() = default;

    bool readState(int port, int slot, uint8_t *data, size_t size);
};

// Opt-in scripted input for unattended testing. PS2_PAD_SCRIPT is a comma-separated
// list of "tick:BUTTON[+BUTTON...]:ticks" presses, timed in guest VSyncs, e.g.
// "600:START:6,900:DOWN:6,960:CROSS:6". Returns the active-high mask of buttons
// the script holds at `vsyncTick` (same bit layout as the pad's button word).
uint16_t scriptedPadButtons(uint64_t vsyncTick);

#endif
