#include <mirage/platform/pet_window.hpp>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <eui/window.h>
#include <iostream>
#include <limits>
#include <vector>
#ifndef _WIN32
#include <csignal>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#endif

namespace mirage::platform::pet_window {
namespace {
GLFWwindow *observed_window = nullptr; // immutable while the UI is running
GLFWwindowiconifyfun previous_iconify = nullptr;
std::function<void(bool)> visibility_sink;   // UI-thread owned
std::function<void(Position)> position_sink; // UI-thread owned
std::atomic_bool hidden{false}, exit_relayed{false};
bool presented = true, visibility_failed = false; // UI-owned
bool bubble_below = false;                        // UI-owned; fixed during a drag
constexpr int bubble_reserve = 58;
constexpr int bubble_gutter = 12;
#ifndef _WIN32
GLFWwindow *drag_window = nullptr;
GLFWmousebuttonfun previous_mouse_button = nullptr;
GLFWcursorposfun previous_cursor = nullptr;
::Window observed_xid = 0; // published to the timer by hidden.store() in the UI callback
bool dragging = false;
int pointer_origin_x = 0, pointer_origin_y = 0, window_origin_x = 0, window_origin_y = 0;

bool root_pointer(int &x, int &y) {
    auto *display = glfwGetX11Display();
    if (!display)
        return false;
    ::Window root = None, child = None;
    int local_x = 0, local_y = 0;
    unsigned int mask = 0;
    return XQueryPointer(display, DefaultRootWindow(display), &root, &child, &x, &y, &local_x,
                         &local_y, &mask) != False;
}

void move_dragged_window(GLFWwindow *window, int pointer_x, int pointer_y) {
    int x = window_origin_x + pointer_x - pointer_origin_x;
    int y = window_origin_y + pointer_y - pointer_origin_y;
    int window_width = 0, window_height = 0;
    glfwGetWindowSize(window, &window_width, &window_height);
    int count = 0;
    GLFWmonitor **monitors = glfwGetMonitors(&count);
    for (int index = 0; index < count; ++index) {
        int left = 0, top = 0, width = 0, height = 0;
        glfwGetMonitorWorkarea(monitors[index], &left, &top, &width, &height);
        if (pointer_x >= left && pointer_x < left + width && pointer_y >= top &&
            pointer_y < top + height) {
            x = std::clamp(x, left, left + std::max(0, width - window_width));
            y = std::clamp(y, top, top + std::max(0, height - window_height));
            break;
        }
    }
    glfwSetWindowPos(window, x, y);
}
#endif
Position clamped_position(GLFWwindow *window, Position desired) {
    int pet_width = 0, pet_height = 0;
    glfwGetWindowSize(window, &pet_width, &pet_height);
    int count = 0;
    GLFWmonitor **monitors = glfwGetMonitors(&count);
    GLFWmonitor *selected = glfwGetPrimaryMonitor();
    long long best_overlap = 0;
    for (int index = 0; index < count; ++index) {
        int left = 0, top = 0, width = 0, height = 0;
        glfwGetMonitorWorkarea(monitors[index], &left, &top, &width, &height);
        const auto overlap_x = std::max<long long>(
            0, std::min<long long>(static_cast<long long>(desired.x) + pet_width,
                                   static_cast<long long>(left) + width) -
                   std::max<long long>(desired.x, left));
        const auto overlap_y = std::max<long long>(
            0, std::min<long long>(static_cast<long long>(desired.y) + pet_height,
                                   static_cast<long long>(top) + height) -
                   std::max<long long>(desired.y, top));
        if (overlap_x * overlap_y > best_overlap) {
            best_overlap = overlap_x * overlap_y;
            selected = monitors[index];
        }
    }
    if (selected) {
        int left = 0, top = 0, width = 0, height = 0;
        glfwGetMonitorWorkarea(selected, &left, &top, &width, &height);
        desired.x = std::clamp(desired.x, left, left + std::max(0, width - pet_width));
        desired.y = std::clamp(desired.y, top, top + std::max(0, height - pet_height));
    }
    return desired;
}
} // namespace
#ifndef _WIN32
namespace {
static_assert(std::atomic_bool::is_always_lock_free);
std::atomic_bool requested_exit{false};
void on_exit_signal(int) { requested_exit.store(true); }
} // namespace
#endif
bool install_exit_request(std::string &diagnostic) {
#ifndef _WIN32
    struct sigaction action {};
    action.sa_handler = on_exit_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, nullptr) != 0) {
        diagnostic = "cannot install pet exit handler";
        return false;
    }
