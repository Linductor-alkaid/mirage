#include "../../platform/common/src/frontend_activation.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <kairo/executor.hpp>

// Xlib defines None; consume the Executor public headers before these macros.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <vector>

namespace {
void mark(Display *display, Window window, unsigned long pid, unsigned long protocol = 1) {
    for (const auto &[name, value] :
         {std::pair{"_NET_WM_PID", pid}, std::pair{"_MIRAGE_PET_SURFACE", protocol}})
        XChangeProperty(display, window, XInternAtom(display, name, False), XA_CARDINAL, 32,
                        PropModeReplace, reinterpret_cast<const unsigned char *>(&value), 1);
    XSync(display, False);
}
unsigned long pixel(Display *display, int x, int y) {
    XSync(display, False);
    auto *image = XGetImage(display, DefaultRootWindow(display), x, y, 1, 1, AllPlanes, ZPixmap);
    MIRAGE_CHECK(image);
    const auto value = image ? XGetPixel(image, 0, 0) : 0;
    if (image)
        XDestroyImage(image);
    return value;
}
Window hit(Display *display, int x, int y) {
    const auto root = DefaultRootWindow(display);
    XWarpPointer(display, None, root, 0, 0, 0, 0, x, y);
    XSync(display, False);
    Window returned = None, child = None;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned int mask = 0;
    MIRAGE_CHECK(XQueryPointer(display, root, &returned, &child, &rx, &ry, &wx, &wy, &mask));
    return child;
}
} // namespace
int main(int argc, char **argv) {
    using namespace mirage;
    // Native-process fixture mode: no installed product API exposes arbitrary
    // PIDs. The external private-session driver owns this process and deadline.
    if (argc == 3 && std::string_view(argv[1]) == "--probe-pid") {
        const auto *session = std::getenv("MIRAGE_ACCEPTANCE_PRIVATE_SESSION");
        if (!session || std::string_view(session) != "1")
            return 2;
        auto control = platform::detail::make_pet_excluder(std::stoll(argv[2]));
        auto lease = control->acquire({});
        if (!lease.lease || !lease.error.code.empty()) {
            std::cerr << lease.error.code << ": " << lease.error.message << '\n';
            return 3;
        }
        std::cout << "hidden" << std::endl;
        struct pollfd input {
            STDIN_FILENO, POLLIN, 0
        };
        const bool released = ::poll(&input, 1, 12000) > 0 && (input.revents & POLLIN);
        const auto verified = lease.lease->verify({});
        const auto restored = lease.lease->release();
        return released && verified.code.empty() && restored.code.empty() ? 0 : 4;
    }
    testing::XvfbDisplay server(testing::find_xvfb(), "800x600x24");
    ::setenv("DISPLAY", server.display_name().c_str(), 1);
    ::unsetenv("WAYLAND_DISPLAY");
    ::setenv("XDG_SESSION_TYPE", "x11", 1);
    Display *display = XOpenDisplay(nullptr);
    MIRAGE_CHECK(display);
    if (!display)
        return testing::finish("pet_exclusion_test");
    const auto root = DefaultRootWindow(display);
    const auto target = XCreateSimpleWindow(display, root, 0, 0, 400, 400, 0, 0, 0x224466);
    const auto pet = XCreateSimpleWindow(display, root, 40, 40, 264, 322, 0, 0, 0xaa3355);
    XMapWindow(display, target);
    XMapWindow(display, pet);
    const auto pid = static_cast<unsigned long>(::getpid());
    mark(display, pet, pid);
    auto control = platform::detail::make_pet_excluder(static_cast<std::int64_t>(pid));
    MIRAGE_CHECK(pixel(display, 50, 50) == 0xaa3355);   // head/bubble reserve
    MIRAGE_CHECK(pixel(display, 100, 300) == 0xaa3355); // character and shadow
    MIRAGE_CHECK(hit(display, 100, 300) == pet);
    Window before = None;
    int revert = 0;
    XSetInputFocus(display, target, RevertToParent, CurrentTime);
    XGetInputFocus(display, &before, &revert);
    auto first = control->acquire({});
    if (!first.error.code.empty())
        std::fprintf(stderr, "exclusion: %s: %s\n", first.error.code.c_str(),
                     first.error.message.c_str());
    MIRAGE_CHECK(first.error.code.empty() && first.lease);
    if (!first.lease)
        return testing::finish("pet_exclusion_test");
    MIRAGE_CHECK(first.lease->verify({}).code.empty());
    MIRAGE_CHECK(pixel(display, 50, 50) == 0x224466);
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x224466);
    MIRAGE_CHECK(hit(display, 100, 300) == target);
    // New content and geometry while hidden cannot make either area reappear.
    XSetWindowBackground(display, pet, 0x66bb33);
    XClearWindow(display, pet);
    XMoveResizeWindow(display, pet, 45, 40, 264, 330);
    XSync(display, False);
    MIRAGE_CHECK(first.lease->verify({}).code.empty());
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x224466);
    auto second = control->acquire({});
    MIRAGE_CHECK(second.error.code.empty() && second.lease);
    MIRAGE_CHECK(first.lease->release().code.empty());
    MIRAGE_CHECK(first.lease->release().code.empty());
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x224466);
    if (second.lease)
        MIRAGE_CHECK(second.lease->release().code.empty());
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x66bb33);
    Window after = None;
    XGetInputFocus(display, &after, &revert);
    MIRAGE_CHECK(before == after && after == target);
    desktop::CancelToken cancelled;
    cancelled.request_cancel();
    auto refused = control->acquire(cancelled);
    MIRAGE_CHECK(!refused.lease && refused.error.code == "cancelled");
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x66bb33);
    // Capacity is explicit, and refusal cannot restore another live lease.
    std::vector<std::unique_ptr<desktop::PresentationLease>> held;
    for (int index = 0; index < 16; ++index) {
        auto item = control->acquire({});
        MIRAGE_CHECK(item.lease && item.error.code.empty());
        held.push_back(std::move(item.lease));
    }
    refused = control->acquire({});
    MIRAGE_CHECK(!refused.lease && refused.error.message.find("capacity") != std::string::npos);
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x224466);
    held.clear();
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x66bb33);
    // PID property is forgeable; XRes must reject an incorrect server owner.
    mark(display, pet, pid + 1);
    auto forged = platform::detail::make_pet_excluder(static_cast<std::int64_t>(pid + 1));
    refused = forged->acquire({});
    MIRAGE_CHECK(!refused.lease && !refused.error.code.empty());
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x66bb33);
    mark(display, pet, pid);
    const auto duplicate = XCreateSimpleWindow(display, root, 500, 0, 100, 100, 0, 0, 0xff0000);
    mark(display, duplicate, pid);
    refused = control->acquire({});
    MIRAGE_CHECK(!refused.lease && !refused.error.code.empty());
    XDestroyWindow(display, duplicate);
    XSync(display, False);
    // Different Executor contexts share resource ownership, not a private queue.
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 2;
    config.queue_capacity = config.max_in_flight_tasks = 8;
    MIRAGE_CHECK(executor.initialize(config));
    auto acquired = executor.submit_auto([control] { return control->acquire({}); });
    auto concurrent = acquired.get();
    MIRAGE_CHECK(concurrent.lease && concurrent.error.code.empty());
    if (concurrent.lease) {
        auto verify = executor.submit_auto([&] { return concurrent.lease->verify({}); });
        MIRAGE_CHECK(verify.get().code.empty());
        auto release = executor.submit_auto([&] { return concurrent.lease->release(); });
        MIRAGE_CHECK(release.get().code.empty());
    }
    executor.shutdown(true);
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x66bb33);
    // An externally changed shape is an explicit failure, followed by cleanup.
    first = control->acquire({});
    MIRAGE_CHECK(first.lease);
    XShapeCombineMask(display, pet, ShapeBounding, 0, 0, None, ShapeSet);
    XSync(display, False);
    if (first.lease) {
        MIRAGE_CHECK(first.lease->verify({}).code == "pet_exclusion_failed");
        MIRAGE_CHECK(first.lease->release().code.empty());
    }
    // A changed protocol identity must not be mutated by cleanup.
    first = control->acquire({});
    MIRAGE_CHECK(first.lease);
    mark(display, pet, pid, 2);
    if (first.lease) {
        MIRAGE_CHECK(!first.lease->verify({}).code.empty());
        MIRAGE_CHECK(first.lease->release().code == "pet_restore_failed");
    }
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x224466);
    mark(display, pet, pid);
    XShapeCombineMask(display, pet, ShapeBounding, 0, 0, None, ShapeSet);
    XSync(display, False);
    // Preserve an original non-default shape rather than expanding the surface.
    XRectangle shape{0, 0, 120, 120};
    XShapeCombineRectangles(display, pet, ShapeBounding, 0, 0, &shape, 1, ShapeSet, Unsorted);
    XSync(display, False);
    first = control->acquire({});
    MIRAGE_CHECK(first.lease);
    first.lease.reset(); // RAII exception-style cleanup
    MIRAGE_CHECK(pixel(display, 50, 50) == 0x66bb33);
    MIRAGE_CHECK(pixel(display, 100, 300) == 0x224466);
    first = control->acquire({});
    MIRAGE_CHECK(first.lease);
    XDestroyWindow(display, pet);
    XSync(display, False);
    if (first.lease) {
        MIRAGE_CHECK(!first.lease->verify({}).code.empty());
        MIRAGE_CHECK(first.lease->release().code.empty());
    }
    ::setenv("XDG_SESSION_TYPE", "wayland", 1);
    refused = control->acquire({});
    MIRAGE_CHECK(!refused.lease && !refused.error.code.empty());
    ::setenv("XDG_SESSION_TYPE", "x11", 1);
    XDestroyWindow(display, target);
    XCloseDisplay(display);
    // Missing XRes cannot degrade to PID/title trust.
    testing::XvfbDisplay no_res(testing::find_xvfb(), "800x600x24", false);
    ::setenv("DISPLAY", no_res.display_name().c_str(), 1);
    refused = platform::detail::make_pet_excluder(pid)->acquire({});
    MIRAGE_CHECK(!refused.lease && !refused.error.code.empty());
    return testing::finish("pet_exclusion_test");
}
