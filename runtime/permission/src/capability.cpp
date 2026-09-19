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
    case Capability::ClipboardRead:
        return "clipboard.read";
    case Capability::ClipboardWrite:
        return "clipboard.write";
    case Capability::ApplicationLaunch:
        return "application.launch";
    case Capability::ApplicationTerminate:
        return "application.terminate";
    case Capability::NotificationPost:
        return "notification.post";
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
    if (name == "clipboard.read") {
        return Capability::ClipboardRead;
    }
    if (name == "clipboard.write") {
        return Capability::ClipboardWrite;
    }
    if (name == "application.launch") {
        return Capability::ApplicationLaunch;
    }
    if (name == "application.terminate") {
        return Capability::ApplicationTerminate;
    }
    if (name == "notification.post") {
        return Capability::NotificationPost;
    }
    return std::nullopt;
}

} // namespace mirage::runtime::permission
