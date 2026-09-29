// M5-09 Desktop Overlay platform carrier verification (independent
// verification pass, DEC-029). Exercises the real X11 shape carrier against
// a private headless Xvfb (the DEC-015 test topology): capability honesty
// on a dead display, the override-redirect surface's mapping and bounding
// shape, click-through everywhere but the confirm entry (verified by real
// XTest clicks against the server-side input shape), click delivery for
// Approve/Deny, clean stop with surface teardown and re-run, and the
// wakeup contract before/during/after run().
//
// Harness: the carrier runs on a test-owned pump thread (the same topology
// the service's blocking worker uses); every assertion happens on the main
// thread over a second X connection. No assertion work happens on the pump.

#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"

#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/shape.h>

namespace {

namespace desktop = mirage::desktop;
using mirage::platform::linux_backend::open_overlay_carrier;
using mirage::testing::XvfbDisplay;

constexpr auto kSettleBudget = std::chrono::seconds{10};
constexpr auto kQuietWindow = std::chrono::milliseconds{700};

template <typename Predicate> bool wait_for(Predicate &&predicate, std::chrono::milliseconds step) {
    const auto deadline = std::chrono::steady_clock::now() + step;
    for (;;) {
        if (predicate()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

/// Second X connection for server-side assertions (window tree, shapes,
/// XTest injection). Everything the carrier draws is verified here, never
/// from the carrier's own state.
class TestConnection {
  public:
    explicit TestConnection(const std::string &display_name)
        : display_(XOpenDisplay(display_name.c_str())) {}

    ~TestConnection() {
        if (display_ != nullptr) {
            XCloseDisplay(display_);
        }
    }
    TestConnection(const TestConnection &) = delete;
    TestConnection &operator=(const TestConnection &) = delete;

    bool ok() const { return display_ != nullptr; }
    Display *get() { return display_; }

    /// The overlay's override-redirect root child at the carrier's screen
    /// size (the private Xvfb has no other windows of this shape).
    Window find_overlay_window(int width, int height) {
        if (display_ == nullptr) {
            return None;
        }
        Window root = DefaultRootWindow(display_);
        Window parent = None;
        Window *children = nullptr;
        unsigned int count = 0;
        if (XQueryTree(display_, root, &root, &parent, &children, &count) == 0) {
            return None;
        }
        Window found = None;
        for (unsigned int index = 0; index < count; ++index) {
            XWindowAttributes attributes{};
            if (XGetWindowAttributes(display_, children[index], &attributes) == 0) {
                continue;
            }
            if (attributes.override_redirect == True && attributes.width == width &&
                attributes.height == height) {
                found = children[index];
                break;
            }
        }
        if (children != nullptr) {
            XFree(children);
        }
        return found;
    }

    bool is_mapped(Window window) {
        if (display_ == nullptr || window == None) {
            return false;
        }
        XWindowAttributes attributes{};
        if (XGetWindowAttributes(display_, window, &attributes) == 0) {
            return false;
        }
        return attributes.map_state == IsViewable;
    }

    bool bounding_shaped(Window window) {
        if (display_ == nullptr || window == None) {
            return false;
        }
        Bool bounding_shaped = False;
        Bool clip_shaped = False;
        int bx = 0, by = 0;
        unsigned int bw = 0, bh = 0;
        int cx = 0, cy = 0;
        unsigned int cw = 0, ch = 0;
        if (XShapeQueryExtents(display_, window, &bounding_shaped, &bx, &by, &bw, &bh, &clip_shaped,
                               &cx, &cy, &cw, &ch) == 0) {
            return false;
        }
        return bounding_shaped == True;
    }

    /// The server-side input shape rectangles (empty = fully click-through).
    std::vector<std::pair<int, int>> input_rect_centers(Window window) {
        std::vector<std::pair<int, int>> centers;
        if (display_ == nullptr || window == None) {
            return centers;
        }
        int count = 0;
        int ordering = 0;
        XRectangle *rectangles =
            XShapeGetRectangles(display_, window, ShapeInput, &count, &ordering);
        for (int index = 0; index < count; ++index) {
            centers.emplace_back(rectangles[index].x + rectangles[index].width / 2,
                                 rectangles[index].y + rectangles[index].height / 2);
        }
        if (rectangles != nullptr) {
            XFree(rectangles);
        }
        return centers;
    }

    /// One real click at a screen position through XTest (the honest path:
    /// the server routes it through the overlay's input shape).
    void inject_click(int x, int y) {
        if (display_ == nullptr) {
            return;
        }
        const int screen = DefaultScreen(display_);
        XTestFakeMotionEvent(display_, screen, x, y, CurrentTime);
        XTestFakeButtonEvent(display_, Button1, True, CurrentTime);
        XTestFakeButtonEvent(display_, Button1, False, CurrentTime);
        XSync(display_, False);
    }

  private:
    Display *display_ = nullptr;
};

/// Flow-side frame publisher (latest frame wins; the carrier polls it from
/// its pump thread through the carrier context).
class FrameSource {
  public:
    bool load(desktop::OverlaySurfaceFrame &frame) {
        const std::lock_guard lock(mutex_);
        if (!next_.has_value()) {
            return false;
        }
        frame = std::move(*next_);
        next_.reset();
        return true;
    }

    void publish(desktop::OverlaySurfaceFrame frame) {
        const std::lock_guard lock(mutex_);
        next_ = std::move(frame);
    }

  private:
    std::mutex mutex_;
    std::optional<desktop::OverlaySurfaceFrame> next_;
};

/// Runs the carrier on a test-owned thread (the service's blocking-worker
/// topology) and records what the surface delivered.
class Pump {
  public:
    void start(desktop::OverlayCarrier &carrier, FrameSource &source) {
        thread_ = std::thread([this, &carrier, &source] {
            desktop::OverlayCarrierContext context;
            context.load_frame = [&source](desktop::OverlaySurfaceFrame &frame) {
                return source.load(frame);
            };
            context.on_click = [this](const desktop::OverlayClick &click) {
                const std::lock_guard lock(mutex_);
                clicks_.push_back(click);
            };
            report_ = carrier.run(context, [this] { return stop_.load(); });
        });
    }

    void request_stop() { stop_.store(true); }
    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::vector<desktop::OverlayClick> clicks() const {
        const std::lock_guard lock(mutex_);
        return clicks_;
    }

    bool has_click(const std::string &request_id, bool approved) const {
        const std::lock_guard lock(mutex_);
        for (const desktop::OverlayClick &click : clicks_) {
            if (click.request_id == request_id && click.approved == approved) {
                return true;
            }
        }
        return false;
    }

    const desktop::OverlayCarrier::RunReport &report() const { return report_; }

  private:
    std::thread thread_;
    std::atomic<bool> stop_{false};
    desktop::OverlayCarrier::RunReport report_{};
    mutable std::mutex mutex_;
    std::vector<desktop::OverlayClick> clicks_;
};

void scenario_open_fails_closed_without_a_server() {
    // A display with no server must yield null (capability honesty,
    // DEC-029 decision 4) — never a degraded carrier.
    const auto carrier = open_overlay_carrier(":1900");
    MIRAGE_CHECK(carrier == nullptr);
}

void scenario_surface_presents_and_clicks_through(XvfbDisplay &server, TestConnection &client) {
    const auto carrier = open_overlay_carrier(server.display_name());
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }

    FrameSource source;
    Pump pump;
    pump.start(*carrier, source);
    MIRAGE_CHECK(
        wait_for([&] { return client.find_overlay_window(640, 480) != None; }, kSettleBudget));
    const Window overlay = client.find_overlay_window(640, 480);

    // A visible frame maps the override-redirect surface with a non-empty
    // bounding shape.
    desktop::OverlaySurfaceFrame frame;
    frame.visible = true;
    frame.hint = "Mirage: activating \"Save\"";
    frame.highlights.push_back({{100, 100, 200, 150}, "Save"});
    source.publish(std::move(frame));
    MIRAGE_CHECK(wait_for([&] { return client.is_mapped(overlay); }, kSettleBudget));
    MIRAGE_CHECK(wait_for([&] { return client.bounding_shaped(overlay); }, kSettleBudget));

    // Without a confirmation the input shape is empty: a real click at the
    // screen center passes through (no click reaches the carrier within the
    // quiet window).
    client.inject_click(320, 240);
    std::this_thread::sleep_for(kQuietWindow);
    MIRAGE_CHECK(pump.clicks().empty());

    // visible=false unmaps the surface.
    desktop::OverlaySurfaceFrame dark;
    dark.visible = false;
    source.publish(std::move(dark));
    MIRAGE_CHECK(wait_for([&] { return !client.is_mapped(overlay); }, kSettleBudget));

    // wakeup() during a live run is safe; stop is clean and the surface is
    // torn down on the pump thread.
    carrier->wakeup();
    pump.request_stop();
    pump.join();
    MIRAGE_CHECK(pump.report().clean);
    MIRAGE_CHECK(wait_for([&] { return client.find_overlay_window(640, 480) == None; },
                          std::chrono::seconds{5}));
    MIRAGE_CHECK(pump.clicks().empty());
}

void scenario_confirmation_entry_delivers_clicks(XvfbDisplay &server, TestConnection &client) {
    // A fresh carrier per run: the surface lifecycle is per-run (created
    // inside run(), destroyed before it returns).
    const auto carrier = open_overlay_carrier(server.display_name());
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }

    FrameSource source;
    Pump pump;
    carrier->wakeup(); // safe before run()
    pump.start(*carrier, source);
    MIRAGE_CHECK(
        wait_for([&] { return client.find_overlay_window(640, 480) != None; }, kSettleBudget));

    desktop::OverlaySurfaceFrame frame;
    frame.visible = true;
    frame.hint = "Mirage: awaiting confirmation";
    frame.confirmation =
        desktop::OverlayConfirmation{"req-1", "filesystem.read", "/tmp/goal.txt", 30000};
    source.publish(std::move(frame));

    const Window overlay = client.find_overlay_window(640, 480);
    MIRAGE_CHECK(wait_for([&] { return client.is_mapped(overlay); }, kSettleBudget));
    // The confirm entry is the only input shape on the surface.
    MIRAGE_CHECK(
        wait_for([&] { return client.input_rect_centers(overlay).size() == 2; }, kSettleBudget));

    // Real clicks on both button rects: the carrier routes them as
    // OverlayClick payloads (first-response-wins lives service-side; the
    // carrier delivers both verdicts faithfully).
    const auto centers = client.input_rect_centers(overlay);
    client.inject_click(centers[0].first, centers[0].second);
    MIRAGE_CHECK(wait_for([&] { return pump.clicks().size() == 1; }, kSettleBudget));
    client.inject_click(centers[1].first, centers[1].second);
    MIRAGE_CHECK(wait_for([&] { return pump.clicks().size() == 2; }, kSettleBudget));

    MIRAGE_CHECK(pump.has_click("req-1", true));
    MIRAGE_CHECK(pump.has_click("req-1", false));
    MIRAGE_CHECK(pump.clicks().size() == 2);

    // Clearing the confirmation takes the entry back off the surface.
    desktop::OverlaySurfaceFrame settled;
    settled.visible = true;
    settled.hint = "Mirage: awaiting confirmation";
    source.publish(std::move(settled));
    MIRAGE_CHECK(
        wait_for([&] { return client.input_rect_centers(overlay).empty(); }, kSettleBudget));

    pump.request_stop();
    pump.join();
    MIRAGE_CHECK(pump.report().clean);
    carrier->wakeup(); // safe after run()
}

void scenario_stop_with_a_dark_surface_is_clean(XvfbDisplay &server) {
    const auto carrier = open_overlay_carrier(server.display_name());
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }

    // A run that never sees a frame still stops cleanly (the pump's idle
    // path), and wakeup() from the owner side never breaks it.
    FrameSource source;
    Pump pump;
    pump.start(*carrier, source);
    for (int index = 0; index < 3; ++index) {
        carrier->wakeup();
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    pump.request_stop();
    pump.join();
    MIRAGE_CHECK(pump.report().clean);
    MIRAGE_CHECK(pump.clicks().empty());
}

} // namespace

int main() {
    const std::string xvfb = mirage::testing::find_xvfb();
    if (xvfb.empty()) {
        // Loud failure, never a skip (DOD-03): the M5-09 carrier claims are
        // only honest when exercised against a real X server.
        std::fprintf(
            stderr,
            "Xvfb not found: install xvfb or set MIRAGE_XVFB "
            "(user-prefix extraction documented in docs/plans/m2-desktop-environment.md)\n");
        return 1;
    }
    try {
        XvfbDisplay server(xvfb, "640x480x24");
        TestConnection client(server.display_name());
        if (!client.ok()) {
            std::fprintf(stderr, "cannot open test display %s\n", server.display_name().c_str());
            return 1;
        }
        scenario_open_fails_closed_without_a_server();
        scenario_surface_presents_and_clicks_through(server, client);
        scenario_confirmation_entry_delivers_clicks(server, client);
        scenario_stop_with_a_dark_surface_is_clean(server);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Xvfb fixture failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("overlay_carrier_test");
}
