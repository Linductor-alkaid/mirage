// M7-10 Windows tiered screen capture engine (DEC-086) against the session's
// real interactive desktop: every tier is driven explicitly on the same
// two-tone fixture window the win32_backend_test uses, so the DXGI Desktop
// Duplication, Windows.Graphics.Capture and GDI BitBlt implementations each
// prove their pixel output, not just that "a capture succeeded". This is a
// whitebox test: it includes the private engine header, because tier
// identity is an implementation choice the provider contract deliberately
// hides.
//
// The process declares per-monitor DPI awareness first: the GPU tiers are
// locked to physical pixels, and the DEC-086 Auto chain only engages them
// when the provider and physical spaces agree — an aware process makes that
// the case here regardless of the host's display scale, so this test always
// exercises the real GPU tiers (an unaware process on a scaled desktop
// degrades to BitBlt by design; that behavior belongs to the provider
// contract and is covered by win32_backend_test).

#include "../support/test.hpp"

#include <mirage/platform/host_environment.hpp>

#include "screen_capture_backend.hpp"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::CaptureLimits;
using mirage::desktop::ImageFormat;
using mirage::desktop::WindowGeometry;
using mirage::platform::windows_backend::ScreenCaptureEngine;

constexpr wchar_t kWindowClassName[] = L"MirageWin32CaptureTierTestClass";
constexpr int kFixtureWidth = 320;
constexpr int kFixtureHeight = 240;

int g_paint_count = 0;

LRESULT CALLBACK test_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT full{};
        GetClientRect(window, &full);
        const RECT left{full.left, full.top, full.right / 2, full.bottom};
        const RECT right{full.right / 2, full.top, full.right, full.bottom};
        const HBRUSH blue = CreateSolidBrush(RGB(10, 60, 200));
        FillRect(dc, &left, blue);
        DeleteObject(blue);
        const HBRUSH ivory = CreateSolidBrush(RGB(235, 235, 220));
        FillRect(dc, &right, ivory);
        DeleteObject(ivory);
        EndPaint(window, &paint);
        ++g_paint_count;
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void run_pump(DWORD milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    MSG message{};
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 15, QS_ALLINPUT);
    }
}

