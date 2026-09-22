#pragma once

// Private UI Automation accessibility frontend of the Windows backend
// (M4-02, extends DEC-017 decision 4): UIA tree walks produce budgeted
// SemanticSnapshots and semantic element actions resolve ElementTargets
// against the snapshot's runtime-id registry. Every COM / UIA type stays
// inside the .cpp (RULE-01); the threading discipline mirrors Win32Backend
// (DEC-017 decision 3): one mutex serializes every call, providers stay
// synchronous and bounded, no threads are created (RULE-03), and the
// calling context is an Executor blocking worker (EXEC-04).
//
// COM model (DEC-017 decision 4, the call-shaped UIA client usage): every
// provider method opens its own COINIT_MULTITHREADED scope on the calling
// thread, and the first successful scope stays open as a process-lifetime
// MTA anchor — exactly one deliberate, bounded reference (the M2-03
// process-lifetime pool precedent, cap 1 here). The anchor keeps the
// process MTA alive between calls, which is what makes the reference
// registry's element pointers safe to hold across calls; without it,
// paired init/uninit on every call could close the last apartment in a gap
// and dangle the registry. A thread already bound to a different apartment
// (RPC_E_CHANGED_MODE) yields an honest io_error for that call — never a
// silent workaround; the anchor forms on the first call from a usable
// thread, before any registry entry exists.

#include <memory>
#include <mutex>
#include <string>

#include <mirage/desktop/accessibility_provider.hpp>

namespace mirage::platform::windows_backend {

class UiaBackend final : public mirage::desktop::AccessibilityProvider {
  public:
    /// Probes UI Automation availability (COM in the MTA + the
    /// CUIAutomation class). Returns null when UIA cannot be used at all
    /// (capability honesty, DEC-017): callers expose no accessibility
    /// provider.
    static std::unique_ptr<UiaBackend> open();

    ~UiaBackend() override;
    UiaBackend(const UiaBackend &) = delete;
    UiaBackend &operator=(const UiaBackend &) = delete;

    mirage::desktop::AccessibilityProvider *accessibility() { return this; }

    mirage::desktop::SnapshotOutcome
    semantic_snapshot(const std::string &window_id,
                      const mirage::desktop::SemanticSnapshotLimits &limits,
                      const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::ElementActionOutcome
    activate_element(const mirage::desktop::ElementTarget &target,
                     const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::ElementActionOutcome
    set_text(const mirage::desktop::ElementTarget &target, const std::string &text,
             const mirage::desktop::InputLimits &limits,
             const mirage::desktop::CancelToken &cancel) override;

  private:
    UiaBackend() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mutex_;
};

} // namespace mirage::platform::windows_backend
