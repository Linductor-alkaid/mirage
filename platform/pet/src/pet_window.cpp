#include <mirage/platform/pet_window.hpp>

#include <GLFW/glfw3.h>
#include <eui/window.h>
#ifndef _WIN32
#include <csignal>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xutil.h>
#endif

namespace mirage::platform::pet_window {
#ifndef _WIN32
namespace {
volatile std::sig_atomic_t requested_exit = 0;
void on_exit_signal(int) { requested_exit = 1; }
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
    return requested_exit != 0;
#else
    return false;
#endif
}
bool prepare(std::string &diagnostic) {
    auto *window = glfwGetCurrentContext();
    if (!window) {
        diagnostic = "mira_pet has no UI window context";
        return false;
    }
    glfwSetWindowAttrib(window, GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#ifndef _WIN32
    // GLFW creates the hidden window before onStart. Mark it non-input before
    // the runner's first map, so X11 window managers never activate the pet.
    if (auto *display = glfwGetX11Display()) {
        const auto xid = glfwGetX11Window(window);
        XWMHints *hints = XGetWMHints(display, xid);
        if (!hints)
            hints = XAllocWMHints();
        if (hints) {
            hints->flags |= InputHint;
            hints->input = False;
            XSetWMHints(display, xid, hints);
            XFree(hints);
            XFlush(display);
        }
    }
#endif
    if (!glfwGetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH) ||
        !eui::window::framebufferTransparent(window)) {
        diagnostic = "transparent framebuffer or mouse passthrough unavailable";
        return false;
    }
    if (auto *monitor = glfwGetPrimaryMonitor()) {
        int x = 0, y = 0, width = 0, height = 0;
        glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
        int pet_width = 0, pet_height = 0;
        glfwGetWindowSize(window, &pet_width, &pet_height);
        if (width > pet_width + 32 && height > pet_height + 32)
            glfwSetWindowPos(window, x + width - pet_width - 24, y + height - pet_height - 24);
    }
    return true;
}
void request_close() {
    if (auto *window = glfwGetCurrentContext())
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}
} // namespace mirage::platform::pet_window
