#pragma once

// Private Win32 frontend of the Windows backend (M4-01, DEC-017). Lives in
// src/ so no Win32 header ever appears in a public include path (RULE-01):
// this header includes neither windows.h nor any other platform header and
// the implementation hides every Win32 type inside the .cpp.
//
// Threading discipline (DEC-017 decision 3): every provider method holds
// one mutex for the duration of its bounded, synchronous work. The M4-01
// API surface (EnumWindows / GetForegroundWindow / SetForegroundWindow,
// GDI BitBlt, SendInput, GetCursorPos, GetSystemMetrics) requires neither a
// message loop nor a window on the calling thread; providers never create
// threads (RULE-03) and the calling context is an Executor blocking worker
// (EXEC-04) chosen by the consumer. SendInput additionally requires the
// calling thread to sit in the interactive desktop session — a service
// context fails per call with "io_error" instead of silently no-oping.

#include <memory>
#include <mutex>
#include <string>

#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/screen_provider.hpp>
#include <mirage/desktop/window_provider.hpp>

namespace mirage::platform::windows_backend {

class Win32Backend final : public mirage::desktop::WindowProvider,
                           public mirage::desktop::ScreenProvider,
                           public mirage::desktop::InputProvider {
  public:
    /// Probes the interactive desktop. Returns null when no display is
    /// attached to the session (capability honesty, DEC-017): callers expose
    /// no Win32-backed provider at all.
    static std::unique_ptr<Win32Backend> open();

    ~Win32Backend() override;
    Win32Backend(const Win32Backend &) = delete;
    Win32Backend &operator=(const Win32Backend &) = delete;

    mirage::desktop::WindowProvider *window() { return this; }
    mirage::desktop::ScreenProvider *screen() { return this; }
    mirage::desktop::InputProvider *input() { return this; }

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
    mirage::desktop::PointerQueryOutcome
    pointer_position(const mirage::desktop::CancelToken &cancel) override;

  private:
    Win32Backend() = default;

    // Serializes every provider method (DEC-017 decision 3); GDI objects and
    // DCs are acquired per call inside the critical section.
    std::mutex mutex_;
};

} // namespace mirage::platform::windows_backend
