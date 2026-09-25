// POSIX entry of the Mirage desktop shell (M5-02): parses the launch flags,
// then hands over to the shared shell lifecycle in shell_main.cpp.
//
//   mirage-desktop [--socket PATH] [--ui-root DIR]

#include <cstring>
#include <string>

#include "include/cef_app.h"
#include "include/internal/cef_types_linux.h"

#include <mirage/runtime/ipc/endpoint.hpp>

#include "shell_main.hpp"

int main(int argc, char *argv[]) {
    mirage::desktop_shell::ShellOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--socket" && index + 1 < argc) {
            options.socket_address = argv[++index];
        } else if (argument == "--ui-root" && index + 1 < argc) {
            options.ui_root = argv[++index];
        } else if (argument == "--help") {
            std::fprintf(stderr, "usage: mirage-desktop [--socket PATH] [--ui-root DIR]\n");
            return 0;
        }
    }
    if (options.socket_address.empty()) {
        options.socket_address = mirage::runtime::ipc::default_socket_path();
    }
    if (options.ui_root.empty()) {
        options.ui_root = MIRAGE_UI_APP_DIST;
    }
    return mirage::desktop_shell::run_shell(CefMainArgs(argc, argv),
                                            /*sandbox_info=*/nullptr, options);
}
