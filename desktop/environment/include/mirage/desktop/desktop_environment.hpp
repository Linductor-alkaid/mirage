#pragma once

#include <string>

#include <mirage/desktop/accessibility_provider.hpp>
#include <mirage/desktop/application_provider.hpp>
#include <mirage/desktop/clipboard_provider.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/notification_provider.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/desktop/screen_provider.hpp>
#include <mirage/desktop/window_provider.hpp>

namespace mirage::desktop {

/// Identity of one DesktopEnvironment instance (design doc section 5).
struct EnvironmentInfo {
    std::string name;
    std::string platform; ///< "linux" or "windows" (design doc section 10)
};

/// Unified PC environment surface presented to Mira (design doc section 5).
///
/// The provider accessors below return a null pointer when the environment
/// does not carry that capability; consumers must fail closed on null and
/// never assume a provider exists. The environment owns its providers, so
/// the pointers stay valid for the environment's lifetime:
///
/// - M1: FilesystemProvider, ProcessProvider
/// - M2: ApplicationProvider, WindowProvider, AccessibilityProvider,
///       ScreenProvider, InputProvider, ClipboardProvider,
///       NotificationProvider
///
/// None of these interfaces may leak platform or third-party types. The
/// public observation contract they feed is frozen as
/// kObservationSchemaVersion "1.0" (DEC-005).
///
/// Lifetime: an owner in the runtime layer constructs the concrete backend
/// (platform/windows or platform/linux) and keeps it alive for as long as the
/// bound Mira instance runs.
class DesktopEnvironment {
  public:
    virtual ~DesktopEnvironment() = default;

    virtual EnvironmentInfo info() const = 0;

    virtual FilesystemProvider *filesystem() { return nullptr; }
    virtual ProcessProvider *process() { return nullptr; }
    virtual ApplicationProvider *application() { return nullptr; }
    virtual WindowProvider *window() { return nullptr; }
    virtual AccessibilityProvider *accessibility() { return nullptr; }
    virtual ScreenProvider *screen() { return nullptr; }
    virtual InputProvider *input() { return nullptr; }
    virtual ClipboardProvider *clipboard() { return nullptr; }
    virtual NotificationProvider *notification() { return nullptr; }
};

} // namespace mirage::desktop
