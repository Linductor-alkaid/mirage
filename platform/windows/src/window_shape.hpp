#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mirage::platform::windows_backend {

// Product-window contour fallback for BUG-20261014-007. An alpha framebuffer
// does not guarantee correct DWM composition on every display path. Windows
// clips outside this contour independently of the OpenGL redirection surface.
// UI-thread only; Windows owns the region after a successful SetWindowRgn.
class RoundedWindowShape {
  public:
    bool update(HWND window, int radius) {
        RECT bounds{};
        if (!::GetWindowRect(window, &bounds))
            return false;
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0)
            return false;
        if (window == window_ && width == width_ && height == height_ && radius == radius_)
            return true;
        HRGN region =
            radius > 0 ? ::CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2)
                       : nullptr;
        if (radius > 0 && !region)
            return false;
        if (!::SetWindowRgn(window, region, TRUE)) {
            if (region)
                ::DeleteObject(region);
            return false;
        }
        window_ = window;
        width_ = width;
        height_ = height;
        radius_ = radius;
        return true;
    }
    void reset() { window_ = nullptr; }

  private:
    HWND window_ = nullptr;
    int width_ = 0, height_ = 0, radius_ = -1;
};

} // namespace mirage::platform::windows_backend
