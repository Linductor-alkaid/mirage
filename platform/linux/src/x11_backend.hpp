#pragma once

// Private X11/XWayland frontend of the Linux backend (M2-02, DEC-015).
// Lives in src/ so no X11 header ever appears in a public include path
// (RULE-01): this header forward-declares nothing platform-specific and the
// implementation hides every X type inside the .cpp.
//
// Threading discipline (DEC-015): Xlib is not thread-safe, so one Display
// connection is guarded by a mutex and every provider method holds it for
// the duration of its bounded, synchronous work. Providers never create
// threads (RULE-03); the calling context is an Executor blocking worker
// (EXEC-04) chosen by the consumer.

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <mirage/desktop/clipboard_provider.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/screen_provider.hpp>
#include <mirage/desktop/window_provider.hpp>

namespace mirage::platform::linux_backend {

class X11Backend final : public mirage::desktop::WindowProvider,
                         public mirage::desktop::ScreenProvider,
                         public mirage::desktop::InputProvider,
                         public mirage::desktop::ClipboardProvider {
  public:
    /// Opens `display_name` (empty means the DISPLAY environment variable).
    /// Returns null when the X connection cannot be established — e.g. a
    /// Wayland-native session without XWayland — so callers expose no
    /// X11-backed provider at all (capability honesty, DEC-015).
    static std::unique_ptr<X11Backend> open(const std::string &display_name);

    ~X11Backend() override;
    X11Backend(const X11Backend &) = delete;
    X11Backend &operator=(const X11Backend &) = delete;

    mirage::desktop::WindowProvider *window() { return this; }
    mirage::desktop::ScreenProvider *screen() { return this; }
    mirage::desktop::InputProvider *input() { return this; }
    mirage::desktop::ClipboardProvider *clipboard() { return this; }

    mirage::desktop::WindowListOutcome
    list_windows(const mirage::desktop::WindowListLimits &limits,
                 const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::WindowQueryOutcome
    front_window(const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::WindowActionOutcome
    activate(const std::string &window_id, const mirage::desktop::CancelToken &cancel) override;

    mirage::desktop::DisplayListOutcome
    list_displays(const mirage::desktop::DisplayListLimits &limits,
                  const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::CaptureOutcome
    capture_display(const std::string &display_id, const mirage::desktop::CaptureLimits &limits,
                    const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::CaptureOutcome
    capture_window(const std::string &window_id, const mirage::desktop::CaptureLimits &limits,
                   const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::CaptureOutcome
    capture_roi(const mirage::desktop::WindowGeometry &roi,
                const mirage::desktop::CaptureLimits &limits,
                const mirage::desktop::CancelToken &cancel) override;

    mirage::desktop::InputOutcome inject_key(const mirage::desktop::KeySym &key, bool pressed,
                                             const mirage::desktop::InputLimits &limits,
                                             const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::InputOutcome type_text(const std::string &text,
                                            const mirage::desktop::InputLimits &limits,
                                            const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::InputOutcome pointer_move(std::int32_t x, std::int32_t y,
                                               const mirage::desktop::InputLimits &limits,
                                               const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::InputOutcome
    pointer_button(const mirage::desktop::MouseButton &button, bool pressed,
                   const mirage::desktop::InputLimits &limits,
                   const mirage::desktop::CancelToken &cancel) override;

    mirage::desktop::ClipboardReadOutcome
    read_text(const mirage::desktop::ClipboardReadLimits &limits,
              const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::ClipboardWriteOutcome
    write_text(const std::string &text, const mirage::desktop::ClipboardWriteLimits &limits,
               const mirage::desktop::CancelToken &cancel) override;

    /// Opaque X connection state; defined (with all X11 types) in the .cpp.
    /// Named from internal helper functions, which is why it is public.
    struct XConnection;

  private:
    X11Backend() = default;

    /// RandR monitor table (or the root geometry as one stable "screen"
    /// display on RandR-less servers). Caller must hold `mutex_`.
    std::vector<mirage::desktop::DisplayInfo> query_displays_locked();

    /// Answers pending clipboard events for the selection we own (requests,
    /// ownership loss, incremental transfer rounds). Called on entry of every
    /// provider method and inside the clipboard paths, so the serving latency
    /// is bounded by one provider call (DEC-015 amendment, M2-04).
    void pump_clipboard_locked();

    XConnection &xconn() { return *connection_; }

    std::unique_ptr<XConnection> connection_;
    std::mutex mutex_;
};

} // namespace mirage::platform::linux_backend
