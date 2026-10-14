#include "../../platform/windows/src/tray_menu.hpp"
#include "../../platform/windows/src/window_shape.hpp"
#include "../support/test.hpp"

namespace {
using mirage::platform::windows_backend::RoundedWindowShape;
using mirage::platform::windows_backend::TrayMenuPresentation;
constexpr UINT command_message = WM_APP + 3;
UINT selected = 0;
LRESULT CALLBACK owner_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == command_message) {
        selected = static_cast<UINT>(wp);
        return 0;
    }
    return ::DefWindowProcW(window, message, wp, lp);
}
void drain() {
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}
HWND create_owner() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = owner_proc;
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.lpszClassName = L"MirageTrayMenuTest";
    ::RegisterClassW(&cls);
    return ::CreateWindowExW(0, cls.lpszClassName, L"Mirage tray menu preview", WS_POPUP, 120, 120,
                             320, 260, nullptr, nullptr, cls.hInstance, nullptr);
}
void menu_behavior(UINT dpi) {
    HWND owner = create_owner();
    MIRAGE_CHECK(owner != nullptr);
    TrayMenuPresentation menu(dpi);
    MIRAGE_CHECK(menu.append(3, L"打开应用", true));
    MIRAGE_CHECK(menu.append(4, L"显示桌宠", false));
    MIRAGE_CHECK(menu.append(9, L"退出应用", true, true));
    MIRAGE_CHECK(!menu.append(10, L"overflow", true));
    MIRAGE_CHECK(menu.width() == ::MulDiv(300, static_cast<int>(dpi), 96));
    MIRAGE_CHECK(menu.height() == ::MulDiv(16, static_cast<int>(dpi), 96) +
                                      3 * ::MulDiv(40, static_cast<int>(dpi), 96) +
                                      ::MulDiv(17, static_cast<int>(dpi), 96));
    MIRAGE_CHECK(menu.show(owner, {1, 1}, command_message));
    HWND popup = menu.window();
    MIRAGE_CHECK(popup != nullptr);
    MIRAGE_CHECK(::IsWindowVisible(popup));
    HWND open = ::GetDlgItem(popup, 3);
    HWND disabled = ::GetDlgItem(popup, 4);
    HWND quit = ::GetDlgItem(popup, 9);
    wchar_t label[32]{};
    ::GetWindowTextW(disabled, label, 32);
    MIRAGE_CHECK(std::wstring{label} == L"显示桌宠");
    MIRAGE_CHECK(!::IsWindowEnabled(disabled));
    RECT rect{};
    ::GetWindowRect(popup, &rect);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    ::GetMonitorInfoW(::MonitorFromWindow(popup, MONITOR_DEFAULTTONEAREST), &monitor);
    MIRAGE_CHECK(rect.left >= monitor.rcWork.left && rect.top >= monitor.rcWork.top);
    MIRAGE_CHECK(rect.right <= monitor.rcWork.right && rect.bottom <= monitor.rcWork.bottom);
    ::SetFocus(open);
    ::SendMessageW(open, WM_KEYDOWN, VK_DOWN, 0);
    MIRAGE_CHECK(::GetFocus() == quit); // disabled action skipped
    ::SendMessageW(quit, WM_KEYDOWN, VK_DOWN, 0);
    MIRAGE_CHECK(::GetFocus() == open); // wrap
    ::SendMessageW(open, WM_KEYDOWN, VK_END, 0);
    MIRAGE_CHECK(::GetFocus() == quit);
    ::SendMessageW(quit, WM_KEYDOWN, VK_HOME, 0);
    MIRAGE_CHECK(::GetFocus() == open);
    selected = 0;
    ::SendMessageW(popup, WM_COMMAND, 4, 0);
    drain();
    MIRAGE_CHECK(selected == 0 && ::IsWindow(popup)); // disabled cannot dispatch
    ::SetFocus(open);
    ::SendMessageW(open, WM_KEYDOWN, VK_RETURN, 0);
    drain();
    MIRAGE_CHECK(selected == 3 && menu.window() == nullptr && !::IsWindow(popup));
    MIRAGE_CHECK(menu.show(owner, {500, 500}, command_message));
    popup = menu.window();
    selected = 0;
    ::SendMessageW(::GetDlgItem(popup, 9), BM_CLICK, 0, 0);
    drain();
    MIRAGE_CHECK(selected == 9 && !menu.window());
    MIRAGE_CHECK(menu.show(owner, {500, 500}, command_message));
    ::SendMessageW(menu.window(), WM_KEYDOWN, VK_ESCAPE, 0);
    MIRAGE_CHECK(!menu.window());
    MIRAGE_CHECK(menu.show(owner, {500, 500}, command_message));
    ::SendMessageW(menu.window(), WM_ACTIVATE, WA_INACTIVE, 0);
    MIRAGE_CHECK(!menu.window());
    MIRAGE_CHECK(menu.show(owner, {500, 500}, command_message));
    popup = menu.window();
    menu.dismiss(); // same cleanup used by carrier stop, with no modal wait
    MIRAGE_CHECK(!::IsWindow(popup));
    ::DestroyWindow(owner);
}
void window_contour() {
    HWND window = create_owner();
    RoundedWindowShape shape;
    HRGN region = ::CreateRectRgn(0, 0, 0, 0);
    for (const int radius : {12, 18, 24}) {
        MIRAGE_CHECK(shape.update(window, radius));
        MIRAGE_CHECK(::GetWindowRgn(window, region) == COMPLEXREGION);
        MIRAGE_CHECK(!::PtInRegion(region, 1, 1));
        MIRAGE_CHECK(!::PtInRegion(region, 319, 1));
        MIRAGE_CHECK(!::PtInRegion(region, 1, 259));
        MIRAGE_CHECK(!::PtInRegion(region, 319, 259));
        MIRAGE_CHECK(::PtInRegion(region, 160, 1));
        MIRAGE_CHECK(::PtInRegion(region, 160, 130));
    }
    ::SetWindowPos(window, nullptr, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER);
    MIRAGE_CHECK(shape.update(window, 24));
    MIRAGE_CHECK(::GetWindowRgn(window, region) == COMPLEXREGION);
    MIRAGE_CHECK(!::PtInRegion(region, 639, 479));
    MIRAGE_CHECK(::PtInRegion(region, 320, 479));
    MIRAGE_CHECK(shape.update(window, 0)); // maximized contour restored to rectangle
    MIRAGE_CHECK(::GetWindowRgn(window, region) == ERROR);
    MIRAGE_CHECK(shape.update(window, 12)); // restore
    ::DeleteObject(region);
    ::DestroyWindow(window);
}
void preview() {
    HWND owner = create_owner();
    TrayMenuPresentation menu(96);
    menu.append(3, L"打开应用", true);
    menu.append(9, L"退出应用", true, true);
    menu.show(owner, {700, 500}, command_message);
    // Expose the transient surface as a standalone preview for inspection tools;
    // the shipping tray popup remains an owned tool window without taskbar entry.
    ::SetWindowLongPtrW(menu.window(), GWLP_HWNDPARENT, 0);
    ::SetWindowLongPtrW(menu.window(), GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
    ::ShowWindow(menu.window(), SW_SHOW); // override hidden first-show hint in preview
    ::ShowWindow(menu.window(), SW_SHOW);
    ::SetForegroundWindow(menu.window());
    MSG message{};
    while (menu.window() && ::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    ::DestroyWindow(owner);
}
} // namespace
int main(int argc, char **argv) {
    if (argc > 1 && std::string{argv[1]} == "--preview") {
        preview();
        return 0;
    }
    menu_behavior(96);
    menu_behavior(144);
    menu_behavior(192);
    window_contour();
    return mirage::testing::finish("win32_tray_menu_test");
}
