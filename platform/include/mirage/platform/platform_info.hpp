#pragma once

#include <string>

namespace mirage::platform {

/// Identity of the operating system backend currently compiled into the
/// binary (design doc section 10). Later milestones extend this with display
/// server (X11/Wayland) and accessibility stack (AT-SPI2/UI Automation)
/// details, reported through the same structure.
struct PlatformInfo {
    std::string os;
};

PlatformInfo platform_info();

} // namespace mirage::platform
