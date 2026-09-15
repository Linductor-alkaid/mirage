#include <mirage/integration/mirador_service.hpp>
#include <mirage/platform/platform_info.hpp>
#include <mirage/runtime/mira_host.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view kProgramName = "mirage";
constexpr std::string_view kVersion = MIRAGE_VERSION;

void print_usage(std::ostream& out) {
    out << "Usage: " << kProgramName << " <command>\n"
        << "\n"
        << "Mirage is the Linux/Windows desktop host for the Mira agent runtime.\n"
        << "\n"
        << "Commands:\n"
        << "  --version    Print Mirage, Mira core and platform versions\n"
        << "  --help       Print this help\n"
        << "\n"
        << "Task commands (mirage task list / inspect / pause, mirage workflow run,\n"
        << "mirage agent run) arrive with the M1 runtime service.\n";
}

void print_version() {
    std::cout << kProgramName << ' ' << kVersion << '\n';

    const mirage::runtime::MiraCoreVersion core = mirage::runtime::mira_core_version();
    std::cout << "mira core " << core.major << '.' << core.minor << '.' << core.patch << '\n';

    std::cout << "platform " << mirage::platform::platform_info().os << '\n';

    const mirage::runtime::ServiceInfo service = mirage::runtime::runtime_service_info();
    std::cout << "runtime service " << service.name << " (background: "
              << (service.background_capable ? "yes" : "no") << ")\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string_view command{argv[1]};
        if (command == "--version") {
            print_version();
            return 0;
        }
        if (command == "--help") {
            print_usage(std::cout);
            return 0;
        }
        std::cerr << kProgramName << ": unknown command '" << command << "'\n\n";
        print_usage(std::cerr);
        return 2;
    }
    print_usage(std::cout);
    return 0;
}
