#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
int mirage_eui_main();
namespace mirage::native_ui {
std::uint64_t process_id();
}
int main() {
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
