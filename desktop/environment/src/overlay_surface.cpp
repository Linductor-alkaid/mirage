#include <mirage/desktop/overlay_surface.hpp>

#include <cstddef>

namespace mirage::desktop {

void clamp_overlay_text(std::string &text) {
    if (text.size() <= OverlaySurfaceFrame::kMaxTextBytes) {
        return;
    }
    // Cut at a UTF-8 boundary: step back over continuation bytes so the
    // clipped label never ends mid-sequence (the input contract validates
    // UTF-8; the budget cut keeps the same guarantee).
    std::size_t size = OverlaySurfaceFrame::kMaxTextBytes;
    while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80) {
        --size;
    }
    text.resize(size);
}

} // namespace mirage::desktop
