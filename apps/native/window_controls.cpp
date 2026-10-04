#include "window_controls.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace mirage::native_ui::window {
namespace {
// All access occurs on EUI's UI thread. The window is owned/destroyed by EUI.
GLFWwindow *handle = nullptr;
int start_x = 0, start_y = 0, start_width = 0, start_height = 0;
double pointer_x = 0, pointer_y = 0;
int resize_edges = 0;
int minimum_width = 860, minimum_height = 620;
GLFWcursor *sidebar_cursor = nullptr;
bool sidebar_cursor_active = false;

void begin() {
    glfwGetWindowPos(handle, &start_x, &start_y);
    glfwGetWindowSize(handle, &start_width, &start_height);
    glfwGetCursorPos(handle, &pointer_x, &pointer_y);
    pointer_x += start_x;
    pointer_y += start_y;
}

void delta(int &dx, int &dy) {
    int x = 0, y = 0;
    double cursor_x = 0, cursor_y = 0;
    glfwGetWindowPos(handle, &x, &y);
    glfwGetCursorPos(handle, &cursor_x, &cursor_y);
    dx = static_cast<int>(std::lround(x + cursor_x - pointer_x));
    dy = static_cast<int>(std::lround(y + cursor_y - pointer_y));
}
} // namespace

void initialize() {
    handle = glfwGetCurrentContext();
    if (!handle)
        return;
    sidebar_cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    float scale_x = 1, scale_y = 1;
    int width = 0, height = 0, fb_width = 0, fb_height = 0;
    glfwGetWindowContentScale(handle, &scale_x, &scale_y);
    glfwGetWindowSize(handle, &width, &height);
    glfwGetFramebufferSize(handle, &fb_width, &fb_height);
    const float scale = (scale_x + scale_y) / 2;
    // EUI composes in framebuffer / content-scale units. GLFW's X11 sizes
    // are physical pixels, so apply that scale to logical startup/min sizes.
    const float ratio = width > 0 && fb_width > 0
                            ? scale * static_cast<float>(width) / static_cast<float>(fb_width)
                            : 1;
    minimum_width = static_cast<int>(std::lround(860 * ratio));
    minimum_height = static_cast<int>(std::lround(620 * ratio));
    glfwSetWindowSizeLimits(handle, minimum_width, minimum_height, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowSize(handle, static_cast<int>(std::lround(1180 * ratio)),
                      static_cast<int>(std::lround(800 * ratio)));
}
bool primary_pointer_down() {
    return handle && glfwGetMouseButton(handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}
void sidebar_resize_cursor(bool active) {
    if (!handle)
        return;
    if (active && sidebar_cursor)
        glfwSetCursor(handle, sidebar_cursor);
    else if (sidebar_cursor_active)
        glfwSetCursor(handle, nullptr);
    sidebar_cursor_active = active;
}
void shutdown() {
    if (handle)
        glfwSetCursor(handle, nullptr);
    if (sidebar_cursor)
        glfwDestroyCursor(sidebar_cursor);
    sidebar_cursor = nullptr;
    sidebar_cursor_active = false;
    handle = nullptr;
}
void minimize() {
    if (handle)
        glfwIconifyWindow(handle);
}
bool maximized() { return handle && glfwGetWindowAttrib(handle, GLFW_MAXIMIZED) != 0; }
void toggle_maximize() {
    if (!handle)
        return;
    if (maximized())
        glfwRestoreWindow(handle);
    else
        glfwMaximizeWindow(handle);
}
void close() {
    if (handle)
        glfwSetWindowShouldClose(handle, GLFW_TRUE);
}
void copy_text(const std::string &text) {
    if (handle)
        glfwSetClipboardString(handle, text.c_str());
}
void begin_move() {
    if (handle && !maximized())
        begin();
}
void move() {
    if (!handle || maximized())
        return;
    int dx = 0, dy = 0;
    delta(dx, dy);
    glfwSetWindowPos(handle, start_x + dx, start_y + dy);
}
void begin_resize(int edges) {
    if (!handle || maximized())
        return;
    resize_edges = edges;
    begin();
}
void resize() {
    if (!handle || maximized())
        return;
    int dx = 0, dy = 0;
    delta(dx, dy);
    const bool from_left = (resize_edges & left) != 0;
    const bool from_top = (resize_edges & top) != 0;
    const bool horizontal = (resize_edges & (left | right)) != 0;
    const bool vertical = (resize_edges & (top | bottom)) != 0;
    const int width =
        horizontal ? std::max(minimum_width, start_width + (from_left ? -dx : dx)) : start_width;
    const int height =
        vertical ? std::max(minimum_height, start_height + (from_top ? -dy : dy)) : start_height;
    glfwSetWindowPos(handle, from_left ? start_x + start_width - width : start_x,
                     from_top ? start_y + start_height - height : start_y);
    glfwSetWindowSize(handle, width, height);
}
} // namespace mirage::native_ui::window
