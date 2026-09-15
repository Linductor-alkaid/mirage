#include <mirage/platform/platform_info.hpp>

namespace mirage::platform {

PlatformInfo platform_info() {
    // M4 refines this with UI Automation and Win32 availability probes;
    // probes must stay lazy (queried on demand, never at static init).
    return {"windows"};
}

} // namespace mirage::platform
