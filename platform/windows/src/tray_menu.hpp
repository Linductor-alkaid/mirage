#pragma once

#include "win32_util.hpp"
#include <algorithm>
#include <array>
#include <dwmapi.h>
#include <string>

namespace mirage::platform::windows_backend {

// Modeless popup on the existing Executor-owned pump. Standard BUTTON children
// retain accessible labels/disabled state; no nested message loop or worker.
class TrayMenuPresentation {
  public:
    explicit TrayMenuPresentation(UINT dpi) : dpi_(dpi ? dpi : 96) {
        font_ = ::CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Segoe UI");
    }
    ~TrayMenuPresentation() {
        dismiss();
        if (font_)
            ::DeleteObject(font_);
    }
    TrayMenuPresentation(const TrayMenuPresentation &) = delete;
    TrayMenuPresentation &operator=(const TrayMenuPresentation &) = delete;

    bool append(UINT id, std::wstring label, bool enabled, bool separate = false) {
        if (count_ == items_.size() || window_)
            return false;
        items_[count_++] = {id, std::move(label), enabled, separate};
        return true;
    }
    int width() const { return scale(300); }
    int height() const {
        int value = scale(16) + static_cast<int>(count_) * scale(40);
        for (std::size_t i = 0; i < count_; ++i)
            if (items_[i].separate)
                value += scale(17);
        return value;
    }
    HWND window() const { return window_; }
    void dismiss() {
        if (window_)
            ::DestroyWindow(window_);
    }
    bool show(HWND owner, POINT anchor, UINT command_message) {
        if (!count_ || !font_ || window_)
            return false;
        WNDCLASSW description{};
        description.style = CS_DROPSHADOW;
        description.lpfnWndProc = window_proc;
        description.hInstance = ::GetModuleHandleW(nullptr);
        description.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        description.lpszClassName = L"MirageTrayPopup";
        if (!::RegisterClassW(&description) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!::GetMonitorInfoW(::MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor))
            return false;
        const auto &area = monitor.rcWork;
        const int x = std::clamp(
            static_cast<int>(anchor.x) - width(), static_cast<int>(area.left),
            std::max(static_cast<int>(area.left), static_cast<int>(area.right) - width()));
        const int y = std::clamp(
            static_cast<int>(anchor.y) - height(), static_cast<int>(area.top),
            std::max(static_cast<int>(area.top), static_cast<int>(area.bottom) - height()));
        owner_ = owner;
        command_message_ = command_message;
        hot_ = nullptr;
        keyboard_ = false;
        ::GetCursorPos(&last_cursor_);
        window_ = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, description.lpszClassName,
                                    L"Mirage 菜单", WS_POPUP | WS_THICKFRAME, x, y, width(),
                                    height(), owner, nullptr, description.hInstance, this);
        if (!window_)
            return false;
        // Windows 11 supplies antialiased corners and shadow. Windows 10 falls
        // back to a clipped contour and the class drop shadow, with opaque paint.
        const DWORD corners = 2; // DWMWCP_ROUND; Windows 11 attribute, older SDK safe
        if (FAILED(::DwmSetWindowAttribute(window_, 33, &corners, sizeof(corners)))) {
            HRGN region =
                ::CreateRoundRectRgn(0, 0, width() + 1, height() + 1, scale(16), scale(16));
            if (region && !::SetWindowRgn(window_, region, FALSE))
                ::DeleteObject(region);
        }
        const MARGINS margins{1, 1, 1, 1};
        ::DwmExtendFrameIntoClientArea(window_, &margins);
        const COLORREF border = RGB(230, 230, 233);
        (void)::DwmSetWindowAttribute(window_, 34, &border, sizeof(border));
        int top = scale(8);
        for (std::size_t i = 0; i < count_; ++i) {
            auto &item = items_[i];
            if (item.separate)
                top += scale(17);
            item.button = ::CreateWindowExW(0, L"BUTTON", item.label.c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                            scale(8), top, width() - scale(16), scale(40), window_,
                                            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(item.id)),
                                            description.hInstance, nullptr);
            if (!item.button) {
                dismiss();
                return false;
            }
            ::SendMessageW(item.button, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
            ::EnableWindow(item.button, item.enabled);
            ::SetWindowLongPtrW(item.button, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&item));
            item.previous = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
                item.button, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(button_proc)));
            top += scale(40);
        }
        ::ShowWindow(window_, SW_SHOW);
        // A hidden startup hint applies only to the first ShowWindow in a GUI
        // process. An explicitly requested tray popup must still become visible.
        if (!::IsWindowVisible(window_))
            ::ShowWindow(window_, SW_SHOW);
        ::SetForegroundWindow(window_);
        for (std::size_t i = 0; i < count_; ++i)
            if (items_[i].enabled) {
                ::SetFocus(items_[i].button);
                break;
            }
        return true;
    }

  private:
    struct Item {
        UINT id = 0;
        std::wstring label;
        bool enabled = false, separate = false;
        HWND button = nullptr;
        WNDPROC previous = nullptr;
    };
    int scale(int value) const { return ::MulDiv(value, static_cast<int>(dpi_), 96); }
    void highlight(HWND button) {
        if (hot_ == button)
            return;
        hot_ = button;
        for (std::size_t i = 0; i < count_; ++i)
            ::InvalidateRect(items_[i].button, nullptr, FALSE);
    }
    void key(UINT key) {
        if (key == VK_ESCAPE) {
            dismiss();
            return;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            ::SendMessageW(window_, WM_COMMAND, static_cast<WPARAM>(::GetDlgCtrlID(::GetFocus())),
                           0);
            return;
        }
        if (key != VK_UP && key != VK_DOWN && key != VK_TAB && key != VK_HOME && key != VK_END)
            return;
        keyboard_ = true;
        int index = -1;
        for (std::size_t i = 0; i < count_; ++i)
            if (items_[i].button == ::GetFocus())
                index = static_cast<int>(i);
        const bool backward = key == VK_UP || (key == VK_TAB && ::GetKeyState(VK_SHIFT) < 0);
        if (key == VK_HOME)
            index = -1;
        if (key == VK_END)
            index = static_cast<int>(count_);
        for (std::size_t i = 0; i < count_; ++i) {
            index = (index + (backward || key == VK_END ? -1 : 1) + static_cast<int>(count_)) %
                    static_cast<int>(count_);
            if (items_[static_cast<std::size_t>(index)].enabled) {
                ::SetFocus(items_[static_cast<std::size_t>(index)].button);
                highlight(items_[static_cast<std::size_t>(index)].button);
                break;
            }
        }
    }
    static LRESULT CALLBACK button_proc(HWND button, UINT message, WPARAM wp, LPARAM lp) {
        auto *item = reinterpret_cast<Item *>(::GetWindowLongPtrW(button, GWLP_USERDATA));
        auto *self = reinterpret_cast<TrayMenuPresentation *>(
            ::GetWindowLongPtrW(::GetParent(button), GWLP_USERDATA));
        if (self && message == WM_GETDLGCODE)
            return DLGC_WANTALLKEYS;
        if (self && message == WM_KEYDOWN) {
            self->key(static_cast<UINT>(wp));
            return 0;
        }
        if (self && message == WM_MOUSEMOVE) {
            POINT cursor{};
            ::GetCursorPos(&cursor);
            if (!self->keyboard_ || cursor.x != self->last_cursor_.x ||
                cursor.y != self->last_cursor_.y) {
                self->keyboard_ = false;
                self->highlight(button);
            }
            self->last_cursor_ = cursor;
            TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, button, 0};
            ::TrackMouseEvent(&track);
        }
        if (self && message == WM_MOUSELEAVE && !self->keyboard_)
            self->highlight(nullptr);
        return item && item->previous ? ::CallWindowProcW(item->previous, button, message, wp, lp)
                                      : ::DefWindowProcW(button, message, wp, lp);
    }
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto *self =
            reinterpret_cast<TrayMenuPresentation *>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<TrayMenuPresentation *>(
                reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return ::DefWindowProcW(window, message, wp, lp);
        switch (message) {
        case WM_NCCALCSIZE:
            return 0;
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE)
                self->dismiss();
            return 0;
        case WM_CLOSE:
            self->dismiss();
            return 0;
        case WM_NCDESTROY:
            self->window_ = nullptr;
            break;
        case WM_KEYDOWN:
            self->key(static_cast<UINT>(wp));
            return 0;
        case WM_COMMAND:
            for (std::size_t i = 0; i < self->count_; ++i)
                if (LOWORD(wp) == self->items_[i].id && self->items_[i].enabled) {
                    ::PostMessageW(self->owner_, self->command_message_, self->items_[i].id, 0);
                    self->dismiss();
                    break;
                }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = ::BeginPaint(window, &paint);
            ::FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
            HBRUSH line = ::CreateSolidBrush(RGB(224, 228, 235));
            int top = self->scale(8);
            for (std::size_t i = 0; i < self->count_; ++i) {
                if (self->items_[i].separate) {
                    RECT rect{0, top + self->scale(8), self->width(), top + self->scale(8) + 1};
                    ::FillRect(dc, &rect, line);
                    top += self->scale(17);
                }
                top += self->scale(40);
            }
            ::DeleteObject(line);
            ::EndPaint(window, &paint);
            return 0;
        }
        case WM_DRAWITEM: {
            const auto &value = *reinterpret_cast<DRAWITEMSTRUCT *>(lp);
            if (value.CtlType != ODT_BUTTON)
                break;
            ::FillRect(value.hDC, &value.rcItem,
                       static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
            const bool enabled = ::IsWindowEnabled(value.hwndItem) != 0;
            const bool selected =
                self->keyboard_ ? ::GetFocus() == value.hwndItem : self->hot_ == value.hwndItem;
            if (enabled && selected) {
                HBRUSH brush = ::CreateSolidBrush(RGB(240, 242, 245));
                const auto previous_brush = ::SelectObject(value.hDC, brush);
                const auto previous_pen = ::SelectObject(value.hDC, ::GetStockObject(NULL_PEN));
                ::RoundRect(value.hDC, value.rcItem.left, value.rcItem.top, value.rcItem.right,
                            value.rcItem.bottom, self->scale(12), self->scale(12));
                ::SelectObject(value.hDC, previous_pen);
                ::SelectObject(value.hDC, previous_brush);
                ::DeleteObject(brush);
            }
            RECT text = value.rcItem;
            text.left += self->scale(12);
            text.right -= self->scale(12);
            const auto previous = ::SelectObject(value.hDC, self->font_);
            ::SetBkMode(value.hDC, TRANSPARENT);
            ::SetTextColor(value.hDC, enabled ? RGB(24, 26, 31) : RGB(145, 148, 154));
            for (std::size_t i = 0; i < self->count_; ++i)
                if (self->items_[i].button == value.hwndItem)
                    ::DrawTextW(value.hDC, self->items_[i].label.c_str(), -1, &text,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ::SelectObject(value.hDC, previous);
            return TRUE;
        }
        }
        return ::DefWindowProcW(window, message, wp, lp);
    }
    UINT dpi_;
    HFONT font_ = nullptr;
    HWND window_ = nullptr, owner_ = nullptr, hot_ = nullptr;
    UINT command_message_ = 0;
    std::array<Item, 3> items_{};
    std::size_t count_ = 0;
    bool keyboard_ = false;
    POINT last_cursor_{};
};

} // namespace mirage::platform::windows_backend
