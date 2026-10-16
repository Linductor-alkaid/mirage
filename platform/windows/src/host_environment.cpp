#include <mirage/platform/host_environment.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mirage::platform {
namespace {

// The DPI awareness entries postdate the WINVER floor some toolchains'
// winuser.h carries, so they resolve dynamically (the same choice the
// capture tier test makes); the context is the documented winuser.h
// pseudo-handle and the awareness value the documented DPI_AWARENESS enum,
// which keeps this unit independent of the header's declaration guards.
using SetProcessDpiAwarenessContextFn = BOOL(WINAPI *)(void *);
using SetThreadDpiAwarenessContextFn = void *(WINAPI *)(void *);
using GetThreadDpiAwarenessContextFn = void *(WINAPI *)();
using GetAwarenessFromContextFn = unsigned long(WINAPI *)(void *);
// reinterpret_cast is not a constant expression, so the context stays a
// runtime constant: ..._CONTEXT_PER_MONITOR_AWARE_V2.
void *kPerMonitorV2Context = reinterpret_cast<void *>(-4);
// The DPI_AWARENESS enum has no v2 value: the per-monitor context flavors
// (v1/v2) both report DPI_AWARENESS_PER_MONITOR_AWARE.
constexpr unsigned long kPerMonitorAwareness = 2;

void *user32_entry(const char *name) {
    const HMODULE user32 = ::GetModuleHandleW(L"user32");
    return user32 == nullptr ? nullptr : reinterpret_cast<void *>(::GetProcAddress(user32, name));
}

} // namespace

DpiAwareness declare_host_dpi_awareness() {
    const auto thread_context = reinterpret_cast<GetThreadDpiAwarenessContextFn>(
        user32_entry("GetThreadDpiAwarenessContext"));
    const auto awareness_from = reinterpret_cast<GetAwarenessFromContextFn>(
        user32_entry("GetAwarenessFromDpiAwarenessContext"));
    const auto set_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
        user32_entry("SetProcessDpiAwarenessContext"));
    const auto set_thread_context = reinterpret_cast<SetThreadDpiAwarenessContextFn>(
        user32_entry("SetThreadDpiAwarenessContext"));
    // A thread that has not called SetThreadDpiAwarenessContext carries the
    // process context it was created with, so this reads what the executable
    // image (manifest) or an earlier call already fixed.
    if (thread_context != nullptr && awareness_from != nullptr &&
        awareness_from(thread_context()) == kPerMonitorAwareness) {
        return DpiAwareness::prior_per_monitor;
    }
    if (set_context != nullptr && set_context(kPerMonitorV2Context) != 0) {
        // SetProcessDpiAwarenessContext does not document whether threads
        // that already exist adopt the new context, and the calling thread
        // is the one that later opens windows and enumerates the desktop —
        // align it explicitly.
        if (set_thread_context != nullptr) {
            set_thread_context(kPerMonitorV2Context);
        }
        return DpiAwareness::per_monitor_v2;
    }
    if (set_context != nullptr) {
        // The modern entry exists but refused (E_ACCESSDENIED: the manifest
        // or an earlier call fixed the awareness to a mode this call cannot
        // raise). The legacy entry's return value is not trustworthy once
        // the modern entry exists (it reports success with ERROR_ACCESS_
        // DENIED outstanding), so report instead of pretending.
        return DpiAwareness::unchanged;
    }
    // Pre-1607 Windows without the modern entry: only the Vista-era
    // declaration remains, and its return value means what it says there.
    if (::SetProcessDPIAware() != 0) {
        return DpiAwareness::system;
    }
    return DpiAwareness::unchanged;
}

} // namespace mirage::platform
