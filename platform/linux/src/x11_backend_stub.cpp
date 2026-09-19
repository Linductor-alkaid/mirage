// Link-time stub for machines without the X11 dev packages: the backend
// exists (so linux_desktop_environment.cpp compiles unchanged) but reports
// no capability, and no X11 header is touched. See platform/CMakeLists.txt.

#include "x11_backend.hpp"

#include <cstdlib>

namespace mirage::platform::linux_backend {

using mirage::desktop::CancelToken;
using mirage::desktop::CaptureLimits;
using mirage::desktop::CaptureOutcome;
using mirage::desktop::ClipboardReadLimits;
using mirage::desktop::ClipboardReadOutcome;
using mirage::desktop::ClipboardWriteLimits;
using mirage::desktop::ClipboardWriteOutcome;
using mirage::desktop::DisplayListLimits;
using mirage::desktop::DisplayListOutcome;
using mirage::desktop::InputLimits;
using mirage::desktop::InputOutcome;
using mirage::desktop::KeySym;
using mirage::desktop::MouseButton;
using mirage::desktop::WindowActionOutcome;
using mirage::desktop::WindowGeometry;
using mirage::desktop::WindowListLimits;
using mirage::desktop::WindowListOutcome;
using mirage::desktop::WindowQueryOutcome;

struct X11Backend::XConnection {};

X11Backend::~X11Backend() = default;

std::unique_ptr<X11Backend> X11Backend::open(const std::string &) {
    return nullptr; // fail closed: no X11 support in this build (DEC-015)
}

std::vector<mirage::desktop::DisplayInfo> X11Backend::query_displays_locked() { return {}; }

// The stub's open() never returns an instance, so no X11Backend object can
// ever exist and the overrides below are unreachable. They are defined only
// to complete the vtable (the out-of-line destructor above emits it), which
// is what previously broke linking of every TU that touches the environment
// destructor.

WindowListOutcome X11Backend::list_windows(const WindowListLimits &, const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

WindowQueryOutcome X11Backend::front_window(const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

WindowActionOutcome X11Backend::activate(const std::string &, const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

DisplayListOutcome X11Backend::list_displays(const DisplayListLimits &, const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

CaptureOutcome X11Backend::capture_display(const std::string &, const CaptureLimits &,
                                           const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

CaptureOutcome X11Backend::capture_window(const std::string &, const CaptureLimits &,
                                          const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

CaptureOutcome X11Backend::capture_roi(const WindowGeometry &, const CaptureLimits &,
                                       const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

InputOutcome X11Backend::inject_key(const KeySym &, bool, const InputLimits &,
                                    const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

InputOutcome X11Backend::type_text(const std::string &, const InputLimits &, const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

InputOutcome X11Backend::pointer_move(std::int32_t, std::int32_t, const InputLimits &,
                                      const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

InputOutcome X11Backend::pointer_button(const MouseButton &, bool, const InputLimits &,
                                        const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

ClipboardReadOutcome X11Backend::read_text(const ClipboardReadLimits &, const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

ClipboardWriteOutcome X11Backend::write_text(const std::string &, const ClipboardWriteLimits &,
                                             const CancelToken &) {
    std::abort(); // unreachable: open() is null in this build
}

} // namespace mirage::platform::linux_backend
