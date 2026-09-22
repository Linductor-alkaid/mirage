#include <mirage/platform/platform_info.hpp>

namespace mirage::platform {

PlatformInfo platform_info() {
    // The Win32 window/capture/input surface is wired through
    // WindowsDesktopEnvironment (M4-01, DEC-017); later M4 work items refine
    // this identity with UI Automation availability probes. Probes must stay
    // lazy (queried on demand, never at static init).
    return {"windows"};
}

} // namespace mirage::platform
