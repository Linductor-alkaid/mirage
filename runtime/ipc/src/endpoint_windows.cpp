#include <mirage/runtime/ipc/endpoint.hpp>

// The Win32 surface is included macro-neutral (DEC-017 decision 5); the
// environment is read through the W surface because MSVC raises the CRT
// getenv as a warnings-as-errors deprecation (C4996) that MinGW never
// sees (the IDI_APPLICATION toolchain fact, recorded in M4-06).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <optional>
#include <string>

namespace mirage::runtime::ipc {
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
    if (value.empty()) {
        return {};
    }
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

} // namespace

std::string default_socket_path() {
    // DEC-007 on Windows: the named pipe namespace. The per-user name keeps
    // fast-user-switching sessions from colliding; %USERNAME% is the uid
    // analog here — a convenience disambiguator, not a security boundary
    // (pipe access control is the caller's DACL, the same division as the
    // POSIX 0700 directory).
    std::string user = environment_utf8(L"USERNAME");
    if (user.empty()) {
        user = "default";
    }
    return std::string("\\\\.\\pipe\\mirage-") + user + "-service";
}

std::string socket_directory(const std::string &socket_path) {
    // Pipes have no directory: the POSIX "create the socket directory
    // first" step has no Windows analog, and the bind path carries its own
    // namespace. Kept for interface parity; never called on this platform's
    // bind path.
    (void)socket_path;
    return {};
}

} // namespace mirage::runtime::ipc
