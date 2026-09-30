#include <mirage/desktop/tray_carrier.hpp>

#include <cstddef>

namespace mirage::desktop {

void clamp_tray_status(std::string &status) {
    if (status.size() <= TrayState::kMaxStatusBytes) {
        return;
    }
    // Cut at a UTF-8 boundary so the clipped status never ends mid-sequence
    // (the overlay's clip discipline, DEC-029).
    std::size_t size = TrayState::kMaxStatusBytes;
    while (size > 0 && (static_cast<unsigned char>(status[size]) & 0xC0) == 0x80) {
        --size;
    }
    status.resize(size);
}

} // namespace mirage::desktop
