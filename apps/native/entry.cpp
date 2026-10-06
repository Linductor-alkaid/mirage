#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
int mirage_eui_main();
namespace mirage::native_ui {
std::uint64_t process_id();
}
namespace {
#ifndef _WIN32
// BUG-20261006-007 / DEC-045: a cached GNOME entry may name this binary directly.
// Replace this bootstrap process with the product launcher before any window
// initialization. A tray-owned child has an explicit socket and never forwards.
bool legacy_desktop_launch() {
    if (std::getenv("MIRAGE_NATIVE_SOCKET"))
        return false;
    const auto *entry = std::getenv("GIO_LAUNCHED_DESKTOP_FILE");
    return entry && std::filesystem::path(entry).filename() == "org.mirage.native.desktop";
}
int forward_desktop_launch() {
    std::error_code error;
    const auto self = std::filesystem::canonical("/proc/self/exe", error);
    if (!error) {
        const std::filesystem::path candidates[] = {self.parent_path() / "mirage",
                                                    self.parent_path().parent_path() / "mirage"};
        for (const auto &candidate : candidates) {
            error.clear();
            const auto launcher = std::filesystem::canonical(candidate, error);
            if (error || launcher == self || !std::filesystem::is_regular_file(launcher, error))
                continue;
            ::execl(launcher.c_str(), launcher.c_str(), "start", "--shell", self.c_str(),
                    static_cast<char *>(nullptr));
            break; // found a launcher, but exec failed; do not select a different build
        }
    }
    std::cerr << "Mirage: product launcher unavailable; rebuild/register the application entry\n";
    return 1;
}
#endif
} // namespace

int main() {
#ifndef _WIN32
    if (legacy_desktop_launch())
        return forward_desktop_launch();
#endif
    namespace ipc = mirage::runtime::ipc;
    const auto *override = std::getenv("MIRAGE_NATIVE_SOCKET");
    ipc::IpcClient client(override ? override : ipc::default_socket_path());
    const auto hello = client.call(ipc::HelloRequest{}, std::chrono::milliseconds{1500});
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    if (!hello.ok || !identity || !identity->tray.value_or(false)) {
        std::cerr
            << "Mirage: tray runtime is not running; open Mirage through its application entry\n";
        return 1; // before GLFW/display initialization, never flashes an orphan window
    }
    const auto status = client.call(ipc::ProductControlRequest{}, std::chrono::milliseconds{1500});
    const auto *product = std::get_if<ipc::ProductState>(&status.payload);
    if (!status.ok || !product ||
        product->frontend_pid != static_cast<std::int64_t>(mirage::native_ui::process_id())) {
        std::cerr << "Mirage: frontend must be launched by the tray; use mirage start\n";
        return 1;
    }
    return mirage_eui_main();
}
