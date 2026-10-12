#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mirage/platform/frontend_process.hpp>
#include <mirage/platform/pet_window.hpp>
#include <mirage/runtime/ipc/client.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int mirage_pet_eui_main();
int main() {
    std::string exit_diagnostic;
    if (!mirage::platform::pet_window::install_exit_request(exit_diagnostic)) {
        std::cerr << "mira_pet: " << exit_diagnostic << '\n';
        return 1;
    }
    const char *endpoint = std::getenv("MIRAGE_PET_SOCKET");
    if (!endpoint || !*endpoint) {
        std::cerr << "mira_pet: tray-owned socket is required\n";
        return 1;
    }
    mirage::runtime::ipc::IpcClient client(endpoint);
    const auto hello =
        client.call(mirage::runtime::ipc::HelloRequest{}, std::chrono::milliseconds{1500});
    const auto *identity = std::get_if<mirage::runtime::ipc::ServiceIdentity>(&hello.payload);
    if (!hello.ok || !identity || !identity->tray.value_or(false) ||
        !identity->pet_status.value_or(false)) {
        std::cerr << "mira_pet: compatible tray runtime unavailable\n";
        return 1;
    }
    const auto product =
        client.call(mirage::runtime::ipc::ProductControlRequest{}, std::chrono::milliseconds{1500});
    const auto *state = std::get_if<mirage::runtime::ipc::ProductState>(&product.payload);
#ifdef _WIN32
    const auto self = static_cast<std::int64_t>(::GetCurrentProcessId());
#else
    const auto self = static_cast<std::int64_t>(::getpid());
#endif
    if (!product.ok || !state || !state->pet_pid || *state->pet_pid != self) {
        std::cerr << "mira_pet: process is not owned by this tray\n";
        return 1;
    }
    // Child-local resource root: the tray may have been launched from any cwd.
    const auto executable = mirage::platform::current_executable_path();
    std::error_code error;
    std::filesystem::current_path(std::filesystem::path(executable).parent_path(), error);
    if (executable.empty() || error) {
        std::cerr << "mira_pet: cannot locate installed assets\n";
        return 1;
    }
    return mirage_pet_eui_main();
}
