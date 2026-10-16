#pragma once

// Runtime-internal Win32 helpers shared across runtime modules (REUSE-12).
// Windows-only: included exclusively from *_windows.cpp translation units.

#ifndef _WIN32
#error "win32_environment.hpp is Windows-only"
#endif

// The Win32 surface is included macro-neutral (DEC-017 decision 5); the
// environment is read through the W surface because MSVC raises the CRT
// getenv as a warnings-as-errors deprecation (C4996) that MinGW never sees
// (the IDI_APPLICATION toolchain fact, recorded in M4-06).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace mirage::runtime::internal {

/// The wide environment variable's value in UTF-8; empty when unset or
/// empty (the callers treat both the same way).
inline std::string environment_utf8(const wchar_t *name) {
    DWORD size = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    size = ::GetEnvironmentVariableW(name, value.data(), size);
    if (size == 0) {
        return {};
    }
    value.resize(size);
    const int bytes = ::WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(),
                          bytes, nullptr, nullptr);
    return out;
}

} // namespace mirage::runtime::internal
