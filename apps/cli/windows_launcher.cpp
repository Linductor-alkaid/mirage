// The installed application entry is a GUI process from its first instruction.
// Reuse the product control plane; the CLI retains its console subsystem.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format off: shellapi requires Win32 declarations first.
#include <shellapi.h>
// clang-format on

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int mirage_cli_main(int argc, char **argv);

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto **wide = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (!wide || count < 1) {
        if (wide)
            ::LocalFree(wide);
        ::MessageBoxW(nullptr, L"无法解析 Mirage 启动参数。", L"Mirage", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(count) + 1);
    for (int index = 0; index < count; ++index) {
        const int size =
            ::WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) {
            ::LocalFree(wide);
            return 1;
        }
        std::string value(static_cast<std::size_t>(size), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, value.data(), size, nullptr, nullptr);
        value.pop_back();
        arguments.push_back(std::move(value));
        if (index == 0)
            arguments.emplace_back("start");
    }
    ::LocalFree(wide);
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (auto &argument : arguments)
        argv.push_back(argument.data());
    const int argc = static_cast<int>(argv.size());
    argv.push_back(nullptr);
    std::ostringstream diagnostic;
    auto *previous = std::cerr.rdbuf(diagnostic.rdbuf());
    const int result = mirage_cli_main(argc, argv.data());
    std::cerr.rdbuf(previous);
    if (result != 0) {
        const auto message = diagnostic.str();
        const int size = ::MultiByteToWideChar(CP_UTF8, 0, message.data(),
                                               static_cast<int>(message.size()), nullptr, 0);
        std::wstring text(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
        if (size > 0)
            ::MultiByteToWideChar(CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
                                  text.data(), size);
        ::MessageBoxW(nullptr, text.empty() ? L"Mirage 启动失败。" : text.c_str(), L"Mirage",
                      MB_OK | MB_ICONERROR);
    }
    return result;
}
