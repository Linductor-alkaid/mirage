#include "../../platform/windows/src/tray_menu.hpp"
#include "../support/test.hpp"

#include <shellapi.h>

namespace {
using mirage::platform::windows_backend::TrayMenuPresentation;

LRESULT CALLBACK preview_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *presentation =
        reinterpret_cast<TrayMenuPresentation *>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (presentation && message == WM_MEASUREITEM &&
        presentation->measure(*reinterpret_cast<MEASUREITEMSTRUCT *>(lparam)))
        return TRUE;
    if (presentation && message == WM_DRAWITEM &&
        presentation->draw(*reinterpret_cast<DRAWITEMSTRUCT *>(lparam)))
        return TRUE;
    return ::DefWindowProcW(window, message, wparam, lparam);
}

void preview_menu() {
    WNDCLASSW description{};
    description.lpfnWndProc = preview_proc;
    description.hInstance = ::GetModuleHandleW(nullptr);
    description.lpszClassName = L"MirageTrayMenuPreview";
    description.hbrBackground = ::GetSysColorBrush(COLOR_MENU);
    ::RegisterClassW(&description);
    HWND window = ::CreateWindowExW(0, description.lpszClassName, L"Mirage tray menu preview",
                                    WS_OVERLAPPEDWINDOW, 120, 120, 320, 260, nullptr, nullptr,
                                    description.hInstance, nullptr);
    TrayMenuPresentation presentation(96);
    ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&presentation));
    HMENU menu = ::CreatePopupMenu();
    presentation.append(menu, 3, L"打开应用", true);
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    presentation.append(menu, 9, L"退出应用", true);
    ::ShowWindow(window, SW_SHOW);
    ::ShowWindow(window, SW_SHOW); // override a hidden console launch's first-show hint
    ::UpdateWindow(window);
    ::SetForegroundWindow(window);
    POINT point{16, 24};
    ::ClientToScreen(window, &point);
    (void)::TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, window, nullptr);
    ::DestroyMenu(menu);
    ::DestroyWindow(window);
    ::UnregisterClassW(description.lpszClassName, description.hInstance);
}

void menu_metrics(UINT dpi) {
    HMENU menu = ::CreatePopupMenu();
    MIRAGE_CHECK(menu != nullptr);
    TrayMenuPresentation presentation(dpi);
    MIRAGE_CHECK(presentation.append(menu, 3, L"打开应用", true));
    MIRAGE_CHECK(presentation.append(menu, 4, L"显示桌宠", false));
    MIRAGE_CHECK(presentation.append(menu, 9, L"退出应用", true));
    MIRAGE_CHECK(!presentation.append(menu, 10, L"overflow", true));
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_DATA | MIIM_FTYPE | MIIM_STATE;
    MIRAGE_CHECK(::GetMenuItemInfoW(menu, 4, FALSE, &item) != 0);
    MIRAGE_CHECK((item.fType & MFT_OWNERDRAW) != 0);
    MIRAGE_CHECK((item.fState & MFS_DISABLED) != 0);
    wchar_t accessible_label[32]{};
    MIRAGE_CHECK(::GetMenuStringW(menu, 4, accessible_label, 32, MF_BYCOMMAND) > 0);
    MIRAGE_CHECK(std::wstring{accessible_label} == L"显示桌宠");
    MEASUREITEMSTRUCT measure{};
    measure.CtlType = ODT_MENU;
    measure.itemData = item.dwItemData;
    MIRAGE_CHECK(presentation.measure(measure));
    MIRAGE_CHECK(measure.itemWidth >= static_cast<UINT>(::MulDiv(220, static_cast<int>(dpi), 96)));
    MIRAGE_CHECK(measure.itemHeight >= static_cast<UINT>(::MulDiv(40, static_cast<int>(dpi), 96)));
    measure.itemData = 0;
    MIRAGE_CHECK(!presentation.measure(measure));
    HDC screen = ::GetDC(nullptr);
    HDC dc = ::CreateCompatibleDC(screen);
    HBITMAP bitmap = ::CreateCompatibleBitmap(screen, 480, 100);
    const auto previous = ::SelectObject(dc, bitmap);
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_MENU;
    draw.itemData = item.dwItemData;
    draw.hDC = dc;
    draw.rcItem = {0, 0, 440, 80};
    draw.itemState = ODS_DISABLED | ODS_SELECTED;
    MIRAGE_CHECK(presentation.draw(draw));
    draw.itemState = ODS_SELECTED | ODS_FOCUS;
    MIRAGE_CHECK(presentation.draw(draw));
    draw.CtlType = ODT_BUTTON;
    MIRAGE_CHECK(!presentation.draw(draw));
    ::SelectObject(dc, previous);
    ::DeleteObject(bitmap);
    ::DeleteDC(dc);
    ::ReleaseDC(nullptr, screen);
    ::DestroyMenu(menu);
}
} // namespace

int main(int argc, char **argv) {
    if (argc > 1 && std::string{argv[1]} == "--preview") {
        preview_menu();
        return 0;
    }
    menu_metrics(96);
    menu_metrics(144);
    menu_metrics(192);
    return mirage::testing::finish("win32_tray_menu_test");
}
