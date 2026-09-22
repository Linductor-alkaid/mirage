#pragma once

// Shared private helpers of the Win32 frontends (win32_backend, uia_backend).
// Lives in src/ so no Win32 type ever appears in a public include path
// (RULE-01); every function is inline and every Win32 type stays inside the
// frontends' translation units.
//
// Macro discipline (DEC-017 decision 5): the Win32 surface is included once
// from here, minimal and macro-neutral; all string APIs use the explicit W
// variants regardless of the UNICODE default.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <mirage/desktop/provider_error.hpp>

namespace mirage::platform::windows_backend::win32_util {

/// Owning kernel HANDLE (files, pipes, processes, jobs). CloseHandle on
/// every release; never wraps pseudo-handles that must not be closed.
struct HandleCloser {
    void operator()(void *handle) const {
        if (handle != nullptr) {
            ::CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

inline mirage::desktop::ProviderError error(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

/// Last-error detail attached to "io_error" messages, so failures stay
/// diagnosable from logs without echoing window content.
inline std::string last_error_suffix() {
    const DWORD code = ::GetLastError();
    return " (Win32 error " + std::to_string(static_cast<unsigned long>(code)) + ")";
}

inline std::string utf16_to_utf8(const std::wstring &text) {
    if (text.empty()) {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    const int written = ::WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

/// Narrow conversion for provider payloads (type_text / set_text contract is
/// UTF-8); malformed UTF-8 fails the call instead of being silently repaired
/// (DEC-017 decision 5, MB_ERR_INVALID_CHARS).
inline std::optional<std::wstring> utf8_to_utf16(const std::string &text) {
    if (text.empty()) {
        return std::wstring{};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return std::nullopt;
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                              static_cast<int>(text.size()), out.data(), size);
    if (written <= 0) {
        return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

/// Window ids are the decimal form of the HWND value (DEC-017 decision 7,
/// same shape as the X11 backend's `std::to_string(Window)`).
inline std::string format_window_id(HWND window) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(window));
}

inline std::optional<HWND> parse_window_id(const std::string &id) {
    // 19 digits: every value that fits uint64_t parses without throwing, and
    // real HWNDs are far below that (handles are 32-bit significant); the
    // provider reports errors, never exceptions.
    if (id.empty() || id.size() > 19) {
        return std::nullopt;
    }
    for (const char ch : id) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
    }
    const unsigned long long value = std::stoull(id);
    if (value == 0) {
        return std::nullopt;
    }
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
}

} // namespace mirage::platform::windows_backend::win32_util
