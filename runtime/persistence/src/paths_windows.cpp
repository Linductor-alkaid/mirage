#include <mirage/runtime/persistence/paths.hpp>

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

#include <filesystem>
#include <string>

namespace mirage::runtime::persistence {
namespace {

/// The wide environment variable's value in UTF-8; empty when unset or
/// empty (the callers treat both the same way).
std::string environment_utf8(const wchar_t *name) {
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

/// DEC-011 on Windows: the known user directories replace the XDG pair.
/// %APPDATA% (roaming) carries configuration, %LOCALAPPDATA% (machine
/// local) carries state, each mirroring the POSIX base/'mirage' layout.
/// An unset or empty variable falls back to the working directory so
/// callers always get something usable, exactly like the POSIX resolution.
std::filesystem::path known_directory(const wchar_t *environment_variable,
                                      const std::filesystem::path &suffix) {
    std::filesystem::path base;
    const std::string value = environment_utf8(environment_variable);
    if (!value.empty()) {
        base = value;
    } else {
        base = std::filesystem::path(".") / suffix;
    }
    return base / "mirage";
}

} // namespace

std::filesystem::path default_config_directory() { return known_directory(L"APPDATA", "config"); }

std::filesystem::path default_state_directory() {
    return known_directory(L"LOCALAPPDATA", "state");
}

} // namespace mirage::runtime::persistence
