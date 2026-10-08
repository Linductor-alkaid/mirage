#pragma once

#include <string>

namespace mirage::native_ui::window {
// Private GLFW adapter. No window/library types enter Mirage public contracts.
void initialize();
void minimize();
void show();
void toggle_maximize();
bool maximized();
// Framebuffer / composed logical width includes display DPI and EUI UI scale.
float render_scale(float logical_width);
void close();
void copy_text(const std::string &text);
void begin_move();
void move();
enum Edge { left = 1, right = 2, top = 4, bottom = 8 };
void begin_resize(int edges);
void resize();
bool primary_pointer_down();
void sidebar_resize_cursor(bool active);
void shutdown();
} // namespace mirage::native_ui::window
