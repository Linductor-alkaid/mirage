#pragma once

#include <string>

namespace mirage::runtime {

/// Identity of the Mirage background runtime service (design doc section 12).
struct ServiceInfo {
    std::string name;
    bool background_capable = false;
};

ServiceInfo runtime_service_info();

} // namespace mirage::runtime
