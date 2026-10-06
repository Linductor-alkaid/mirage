#include "frontend_activation.hpp"
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <mirage/platform/frontend_process.hpp>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace mirage::platform {
namespace {
class OwnedFrontend final : public desktop::FrontendProcess {
  public:
    explicit OwnedFrontend(std::string binary) : binary_(std::move(binary)) {}
    ~OwnedFrontend() override {
        std::string diagnostic;
        (void)stop(diagnostic);
    }
    bool open(const std::string &endpoint, std::string &diagnostic) override {
        // BUG-20261006-006: activation must work while the UI paint loop is suspended.
        if (const auto child = pid(); child != 0)
            return detail::activate_frontend(child, diagnostic);
        if (binary_.empty() || !std::filesystem::is_regular_file(binary_)) {
            diagnostic = "frontend executable is missing";
            return false;
        }
#ifdef _WIN32
        const auto wide = [](const std::string &text) {
            const int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                   static_cast<int>(text.size()), nullptr, 0);
            std::wstring value(static_cast<std::size_t>(size), L'\0');
            if (size > 0)
                ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                      static_cast<int>(text.size()), value.data(), size);
            return value;
        };
        const auto executable = wide(binary_);
        if (executable.empty()) {
            diagnostic = "invalid UTF-8 executable path";
            return false;
        }
        std::wstring command = L"\"" + executable + L"\"";
        std::vector<wchar_t> environment;
        wchar_t *inherited = ::GetEnvironmentStringsW();
        if (!inherited) {
            diagnostic = "cannot read frontend environment";
            return false;
        }
        for (const wchar_t *entry = inherited; *entry; entry += std::wcslen(entry) + 1) {
            const std::wstring value(entry);
            if (value.starts_with(L"MIRAGE_NATIVE_SOCKET="))
                continue;
            environment.insert(environment.end(), value.begin(), value.end());
            environment.push_back(L'\0');
        }
        ::FreeEnvironmentStringsW(inherited);
        const auto socket = L"MIRAGE_NATIVE_SOCKET=" + wide(endpoint);
        environment.insert(environment.end(), socket.begin(), socket.end());
        environment.push_back(L'\0');
        environment.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                              CREATE_UNICODE_ENVIRONMENT, environment.data(), nullptr, &startup,
                              &child)) {
            diagnostic = "frontend CreateProcess failed: " + std::to_string(::GetLastError());
            return false;
        }
        ::CloseHandle(child.hThread);
        handle_ = child.hProcess;
        pid_ = child.dwProcessId;
#else
        // posix_spawn avoids executing C++ or setenv in a forked multi-threaded host.
        std::vector<std::string> values;
        for (char **entry = environ; *entry; ++entry)
            if (!std::string_view(*entry).starts_with("MIRAGE_NATIVE_SOCKET="))
                values.emplace_back(*entry);
        values.push_back("MIRAGE_NATIVE_SOCKET=" + endpoint);
        std::vector<char *> environment;
        for (auto &value : values)
            environment.push_back(value.data());
        environment.push_back(nullptr);
        char *arguments[] = {binary_.data(), nullptr};
        pid_t child = 0;
        const int code =
            ::posix_spawn(&child, binary_.c_str(), nullptr, nullptr, arguments, environment.data());
        if (code != 0) {
            diagnostic = "frontend spawn failed: " + std::string(std::strerror(code));
            return false;
        }
        pid_ = child;
#endif
        return true;
    }
    std::int64_t pid() override {
#ifdef _WIN32
        if (handle_) {
            DWORD code = 0;
            if (!::GetExitCodeProcess(handle_, &code) || code != STILL_ACTIVE) {
                ::CloseHandle(handle_);
                handle_ = nullptr;
                pid_ = 0;
            }
        }
#else
        if (pid_ != 0) {
            const auto result = ::waitpid(pid_, nullptr, WNOHANG);
            if (result == pid_ || (result < 0 && errno == ECHILD))
                pid_ = 0;
        }
#endif
        return static_cast<std::int64_t>(pid_);
    }
    bool stop(std::string &diagnostic) override {
        // IPC closure lets the frontend complete its normal UI teardown first.
        const auto wait = [this](std::chrono::milliseconds budget) {
            const auto deadline = std::chrono::steady_clock::now() + budget;
            while (pid() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            return pid() == 0;
        };
        if (wait(std::chrono::milliseconds{500}))
            return true;
#ifdef _WIN32
        (void)::TerminateProcess(handle_, 1);
#else
        (void)::kill(pid_, SIGTERM);
#endif
        if (wait(std::chrono::milliseconds{1000}))
            return true;
#ifndef _WIN32
        (void)::kill(pid_, SIGKILL);
        if (wait(std::chrono::milliseconds{1000}))
            return true;
#endif
        diagnostic = "frontend did not exit within shutdown budget";
        return false;
    }

  private:
    std::string binary_;
#ifdef _WIN32
    HANDLE handle_ = nullptr;
    DWORD pid_ = 0;
#else
    pid_t pid_ = 0;
#endif
};
} // namespace
std::string current_executable_path() {
#ifdef _WIN32
    std::vector<wchar_t> path(32768);
    const DWORD length =
        ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(length),
                                           nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(length), result.data(),
                              size, nullptr, nullptr);
    return result;
#else
    char path[4096];
    const auto length = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    return length > 0 ? std::string(path, static_cast<std::size_t>(length)) : std::string{};
#endif
}
std::shared_ptr<desktop::FrontendProcess> make_frontend_process(std::string binary) {
    return std::make_shared<OwnedFrontend>(std::move(binary));
}
} // namespace mirage::platform
