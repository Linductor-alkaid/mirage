#pragma once

#include <memory>

#include <mirage/desktop/desktop_environment.hpp>

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

    /// The desktop surface this binding wraps; the M1 runtime service's
    /// task drivers act on it (DEC-007 item 5). Returns null by default so
    /// bindings without a mirage environment fail closed at their consumers
    /// instead of breaking existing implementations.
    virtual std::shared_ptr<mirage::desktop::DesktopEnvironment>
    bound_environment() const {
        return nullptr;
    }
};

} // namespace mirage::integration
