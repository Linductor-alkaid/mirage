#include <mirage/runtime/permission/capability.hpp>

namespace mirage::runtime::permission {

const char* capability_name(Capability capability) {
    switch (capability) {
    case Capability::FilesystemRead:
        return "filesystem.read";
    case Capability::FilesystemWrite:
        return "filesystem.write";
    case Capability::ProcessExecute:
        return "process.execute";
    }
    return "unknown";
}

std::optional<Capability> capability_from_name(std::string_view name) {
    if (name == "filesystem.read") {
        return Capability::FilesystemRead;
    }
    if (name == "filesystem.write") {
        return Capability::FilesystemWrite;
    }
    if (name == "process.execute") {
        return Capability::ProcessExecute;
    }
    return std::nullopt;
}

} // namespace mirage::runtime::permission