/// The TOPMOST two-tone fixture window (same discipline as
/// win32_backend_test): a capture of its rectangle must show both tones.
class FixtureWindow {
  public:
    FixtureWindow() {
        WNDCLASSEXW window_class{};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &test_wndproc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kWindowClassName;
        MIRAGE_CHECK(RegisterClassExW(&window_class) != 0);
        window_ =
            CreateWindowExW(WS_EX_TOPMOST, kWindowClassName, L"mirage-win32-capture-tier-test",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 120, 120, kFixtureWidth,
                            kFixtureHeight, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        MIRAGE_CHECK(window_ != nullptr);
        if (window_ != nullptr) {
            SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        run_pump(300);
        MIRAGE_CHECK(g_paint_count >= 1);
    }
    ~FixtureWindow() {
        if (window_ != nullptr) {
            DestroyWindow(window_);
        }
        UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    }
    FixtureWindow(const FixtureWindow &) = delete;
    FixtureWindow &operator=(const FixtureWindow &) = delete;

    HWND handle() const { return window_; }

    WindowGeometry rect() const {
        RECT bounds{};
        MIRAGE_CHECK(GetWindowRect(window_, &bounds) != 0);
        return WindowGeometry{bounds.left, bounds.top, bounds.right - bounds.left,
                              bounds.bottom - bounds.top};
    }

  private:
    HWND window_ = nullptr;
};

/// The product runtime hosts declare their awareness through the public
/// platform entry (M7-10), so this test does the same: the engine gate and
/// the physical-geometry assertions below then run under exactly the mode
/// the product runs under. Returns false only when the process awareness is
/// fixed to a non-physical mode — the caller turns that into a loud
/// failure, not a silent BitBlt-only run.
bool declare_dpi_awareness() {
    const auto mode = mirage::platform::declare_host_dpi_awareness();
    std::fprintf(stderr, "[host] dpi awareness mode %d\n", static_cast<int>(mode));
    return mode == mirage::platform::DpiAwareness::per_monitor_v2 ||
           mode == mirage::platform::DpiAwareness::prior_per_monitor ||
           mode == mirage::platform::DpiAwareness::system;
}

/// The two tones must both be visible inside the frame (interior sampling
/// with tolerant predicates, so DWM edge/shadow pixels at the window bounds
/// cannot flake the check): a saturated blue-ish region on the left half and
/// a bright warm region on the right half.
void check_two_tone_content(const mirage::desktop::ImageFrame &frame,
                            ScreenCaptureEngine::Tier tier) {
    const auto pixel_at = [&frame](int x, int y) {
        const std::size_t offset =
            frame.stride * static_cast<std::size_t>(y) + static_cast<std::size_t>(x) * 4u;
        return std::uint32_t{frame.pixels[offset]} |
               (std::uint32_t{frame.pixels[offset + 1]} << 8) |
               (std::uint32_t{frame.pixels[offset + 2]} << 16);
    };
    const int left_x = frame.width / 4;
    const int right_x = (frame.width * 3) / 4;
    const int mid_y = frame.height / 2;
    bool blue_seen = false;
    bool ivory_seen = false;
    for (int dy = -frame.height / 10; dy <= frame.height / 10 && !(blue_seen && ivory_seen); ++dy) {
        const int y = mid_y + dy;
        if (y < 0 || y >= frame.height) {
            continue;
        }
        const std::uint32_t left = pixel_at(left_x, y);
        const std::uint32_t right = pixel_at(right_x, y);
        const int left_b = static_cast<int>(left & 0xFFu);
        const int left_g = static_cast<int>((left >> 8) & 0xFFu);
        const int left_r = static_cast<int>((left >> 16) & 0xFFu);
        if (left_b > 120 && left_b > left_r + 40 && left_b > left_g + 40) {
            blue_seen = true;
        }
        const int right_b = static_cast<int>(right & 0xFFu);
        const int right_g = static_cast<int>((right >> 8) & 0xFFu);
        const int right_r = static_cast<int>((right >> 16) & 0xFFu);
        if (right_r > 160 && right_g > 160 && right_b > 120) {
            ivory_seen = true;
        }
    }
    MIRAGE_CHECK(blue_seen);
    MIRAGE_CHECK(ivory_seen);
    if (!blue_seen || !ivory_seen) {
        std::fprintf(stderr, "[tier %d] content probe missed: blue=%d ivory=%d frame %dx%d\n",
                     static_cast<int>(tier), blue_seen ? 1 : 0, ivory_seen ? 1 : 0, frame.width,
                     frame.height);
        // Center-of-half pixel dumps for the first missed rows (diagnostic
        // evidence only; the tolerant predicates above remain the contract).
        for (int y = frame.height / 4; y < frame.height; y += frame.height / 8) {
            const std::uint32_t left = pixel_at(frame.width / 4, y);
            const std::uint32_t right = pixel_at((frame.width * 3) / 4, y);
            std::fprintf(stderr, "  y=%d left=#%06X right=#%06X\n", y, left, right);
        }
    }
}

void check_frame_invariants(const mirage::desktop::ImageFrame &frame,
                            const WindowGeometry &request) {
    MIRAGE_CHECK(frame.format == ImageFormat::Bgra8);
    MIRAGE_CHECK(frame.width == request.width);
    MIRAGE_CHECK(frame.height == request.height);
    MIRAGE_CHECK(frame.stride == static_cast<std::size_t>(frame.width) * 4u);
    MIRAGE_CHECK(frame.pixels.size() == frame.stride * static_cast<std::size_t>(frame.height));
}

struct DisplayWitnessContext {
    ScreenCaptureEngine *engine = nullptr;
    int served = 0;
};

/// M7-10 multi-display witness: every real monitor's Auto capture must come
/// back at the physical geometry EnumDisplayMonitors reports in this aware
/// process — an unaware host would get the scaled-down size on scaled
/// outputs, and the DEC-086 gate would have kept the GPU tiers off. Content
/// on the other displays is arbitrary desktop pixels, so this asserts frame
/// invariants and the physical dimensions only; pixel content is the
/// fixture's job in exercise_tier.
BOOL CALLBACK witness_display(HMONITOR monitor, HDC, LPRECT, LPARAM lparam) {
    auto *context = reinterpret_cast<DisplayWitnessContext *>(lparam);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (::GetMonitorInfoW(monitor, &info) == 0) {
        return TRUE; // a monitor that vanished mid-walk is not an error
    }
    const WindowGeometry rect{info.rcMonitor.left, info.rcMonitor.top,
                              info.rcMonitor.right - info.rcMonitor.left,
                              info.rcMonitor.bottom - info.rcMonitor.top};
    std::fprintf(stderr, "[display %ls] physical (%d,%d) %dx%d primary=%d\n", info.szDevice, rect.x,
                 rect.y, rect.width, rect.height,
                 (info.dwFlags & MONITORINFOF_PRIMARY) != 0 ? 1 : 0);
    CaptureLimits budget;
    budget.max_bytes =
        static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height) * 4u;
    const auto shot =
        context->engine->capture_rect(rect, budget, CancelToken{}, ScreenCaptureEngine::Tier::Auto);
    MIRAGE_CHECK(shot.ok);
    if (!shot.ok) {
        std::fprintf(stderr, "[display %ls] auto capture failed: %s / %s\n", info.szDevice,
                     shot.error.code.c_str(), shot.error.message.c_str());
        return TRUE;
    }
    check_frame_invariants(shot.frame, rect);
    ++context->served;
    return TRUE;
}

void exercise_all_displays(ScreenCaptureEngine &engine) {
    DisplayWitnessContext context;
    context.engine = &engine;
    ::EnumDisplayMonitors(nullptr, nullptr, &witness_display, reinterpret_cast<LPARAM>(&context));
    MIRAGE_CHECK(context.served >= 1);
}

/// The tier's whole scenario set on the real desktop: pixel output over the
/// fixture, repeated calls (per-call sessions must not corrupt each other),
/// and the refusals that happen before any capture work. `display_rect` is
/// the containing monitor's own rectangle (GPU tiers are single-output; a
/// whole-virtual-screen request belongs to BitBlt by design).
void exercise_tier(ScreenCaptureEngine &engine, ScreenCaptureEngine::Tier tier,
                   const FixtureWindow &fixture, const WindowGeometry &display_rect) {
    const char *label = tier == ScreenCaptureEngine::Tier::Dxgi              ? "dxgi"
                        : tier == ScreenCaptureEngine::Tier::GraphicsCapture ? "graphics-capture"
                        : tier == ScreenCaptureEngine::Tier::BitBlt          ? "bitblt"
                                                                             : "auto";

    // Contract-level refusals happen before any platform work and are strict
    // for every tier.
    CaptureLimits zero_bytes;
    zero_bytes.max_bytes = 0;
    const auto too_large =
        engine.capture_rect(WindowGeometry{0, 0, 16, 16}, zero_bytes, CancelToken{}, tier);
    MIRAGE_CHECK(!too_large.ok);
    MIRAGE_CHECK(too_large.error.code == "capture_too_large");

    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled =
        engine.capture_rect(WindowGeometry{0, 0, 16, 16}, CaptureLimits{}, cancel, tier);
    MIRAGE_CHECK(cancelled.cancelled);
    MIRAGE_CHECK(cancelled.error.code == "cancelled");

    const WindowGeometry degenerate{0, 0, 0, 10};
    MIRAGE_CHECK(engine.capture_rect(degenerate, CaptureLimits{}, CancelToken{}, tier).error.code ==
                 "invalid_argument");

    // Pixel scenarios run against the real desktop. The forced GPU tiers are
    // gated on an availability probe first: a desktop where the platform
    // refuses the tier (occupied duplication, indirect-display driver, no
    // capture runtime) skips the tier's pixel assertions with a loud note and
    // the recorded refusal — the DEC-017 decision 9 skip discipline, never a
    // silent pass. The Auto chain and the BitBlt tier must produce correct
    // pixels on every interactive desktop, so they stay strict.
    const WindowGeometry window_rect = fixture.rect();
    CaptureLimits budget;
    budget.max_bytes = static_cast<std::size_t>(window_rect.width) *
                       static_cast<std::size_t>(window_rect.height) * 4u;
    const auto availability = engine.capture_rect(window_rect, budget, CancelToken{}, tier);
    if (!availability.ok) {
        const bool gpu_tier = tier == ScreenCaptureEngine::Tier::Dxgi ||
                              tier == ScreenCaptureEngine::Tier::GraphicsCapture;
        if (gpu_tier) {
            std::fprintf(stderr,
                         "[tier %d] UNAVAILABLE on this desktop, pixel scenarios skipped with "
                         "recorded condition: %s / %s\n",
                         static_cast<int>(tier), availability.error.code.c_str(),
                         availability.error.message.c_str());
            return;
        }
    }
    MIRAGE_CHECK(availability.ok);
    if (!availability.ok) {
        std::fprintf(stderr, "[tier %d] capture_rect failed: %s / %s\n", static_cast<int>(tier),
                     availability.error.code.c_str(), availability.error.message.c_str());
    }

    for (int round = 0; round < 3; ++round) {
        run_pump(30); // keep the fixture painted between rounds
        const auto shot = engine.capture_rect(window_rect, budget, CancelToken{}, tier);
        MIRAGE_CHECK(shot.ok);
        if (!shot.ok) {
            std::fprintf(stderr, "[tier %d] capture_rect failed: %s / %s\n", static_cast<int>(tier),
                         shot.error.code.c_str(), shot.error.message.c_str());
            continue;
        }
        check_frame_invariants(shot.frame, window_rect);
        check_two_tone_content(shot.frame, tier);
    }

    // Whole-monitor capture inside the tier: dimensions match the monitor's
    // (aware) geometry, and the fixture's two tones must appear at the
    // window's absolute position inside the frame — this is the crop-origin
    // witness that a window-rect capture alone cannot provide.
    CaptureLimits display_budget;
    display_budget.max_bytes = static_cast<std::size_t>(display_rect.width) *
                               static_cast<std::size_t>(display_rect.height) * 4u;
    const auto display_shot =
        engine.capture_rect(display_rect, display_budget, CancelToken{}, tier);
    MIRAGE_CHECK(display_shot.ok);
    if (!display_shot.ok) {
        std::fprintf(stderr, "[tier %d] display capture failed: %s / %s\n", static_cast<int>(tier),
                     display_shot.error.code.c_str(), display_shot.error.message.c_str());
    } else {
        check_frame_invariants(display_shot.frame, display_rect);
        const int wx = window_rect.x - display_rect.x;
        const int wy = window_rect.y - display_rect.y;
        const auto at = [&display_shot](int x, int y) {
            const std::size_t offset = display_shot.frame.stride * static_cast<std::size_t>(y) +
                                       static_cast<std::size_t>(x) * 4u;
            return (std::uint32_t{display_shot.frame.pixels[offset + 2]} << 16) |
                   (std::uint32_t{display_shot.frame.pixels[offset + 1]} << 8) |
                   std::uint32_t{display_shot.frame.pixels[offset]};
        };
        const std::uint32_t blue_probe =
            at(wx + window_rect.width / 4, wy + window_rect.height - 60);
        const std::uint32_t ivory_probe =
            at(wx + (window_rect.width * 3) / 4, wy + window_rect.height - 60);
        std::fprintf(stderr, "[tier %d] display probes: blue=#%06X ivory=#%06X (frame %dx%d)\n",
                     static_cast<int>(tier), blue_probe, ivory_probe, display_shot.frame.width,
                     display_shot.frame.height);
        MIRAGE_CHECK((blue_probe & 0xFFu) > 120); // B channel dominates: fixture blue
        MIRAGE_CHECK(((ivory_probe >> 16) & 0xFFu) > 160 &&
                     ((ivory_probe >> 8) & 0xFFu) > 160); // warm bright: fixture ivory
    }

    std::fprintf(stderr, "[tier %s] scenarios complete\n", label);
}

} // namespace

