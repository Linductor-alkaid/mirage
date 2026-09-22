#include <mirage/platform/windows/windows_desktop_environment.hpp>

#include "uia_backend.hpp"
#include "win32_backend.hpp"

namespace mirage::platform::windows_backend {

WindowsDesktopEnvironment::WindowsDesktopEnvironment(Win32Options win32_options,
                                                     UiaOptions uia_options) {
    if (win32_options.enabled) {
        win32_ = Win32Backend::open();
        // A failed probe (no interactive display in this session)
        // intentionally leaves win32_ null: the window / screen / input
        // accessors report an absent capability instead of returning broken
        // providers (DEC-017 capability honesty).
    }
    if (uia_options.enabled) {
        uia_ = UiaBackend::open();
        // A failed probe (UIA client core unavailable, or the constructing
        // thread is bound to a foreign COM apartment) intentionally leaves
        // uia_ null — fail closed, same honesty discipline.
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

mirage::desktop::AccessibilityProvider *WindowsDesktopEnvironment::accessibility() {
    return uia_ != nullptr ? uia_->accessibility() : nullptr;
}

} // namespace mirage::platform::windows_backend
