#include "../../common/src/frontend_activation.hpp"
#include <windows.h>
namespace mirage::platform::detail {
bool activate_frontend(std::int64_t pid, std::string &diagnostic) {
    struct Search {
        DWORD pid;
        HWND target = nullptr;
        unsigned count = 0;
    } search{static_cast<DWORD>(pid)};
    ::EnumWindows(
        [](HWND window, LPARAM opaque) -> BOOL {
            auto &state = *reinterpret_cast<Search *>(opaque);
            if (++state.count > 256)
                return FALSE;
            DWORD owner = 0;
            ::GetWindowThreadProcessId(window, &owner);
            if (owner == state.pid && ::GetWindow(window, GW_OWNER) == nullptr) {
                state.target = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    if (!search.target) {
        if (search.count > 256) {
            diagnostic = "frontend activation exceeds 256 windows";
            return false;
        }
        return true; // child is still initializing
    }
    if (!::ShowWindowAsync(search.target, SW_RESTORE)) {
        diagnostic = "frontend restore request was refused";
        return false;
    }
    // Foreground-lock policy may refuse focus; the restore still reaches the
    // UI thread before its next paint and the existing UI show request retries.
    (void)::SetForegroundWindow(search.target);
    return true;
}
} // namespace mirage::platform::detail