#else
    (void)diagnostic;
#endif
    return true;
}
bool exit_requested() {
#ifndef _WIN32
    return requested_exit.load();
#else
    return false;
#endif
}
bool prepare(std::optional<Position> saved_position, std::function<void(Position)> position_changed,
             std::string &diagnostic) {
    auto *window = glfwGetCurrentContext();
    if (!window) {
        diagnostic = "mira_pet has no UI window context";
        return false;
    }
    glfwSetWindowAttrib(window, GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    position_sink = std::move(position_changed);
#ifndef _WIN32
    auto *display = glfwGetX11Display();
    int shape_event = 0, shape_error = 0, shape_major = 0, shape_minor = 0;
    if (!display || !XShapeQueryExtension(display, &shape_event, &shape_error) ||
        !XShapeQueryVersion(display, &shape_major, &shape_minor) || shape_major < 1 ||
        (shape_major == 1 && shape_minor < 1)) {
        diagnostic = "X11 input shape unavailable for draggable pet";
        return false;
    }
    glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
    if (glfwGetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH)) {
        diagnostic = "pet input shape cannot receive drag events";
        return false;
    }
    const auto xid = glfwGetX11Window(window);
    const unsigned long surface_protocol = 1;
    XChangeProperty(display, xid, XInternAtom(display, "_MIRAGE_PET_SURFACE", False), XA_CARDINAL,
                    32, PropModeReplace, reinterpret_cast<const unsigned char *>(&surface_protocol),
                    1);
    // DEC-105: a non-focus utility, excluded from GNOME's normal-window
    // minimize/map animations. Identity is still server-attested by the owner.
    const Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(display, xid, XInternAtom(display, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(&type), 1);
    XShapeCombineRectangles(display, xid, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
    const Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    const Atom hidden_from_dock[] = {XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False),
                                     XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False)};
    XChangeProperty(display, xid, wm_state, XA_ATOM, 32, PropModeAppend,
                    reinterpret_cast<const unsigned char *>(hidden_from_dock), 2);
    // GLFW creates the hidden window before onStart. Mark it non-input before
    // the runner's first map, so X11 window managers never activate the pet.
    XWMHints *hints = XGetWMHints(display, xid);
    if (!hints)
        hints = XAllocWMHints();
    if (hints) {
        hints->flags |= InputHint;
        hints->input = False;
        XSetWMHints(display, xid, hints);
        XFree(hints);
    }
    XFlush(display);
    drag_window = window;
    previous_mouse_button = glfwSetMouseButtonCallback(
        window, [](GLFWwindow *current, int button, int action, int mods) {
            if (previous_mouse_button)
                previous_mouse_button(current, button, action, mods);
            if (button != GLFW_MOUSE_BUTTON_LEFT)
                return;
            if (action == GLFW_RELEASE) {
                if (dragging && position_sink) {
                    Position position;
                    glfwGetWindowPos(current, &position.x, &position.y);
                    position.x += bubble_gutter;
                    position.y += bubble_gutter + (bubble_below ? 0 : bubble_reserve);
                    position_sink(position);
                }
                dragging = false;
            } else if (action == GLFW_PRESS && root_pointer(pointer_origin_x, pointer_origin_y)) {
                glfwGetWindowPos(current, &window_origin_x, &window_origin_y);
                dragging = true;
            }
        });
    previous_cursor = glfwSetCursorPosCallback(window, [](GLFWwindow *current, double x, double y) {
        if (previous_cursor)
            previous_cursor(current, x, y);
        int pointer_x = 0, pointer_y = 0;
        if (dragging && root_pointer(pointer_x, pointer_y))
            move_dragged_window(current, pointer_x, pointer_y);
    });
#else
    glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif
    if (!eui::window::framebufferTransparent(window)) {
        diagnostic = "transparent framebuffer unavailable";
        return false;
    }
    if (saved_position) {
        // Stored coordinates identify the character canvas, preserving older
        // 240x240 records when the new transparent bubble reserve is added.
        auto desired = *saved_position;
        int count = 0;
        auto **monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            int left = 0, top = 0, width = 0, height = 0;
            glfwGetMonitorWorkarea(monitors[i], &left, &top, &width, &height);
            if (desired.x >= left && desired.x < left + width && desired.y >= top &&
                desired.y < top + height)
                bubble_below = desired.y - top < bubble_reserve;
        }
        desired.x = static_cast<int>(std::clamp<long long>(
            static_cast<long long>(desired.x) - bubble_gutter, std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max()));
        desired.y = static_cast<int>(std::clamp<long long>(
            static_cast<long long>(desired.y) - bubble_gutter - (bubble_below ? 0 : bubble_reserve),
            std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        const auto position = clamped_position(window, desired);
        glfwSetWindowPos(window, position.x, position.y);
    } else if (auto *monitor = glfwGetPrimaryMonitor()) {
        int x = 0, y = 0, width = 0, height = 0;
        glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
        int pet_width = 0, pet_height = 0;
        glfwGetWindowSize(window, &pet_width, &pet_height);
        if (width > pet_width + 32 && height > pet_height + 32)
            glfwSetWindowPos(window, x + width - pet_width - 24, y + height - pet_height - 24);
    }
    return true;
}
Layout layout(float width, float height) {
    const float scale = width / 264.0f;
    const float side = 240 * scale, gutter = 12 * scale;
    return {width, height, gutter, bubble_below ? gutter : gutter + 58 * scale, side, bubble_below};
}
void update_hit_region(std::span<const std::uint8_t> rgba, const Layout &layout) {
#ifndef _WIN32
    if (!drag_window || layout.screen_width <= 0 || layout.screen_height <= 0)
        return;
    auto *display = glfwGetX11Display();
    if (!display)
        return;
    int window_width = 0, window_height = 0;
    glfwGetWindowSize(drag_window, &window_width, &window_height);
    const float scale_x = window_width / layout.screen_width;
    const float scale_y = window_height / layout.screen_height;
    std::vector<XRectangle> rectangles;
    constexpr int source_side = 512;
    constexpr int cell = 4;
    if (rgba.size() == source_side * source_side * 4 && layout.image_side > 0) {
        const float left = layout.image_x * scale_x;
        const float top = layout.image_y * scale_y;
        const float side_x = layout.image_side * scale_x;
        const float side_y = layout.image_side * scale_y;
        const int max_x = std::min(window_width, static_cast<int>(left + side_x));
        const int max_y = std::min(window_height, static_cast<int>(top + side_y));
        const auto opaque = [&](int x, int y) {
            const int source_x = std::clamp(
                static_cast<int>((x + 0.5f - left) * source_side / side_x), 0, source_side - 1);
            const int source_y = std::clamp(
                static_cast<int>((y + 0.5f - top) * source_side / side_y), 0, source_side - 1);
            return rgba[(source_y * source_side + source_x) * 4 + 3] >= 160;
        };
        for (int y = std::max(0, static_cast<int>(top)); y + cell <= max_y; y += cell) {
            int run = -1;
            for (int x = std::max(0, static_cast<int>(left)); x + cell <= max_x; x += cell) {
                bool solid = true;
                for (int dy = 0; dy < cell && solid; ++dy)
                    for (int dx = 0; dx < cell; ++dx)
                        if (!opaque(x + dx, y + dy)) {
                            solid = false;
                            break;
                        }
                if (solid && run < 0)
                    run = x;
                if ((!solid || x + cell * 2 > max_x) && run >= 0) {
                    const int right = solid ? x + cell : x;
                    rectangles.push_back({static_cast<short>(run), static_cast<short>(y),
                                          static_cast<unsigned short>(right - run), cell});
                    run = -1;
                }
            }
        }
    }
    XShapeCombineRectangles(display, glfwGetX11Window(drag_window), ShapeInput, 0, 0,
                            rectangles.data(), static_cast<int>(rectangles.size()), ShapeSet,
                            Unsorted);
    XFlush(display);
#else
    (void)rgba;
    (void)layout;
#endif
}
namespace {
void report_visibility() {
    bool visible = glfwGetWindowAttrib(observed_window, GLFW_ICONIFIED) != GLFW_TRUE;
#ifndef _WIN32
    if (visible) {
        // This is GLFW's connection, used exclusively by its UI owner.
        Bool bounding = False, clip = False;
        int x = 0, y = 0, cx = 0, cy = 0;
        unsigned int width = 0, height = 0, cw = 0, ch = 0;
        const bool ok = XShapeQueryExtents(glfwGetX11Display(), observed_xid, &bounding, &x, &y,
                                           &width, &height, &clip, &cx, &cy, &cw, &ch);
        XWindowAttributes attributes{};
        visible = ok && XGetWindowAttributes(glfwGetX11Display(), observed_xid, &attributes) &&
                  attributes.map_state == IsViewable && (!bounding || (width && height));
        if (!ok && !visibility_failed)
            std::cerr << "mira_pet: presentation visibility query failed\n";
        visibility_failed = !ok;
    }
#endif
    hidden.store(!visible);
    if (presented != visible) {
        presented = visible;
        if (visibility_sink)
            visibility_sink(visible);
    }
}
} // namespace
void watch_visibility(std::function<void(bool)> sink) {
    if (!observed_window) {
        observed_window = glfwGetCurrentContext();
        if (!observed_window)
            return;
#ifndef _WIN32
        observed_xid = glfwGetX11Window(observed_window);
#endif
        visibility_sink = std::move(sink);
        hidden.store(glfwGetWindowAttrib(observed_window, GLFW_ICONIFIED) == GLFW_TRUE);
        previous_iconify =
            glfwSetWindowIconifyCallback(observed_window, [](GLFWwindow *window, int value) {
                if (previous_iconify)
                    previous_iconify(window, value);
                hidden.store(value == GLFW_TRUE);
                report_visibility();
            });
    }
    report_visibility();
}
void release_visibility() {
    // Called after the Bridge timers/workers have joined, before window teardown.
    if (auto *window = glfwGetCurrentContext(); window && position_sink) {
        Position position;
        glfwGetWindowPos(window, &position.x, &position.y);
        position.x += bubble_gutter;
        position.y += bubble_gutter + (bubble_below ? 0 : bubble_reserve);
        position_sink(position);
    }
    if (observed_window)
        glfwSetWindowIconifyCallback(observed_window, previous_iconify);
    observed_window = nullptr;
    visibility_sink = {};
    position_sink = {};
#ifndef _WIN32
    if (drag_window) {
        glfwSetCursorPosCallback(drag_window, previous_cursor);
        glfwSetMouseButtonCallback(drag_window, previous_mouse_button);
    }
    drag_window = nullptr;
    previous_cursor = nullptr;
    previous_mouse_button = nullptr;
    dragging = false;
#endif
}
bool relay_hidden_exit(bool owner_lost, std::string &diagnostic) {
#ifndef _WIN32
    if (!hidden.load() || (!owner_lost && !exit_requested()) || exit_relayed.exchange(true))
        return true;
    // Own connection: do not use GLFW's Xlib connection from an Executor task.
    auto *display = XOpenDisplay(nullptr);
    if (!display) {
        diagnostic = "cannot open X11 connection for hidden pet exit";
        return false;
    }
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = observed_xid;
    event.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(XInternAtom(display, "WM_DELETE_WINDOW", False));
    const bool sent = XSendEvent(display, observed_xid, False, NoEventMask, &event) != 0;
    XFlush(display);
    XCloseDisplay(display);
    if (!sent)
        diagnostic = "hidden pet exit event was refused";
    return sent;
#else
    (void)owner_lost;
    (void)diagnostic;
    return true;
#endif
}
void request_close() {
    if (auto *window = glfwGetCurrentContext())
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}
} // namespace mirage::platform::pet_window
