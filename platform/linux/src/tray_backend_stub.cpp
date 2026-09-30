// Link-time stub for machines without the gio-unix dev packages: the tray
// carrier exists (so callers compile unchanged) but reports no capability,
// and no gio header is touched. See platform/CMakeLists.txt and DEC-030
// decision 4 (capability honesty, DEC-015 precedent).

#include "tray_backend.hpp"

#include <cstdlib>

namespace mirage::platform::linux_backend {

// The member unique_ptr's default destruction needs the nested type
// complete in this translation unit (the overlay stub's XConnection
// precedent).
struct GioTrayCarrier::Surface {};

GioTrayCarrier::~GioTrayCarrier() = default;

std::unique_ptr<GioTrayCarrier> GioTrayCarrier::open() {
    return nullptr; // fail closed: no gio support in this build (DEC-015)
}

mirage::desktop::TrayCarrier::RunReport
GioTrayCarrier::run(const mirage::desktop::TrayCarrierContext &, const std::function<bool()> &) {
    std::abort(); // unreachable: open() is null in this build
}

void GioTrayCarrier::wakeup() {
    std::abort(); // unreachable: open() is null in this build
}

std::unique_ptr<mirage::desktop::TrayCarrier> open_tray_carrier() {
    return nullptr; // fail closed: no gio support in this build (DEC-030)
}

} // namespace mirage::platform::linux_backend
