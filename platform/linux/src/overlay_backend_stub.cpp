// Link-time stub for machines without the X11/Xshape dev packages: the
// overlay carrier exists (so callers compile unchanged) but reports no
// capability, and no X11 header is touched. See platform/CMakeLists.txt
// and DEC-029 decision 4 (capability honesty, DEC-015 precedent).

#include "overlay_backend.hpp"

#include <cstdlib>

namespace mirage::platform::linux_backend {

X11OverlayCarrier::~X11OverlayCarrier() = default;

std::unique_ptr<X11OverlayCarrier> X11OverlayCarrier::open(const std::string &) {
    return nullptr; // fail closed: no X11 support in this build (DEC-015)
}

mirage::desktop::OverlayCarrier::RunReport
X11OverlayCarrier::run(const mirage::desktop::OverlayCarrierContext &,
                       const std::function<bool()> &) {
    std::abort(); // unreachable: open() is null in this build
}

void X11OverlayCarrier::wakeup() {
    std::abort(); // unreachable: open() is null in this build
}

std::unique_ptr<mirage::desktop::OverlayCarrier> open_overlay_carrier(const std::string &) {
    return nullptr; // fail closed: no X11 support in this build (DEC-029)
}

} // namespace mirage::platform::linux_backend
