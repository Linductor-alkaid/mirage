#pragma once

namespace mirage::integration {

/// Contract-facing adapter between Mirage's DesktopEnvironment and the hosted
/// Mira instance (design doc section 11). Implementations translate Mira's
/// environment requests into Desktop Environment calls and feed agent events
/// back to the runtime layer.
///
/// The first concrete binding lands with M1; the interface only fixes the
/// adapter's identity so the dependency direction (runtime -> integration ->
/// desktop) is locked by the build graph.
class DesktopEnvironmentBinding {
public:
    virtual ~DesktopEnvironmentBinding() = default;

    /// Stable adapter identity, e.g. "mirage.desktop.linux-v1".
    virtual const char* binding_name() const = 0;
};

} // namespace mirage::integration
