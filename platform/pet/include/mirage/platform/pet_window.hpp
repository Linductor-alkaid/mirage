#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace mirage::platform::pet_window {
struct Layout {
    float screen_width = 0;
    float screen_height = 0;
    float image_x = 0;
    float image_y = 0;
    float image_side = 0;
    bool bubble_below = false;
};
struct Position {
    int x = 0;
    int y = 0;
};
// Called only on EUI's owning UI thread before its first visible frame.
// Public signatures intentionally contain no GLFW or OS types.
bool prepare(std::optional<Position> saved_position, std::function<void(Position)> position_changed,
             std::string &diagnostic);
// Update the draggable X11 input shape from the visible RGBA frame. Transparent
// pixels remain click-through; the visible character remains a drag handle.
Layout layout(float screen_width, float screen_height);
void update_hit_region(std::span<const std::uint8_t> rgba, const Layout &layout);
// Install after the runner's callbacks, on its UI thread. Chain its iconify
// callback, and sample the actual bounding shape on subsequent UI checks.
// Reports iconification/exclusion suspension and restoration without OS types.
void watch_visibility(std::function<void(bool)> sink);
void release_visibility();
// A hidden window skips UI frame callbacks. The IPC/timer owner may relay its
// pending exit to the X11 event loop so shutdown can finish without animation.
bool relay_hidden_exit(bool owner_lost, std::string &diagnostic);
void request_close();
// Parent-owned graceful exit request. Only the pet process installs this hook.
bool install_exit_request(std::string &diagnostic);
bool exit_requested();
} // namespace mirage::platform::pet_window
