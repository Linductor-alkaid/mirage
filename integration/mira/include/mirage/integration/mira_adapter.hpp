#pragma once

#include <memory>

namespace mirage::integration {

/// Bridge from Mirage's DesktopEnvironment to the hosted Mira instance's
/// environment contract (design doc section 11).
///
/// This interface stays free of pinned-contract types so runtime public
/// headers that name it keep the dependency boundary (DEC-003). The concrete
/// adapter (M1-03) has a most-derived type that additionally implements the
/// pinned environment interface; MiraHost::start() resolves that contract
/// with a runtime cross-cast and fails closed when a binding does not carry
/// it. Dependency direction locked by the build graph: runtime ->
/// integration -> desktop.
class DesktopEnvironmentBinding {
public:
    virtual ~DesktopEnvironmentBinding() = default;

    /// Stable adapter identity, e.g. "mirage.desktop.linux-v1".
    virtual const char* binding_name() const = 0;
};

} // namespace mirage::integration
