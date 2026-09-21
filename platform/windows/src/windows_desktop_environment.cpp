#include <mirage/platform/windows/windows_desktop_environment.hpp>

#include "win32_backend.hpp"

namespace mirage::platform::windows_backend {

WindowsDesktopEnvironment::WindowsDesktopEnvironment(Win32Options win32_options) {
    if (win32_options.enabled) {
        win32_ = Win32Backend::open();
        // A failed probe (no interactive display in this session)
        // intentionally leaves win32_ null: the window / screen / input
        // accessors report an absent capability instead of returning broken
        // providers (DEC-017 capability honesty).
    }
}

WindowsDesktopEnvironment::~WindowsDesktopEnvironment() = default;

mirage::desktop::EnvironmentInfo WindowsDesktopEnvironment::info() const {
    return {"mirage-windows", "windows"};
}

mirage::desktop::WindowProvider *WindowsDesktopEnvironment::window() {
    return win32_ != nullptr ? win32_->window() : nullptr;
}

mirage::desktop::ScreenProvider *WindowsDesktopEnvironment::screen() {
    return win32_ != nullptr ? win32_->screen() : nullptr;
}

mirage::desktop::InputProvider *WindowsDesktopEnvironment::input() {
    return win32_ != nullptr ? win32_->input() : nullptr;
}

} // namespace mirage::platform::windows_backend
