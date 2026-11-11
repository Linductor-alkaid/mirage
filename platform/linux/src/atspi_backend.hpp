#pragma once

// Private AT-SPI2 accessibility frontend of the Linux backend (M2-03,
// extends DEC-015): libatspi tree walks produce budgeted SemanticSnapshots
// and semantic element actions resolve ElementTargets against the snapshot
// reference registry or the live tree. All AT-SPI / glib types stay inside
// the .cpp (RULE-01); the threading discipline mirrors X11Backend — one
// mutex serializes every libatspi call, providers stay synchronous and
// bounded, no threads are created (RULE-03).

#include <memory>
#include <mutex>
#include <string>

#include <mirage/desktop/accessibility_provider.hpp>
#include <mirage/desktop/window_provider.hpp>

namespace mirage::platform::linux_backend {

class AtspiBackend final : public mirage::desktop::AccessibilityProvider {
  public:
    /// Initializes libatspi and binds the window-title mapping source.
    /// Returns null when the accessibility bus is unavailable (capability
    /// honesty, DEC-015). `windows` may be null: window-id mapping then
    /// fails closed ("unsupported_window") while live-tree actions keep
    /// working.
    static std::unique_ptr<AtspiBackend> open(mirage::desktop::WindowProvider *windows);

    /// Wayland window enumeration over the accessibility tree (M7-18):
    /// top-level frames and dialogs with screen-coordinate extents back the
    /// window-geometry approximation behind Portal window captures. Returns
    /// null without an accessibility bus (capability honesty, DEC-015).
    /// Activation is not expressible over AT-SPI v1 and fails closed.
    static std::unique_ptr<mirage::desktop::WindowProvider> open_window_provider();

    ~AtspiBackend() override;
    AtspiBackend(const AtspiBackend &) = delete;
    AtspiBackend &operator=(const AtspiBackend &) = delete;

    mirage::desktop::AccessibilityProvider *accessibility() { return this; }

    bool scoped_references_supported() const override { return true; }
    bool scoped_snapshots_supported() const override { return true; }
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
    AtspiBackend() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mutex_;
};

} // namespace mirage::platform::linux_backend
