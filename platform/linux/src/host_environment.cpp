#include <mirage/platform/host_environment.hpp>

namespace mirage::platform {

// Linux desktops scale per output through the display server (X11 DPI
// resources, Wayland fractional-scale-v1): there is no Win32-style
// process-wide declaration, and the Linux backends already enumerate,
// capture and inject in the desktop's pixel space end to end.
DpiAwareness declare_host_dpi_awareness() { return DpiAwareness::unavailable; }

} // namespace mirage::platform
