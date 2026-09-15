#include <mirage/platform/platform_info.hpp>

namespace mirage::platform {

PlatformInfo platform_info() {
    // M2 refines this with display server and AT-SPI2 availability probes;
    // probes must stay lazy (queried on demand, never at static init).
    return {"linux"};
}

} // namespace mirage::platform
