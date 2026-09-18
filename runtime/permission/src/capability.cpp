#include <mirage/runtime/permission/capability.hpp>

namespace mirage::runtime::permission {

const char *capability_name(Capability capability) {
    switch (capability) {
    case Capability::FilesystemRead:
        return "filesystem.read";
    case Capability::FilesystemWrite:
        return "filesystem.write";
    case Capability::ProcessExecute:
        return "process.execute";
    case Capability::WindowActivate:
        return "window.activate";
    case Capability::ScreenCapture:
        return "screen.capture";
    case Capability::InputInject:
        return "input.inject";
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
    if (name == "window.activate") {
        return Capability::WindowActivate;
    }
    if (name == "screen.capture") {
        return Capability::ScreenCapture;
    }
    if (name == "input.inject") {
        return Capability::InputInject;
    }
    return std::nullopt;
}

} // namespace mirage::runtime::permission
