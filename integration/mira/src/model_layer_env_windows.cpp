#include "model_layer_env.hpp"

// REUSE-12: this is the integration layer's own env face (narrow name widened
// to the W surface); runtime modules share runtime/internal/win32_environment.hpp
// instead, and the two cannot merge because runtime depends on integration,
// not the reverse. Same toolchain rationale as that header.
// The Win32 surface is included macro-neutral (DEC-017 decision 5); the
// environment is read through the W surface because MSVC raises the CRT
// getenv as a warnings-as-errors deprecation (C4996) that MinGW never sees
// (the same toolchain fact runtime/persistence records for its path
// resolution, M4-06).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mirage::integration::detail {

std::string read_environment_value(const std::string &name) {
    const int wide_length =
        ::MultiByteToWideChar(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), nullptr, 0);
    if (wide_length <= 0) {
        return {};
    }
    std::wstring wide_name(static_cast<std::size_t>(wide_length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), wide_name.data(),
                          wide_length);
    DWORD size = ::GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    size = ::GetEnvironmentVariableW(wide_name.c_str(), value.data(), size);
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

} // namespace mirage::integration::detail