int main() {
    MIRAGE_CHECK(declare_dpi_awareness());

    // The aware virtual-screen geometry is the coordinate space for the
    // whole process from here on; the GPU tiers exercise the PRIMARY
    // monitor (single-output by design).
    const WindowGeometry virtual_screen{
        ::GetSystemMetrics(SM_XVIRTUALSCREEN), ::GetSystemMetrics(SM_YVIRTUALSCREEN),
        ::GetSystemMetrics(SM_CXVIRTUALSCREEN), ::GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    MIRAGE_CHECK(virtual_screen.width > 0 && virtual_screen.height > 0);
    const POINT primary_point{0, 0};
    const HMONITOR primary_monitor = ::MonitorFromPoint(primary_point, MONITOR_DEFAULTTOPRIMARY);
    MIRAGE_CHECK(primary_monitor != nullptr);
    MONITORINFO primary_info{};
    primary_info.cbSize = sizeof(primary_info);
    MIRAGE_CHECK(::GetMonitorInfoW(primary_monitor, &primary_info) != 0);
    const WindowGeometry primary_rect{primary_info.rcMonitor.left, primary_info.rcMonitor.top,
                                      primary_info.rcMonitor.right - primary_info.rcMonitor.left,
                                      primary_info.rcMonitor.bottom - primary_info.rcMonitor.top};
    std::fprintf(stderr, "virtual screen: (%d,%d) %dx%d; primary: (%d,%d) %dx%d\n",
                 virtual_screen.x, virtual_screen.y, virtual_screen.width, virtual_screen.height,
                 primary_rect.x, primary_rect.y, primary_rect.width, primary_rect.height);

    FixtureWindow fixture;
    const WindowGeometry fixture_rect = fixture.rect();
    std::fprintf(stderr, "fixture window: (%d,%d) %dx%d\n", fixture_rect.x, fixture_rect.y,
                 fixture_rect.width, fixture_rect.height);
    MIRAGE_CHECK(fixture_rect.x >= primary_rect.x && fixture_rect.y >= primary_rect.y &&
                 fixture_rect.x + fixture_rect.width <= primary_rect.x + primary_rect.width &&
                 fixture_rect.y + fixture_rect.height <= primary_rect.y + primary_rect.height);

    const auto engine = ScreenCaptureEngine::create();
    MIRAGE_CHECK(engine != nullptr);
    if (engine == nullptr) {
        return mirage::testing::finish("win32_capture_tier_test");
    }

    // The Auto chain in an aware process takes the DEC-086 order end to end;
    // the forced tiers pin each implementation for its own evidence.
    exercise_tier(*engine, ScreenCaptureEngine::Tier::Auto, fixture, primary_rect);
    exercise_tier(*engine, ScreenCaptureEngine::Tier::Dxgi, fixture, primary_rect);
    exercise_tier(*engine, ScreenCaptureEngine::Tier::GraphicsCapture, fixture, primary_rect);
    exercise_tier(*engine, ScreenCaptureEngine::Tier::BitBlt, fixture, primary_rect);

    // M7-10 multi-display witness: every attached monitor must be served at
    // its physical geometry through the Auto chain (mixed-DPI desktops are
    // the point; the per-display evidence is on stderr).
    exercise_all_displays(*engine);

    // A forced GPU tier over a rectangle reaching past the containing
    // monitor's bounds has no single-output representation and must refuse
    // instead of silently capturing something else.
    WindowGeometry spanning{virtual_screen.x + virtual_screen.width / 2,
                            virtual_screen.y + virtual_screen.height / 2,
                            virtual_screen.width, // extends past the virtual screen edge
                            virtual_screen.height / 2};
    CaptureLimits spanning_budget;
    spanning_budget.max_bytes =
        static_cast<std::size_t>(spanning.width) * static_cast<std::size_t>(spanning.height) * 4u;
    const auto refused_dxgi = engine->capture_rect(spanning, spanning_budget, CancelToken{},
                                                   ScreenCaptureEngine::Tier::Dxgi);
    MIRAGE_CHECK(!refused_dxgi.ok);
    MIRAGE_CHECK(refused_dxgi.error.code == "invalid_argument");
    if (refused_dxgi.error.code != "invalid_argument") {
        std::fprintf(stderr, "[spanning] unexpected refusal: %s / %s\n",
                     refused_dxgi.error.code.c_str(), refused_dxgi.error.message.c_str());
    }
    // The Auto chain answers the same rectangle with the BitBlt rung, which
    // composes across the virtual screen honestly.
    const auto auto_spanning = engine->capture_rect(spanning, spanning_budget, CancelToken{},
                                                    ScreenCaptureEngine::Tier::Auto);
    MIRAGE_CHECK(auto_spanning.ok);
    if (auto_spanning.ok) {
        check_frame_invariants(auto_spanning.frame, spanning);
    }

    return mirage::testing::finish("win32_capture_tier_test");
}
