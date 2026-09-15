#pragma once

#include <string>

#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/process_provider.hpp>

namespace mirage::desktop {

/// Identity of one DesktopEnvironment instance (design doc section 5).
struct EnvironmentInfo {
    std::string name;
    std::string platform; ///< "linux" or "windows" (design doc section 10)
};

/// Unified PC environment surface presented to Mira (design doc section 5).
///
/// The skeleton carries identity only. The provider accessors listed below
/// are introduced milestone by milestone; none of them may leak platform or
/// third-party types into this interface:
///
/// - M1: FilesystemProvider, ProcessProvider (Phase 1 of the roadmap)
/// - M2: ApplicationProvider, WindowProvider, AccessibilityProvider,
///       ScreenProvider, InputProvider, ClipboardProvider,
///       NotificationProvider
///
/// Lifetime: an owner in the runtime layer constructs the concrete backend
/// (platform/windows or platform/linux) and keeps it alive for as long as the
/// bound Mira instance runs.
class DesktopEnvironment {
public:
    virtual ~DesktopEnvironment() = default;

    virtual EnvironmentInfo info() const = 0;

    /// Provider accessors (design doc section 5). A null pointer means the
    /// provider is not part of this environment and consumers must fail
    /// closed. The environment owns its providers, so the pointers stay valid
    /// for the environment's lifetime.
    virtual FilesystemProvider* filesystem() { return nullptr; }
    virtual ProcessProvider* process() { return nullptr; }
};

} // namespace mirage::desktop
