#pragma once

#include "win32_util.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace mirage::platform::windows_backend {

// Lives on the existing tray pump for the duration of TrackPopupMenuEx.
// Native HMENU retains keyboard navigation, dismissal and disabled actions.
class TrayMenuPresentation {
  public:
    explicit TrayMenuPresentation(UINT dpi) : dpi_(dpi ? dpi : 96) {
        font_ = ::CreateFontW(-scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Segoe UI");
    }
    ~TrayMenuPresentation() {
        if (font_)
            ::DeleteObject(font_);
    }
    TrayMenuPresentation(const TrayMenuPresentation &) = delete;
    TrayMenuPresentation &operator=(const TrayMenuPresentation &) = delete;

    bool append(HMENU menu, UINT id, std::wstring label, bool enabled) {
        if (count_ == items_.size())
            return false;
        auto &item = items_[count_++];
        item = std::move(label);
        MENUITEMINFOW description{};
        description.cbSize = sizeof(description);
        description.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_DATA | MIIM_STRING;
        description.wID = id;
        description.fState = enabled ? MFS_ENABLED : MFS_DISABLED;
        description.fType = font_ ? MFT_OWNERDRAW : MFT_STRING;
        description.dwItemData = reinterpret_cast<ULONG_PTR>(&item);
        description.dwTypeData = item.data();
        description.cch = static_cast<UINT>(item.size());
        return ::InsertMenuItemW(menu, static_cast<UINT>(::GetMenuItemCount(menu)), TRUE,
                                 &description) != 0;
    }

    bool measure(MEASUREITEMSTRUCT &value) const {
        const auto *text = label(value.CtlType, value.itemData);
        if (!text || !font_)
            return false;
        HDC dc = ::GetDC(nullptr);
        if (!dc)
            return false;
        const auto previous = ::SelectObject(dc, font_);
        SIZE size{};
        ::GetTextExtentPoint32W(dc, text->c_str(), static_cast<int>(text->size()), &size);
        ::SelectObject(dc, previous);
        ::ReleaseDC(nullptr, dc);
        value.itemWidth = static_cast<UINT>(std::max<LONG>(scale(220), size.cx + scale(40)));
        value.itemHeight = static_cast<UINT>(std::max<LONG>(scale(40), size.cy + scale(16)));
        return true;
    }

    bool draw(const DRAWITEMSTRUCT &value) const {
        const auto *text = label(value.CtlType, value.itemData);
        if (!text || !font_)
            return false;
        const bool disabled = (value.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
        const bool selected = (value.itemState & ODS_SELECTED) != 0 && !disabled;
        const int saved = ::SaveDC(value.hDC);
        if (!saved)
            return false;
        ::FillRect(value.hDC, &value.rcItem,
                   ::GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
        ::SelectObject(value.hDC, font_);
        ::SetBkMode(value.hDC, TRANSPARENT);
        ::SetTextColor(value.hDC, ::GetSysColor(disabled   ? COLOR_GRAYTEXT
                                                : selected ? COLOR_HIGHLIGHTTEXT
                                                           : COLOR_MENUTEXT));
        RECT rect = value.rcItem;
        rect.left += scale(20);
        rect.right -= scale(20);
        ::DrawTextW(value.hDC, text->c_str(), static_cast<int>(text->size()), &rect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if ((value.itemState & ODS_FOCUS) != 0 && (value.itemState & ODS_NOFOCUSRECT) == 0)
            ::DrawFocusRect(value.hDC, &rect);
        ::RestoreDC(value.hDC, saved);
        return true;
    }

  private:
    int scale(int value) const { return ::MulDiv(value, static_cast<int>(dpi_), 96); }
    const std::wstring *label(UINT type, ULONG_PTR data) const {
        if (type == ODT_MENU)
            for (std::size_t index = 0; index < count_; ++index)
                if (data == reinterpret_cast<ULONG_PTR>(&items_[index]))
                    return &items_[index];
        return nullptr;
    }
    UINT dpi_;
    HFONT font_ = nullptr;
    std::array<std::wstring, 3> items_;
    std::size_t count_ = 0;
};

} // namespace mirage::platform::windows_backend
