#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/runtime/permission/confirmation_hub.hpp>
#include <mirage/runtime/permission/permission.hpp>
#include <mirage/runtime/persistence/paths.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/persistence/store.hpp>
#include <mirage/runtime/runtime_service.hpp>

// The reference topology binds the platform backend (DEC-008 item 3):
// the Linux desktop environment on Linux, the Windows one on Windows
// (M4-06; a service context reports no desktop surface and every
// desktop accessor stays null — fail closed).
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mirage/platform/windows/windows_desktop_environment.hpp>
#else
#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <fcntl.h>
#include <unistd.h>
#endif

#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::string_view kProgramName = "mirage-service";
constexpr std::string_view kVersion = MIRAGE_VERSION;

#ifdef _WIN32
// Windows shutdown: the console ctrl handler runs on its own thread, which
// is exactly the contract of RuntimeService::request_shutdown() (safe from
// any thread, never from a POSIX signal handler). The service is created
// after the handler is registered, so the pointer is only read afterwards.
mirage::runtime::RuntimeService *g_service = nullptr;

BOOL WINAPI on_console_ctrl(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT ||
        ctrl_type == CTRL_CLOSE_EVENT || ctrl_type == CTRL_SHUTDOWN_EVENT) {
        if (g_service != nullptr) {
            g_service->request_shutdown();
        }
        return TRUE;
    }
    return FALSE;
}
#else
// DEC-007: SIGINT/SIGTERM reach the service through a self-pipe registered
// with the IPC loop; the handler itself stays async-signal-safe (one write).
int g_signal_pipe_write = -1;

extern "C" void on_signal(int) {
    if (g_signal_pipe_write >= 0) {
        const char token = 's';
        const ssize_t written = ::write(g_signal_pipe_write, &token, 1);
        (void)written; // async-signal-safe best effort
    }
}
#endif

void print_usage(std::ostream &out) {
    out << "Usage: " << kProgramName << " [--socket PATH] [--read-root PATH]...\n"
        << "               [--perm CAPABILITY=allow|confirm|deny]...\n"
        << "               [--confirm allow|deny]\n"
        << "               [--config PATH] [--state-dir PATH] [--no-recovery]\n"
        << "\n"
        << "Hosts the Mirage background runtime service (design doc section\n"
        << "12): a pinned Mira instance bound to the local desktop\n"
        << "environment, serving the Local IPC surface (DEC-007). Runs in\n"
        << "the foreground until a service.shutdown IPC request or\n"
        << "SIGINT/SIGTERM.\n"
        << "\n"
        << "Options:\n"
        << "  --socket PATH     IPC endpoint (default: XDG runtime dir)\n"
        << "  --read-root PATH  Filesystem path tasks may read (repeatable;\n"
        << "                    without one, filesystem reads are denied)\n"
        << "  --perm RULE       Desktop permission rule (repeatable; RULE is\n"
        << "                    CAPABILITY=allow|confirm|deny with CAPABILITY\n"
        << "                    one of filesystem.read, filesystem.write,\n"
        << "                    process.execute; DEC-010)\n"
        << "  --confirm MODE    Outcome of confirmation requests (DEC-010\n"
        << "                    fail-closed hook): allow|deny|ipc. allow/deny\n"
        << "                    keep the in-process hook; ipc enables the M5\n"
        << "                    async confirmation surface (DEC-020): requests\n"
        << "                    broadcast as permission.request events, wait\n"
        << "                    --confirm-wait-ms for a permission.respond,\n"
        << "                    fail closed on timeout (default deny)\n"
        << "  --confirm-wait-ms N\n"
        << "                    Wait budget for --confirm ipc confirmations\n"
        << "                    (default 120000)\n"
        << "  --config PATH     Load a settings file first (DEC-011); flags\n"
        << "                    below override it item by item\n"
        << "  --state-dir PATH  Directory of the task recovery file (M1-07,\n"
        << "                    default: XDG state dir)\n"
        << "  --no-recovery     Keep the task registry memory-only\n"
        << "  --version         Print versions\n"
        << "  --help            Print this help\n";
}

void print_version() {
    std::cout << kProgramName << ' ' << kVersion << '\n';
    const mirage::runtime::MiraCoreVersion core = mirage::runtime::mira_core_version();
    std::cout << "mira core " << core.major << '.' << core.minor << '.' << core.patch << '\n';
}

/// Parses CAPABILITY=allow|confirm|deny; nullopt (with a message printed)
/// on anything else.
std::optional<std::pair<mirage::runtime::permission::Capability, mirage::runtime::permission::Rule>>
parse_perm_rule(std::string_view rule) {
    const auto equals = rule.find('=');
    if (equals == std::string_view::npos) {
        return std::nullopt;
    }
    const auto capability =
        mirage::runtime::permission::capability_from_name(rule.substr(0, equals));
    const auto mode = mirage::runtime::permission::rule_from_name(rule.substr(equals + 1));
    if (!capability || !mode) {
        return std::nullopt;
    }
    return std::make_pair(*capability, *mode);
}

/// Strict decimal integer parse (no sign, no whitespace, no overflow).
bool parse_long(std::string_view text, long &value) {
    if (text.empty()) {
        return false;
    }
    long parsed = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        if (parsed > (std::numeric_limits<long>::max() - (character - '0')) / 10) {
            return false;
        }
        parsed = parsed * 10 + (character - '0');
    }
    value = parsed;
    return true;
}

} // namespace

int main(int argc, char **argv) {
    mirage::runtime::ServiceConfig config;
    config.mirage_version = std::string(kVersion);
    std::vector<std::string> read_roots;
    std::vector<std::string> perm_flags;
    std::optional<std::string> flag_socket;
    std::optional<std::string> flag_confirm;
    long confirm_wait_ms = 120000;
    bool read_roots_from_flags = false;
    std::optional<std::filesystem::path> config_file;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help") {
            print_usage(std::cout);
            return 0;
        }
        if (argument == "--version") {
            print_version();
            return 0;
        }
        if (argument == "--socket" && index + 1 < argc) {
            flag_socket = argv[++index];
            continue;
        }
        if (argument == "--read-root" && index + 1 < argc) {
            read_roots.emplace_back(argv[++index]);
            read_roots_from_flags = true;
            continue;
        }
        if (argument == "--config" && index + 1 < argc) {
            config_file = argv[++index];
            continue;
        }
        if (argument == "--state-dir" && index + 1 < argc) {
            config.recovery_directory = argv[++index];
            continue;
        }
        if (argument == "--no-recovery") {
            config.persist_recovery_state = false;
            continue;
        }
        if (argument == "--perm" && index + 1 < argc) {
            const std::string_view rule{argv[++index]};
            if (!parse_perm_rule(rule)) {
                std::cerr << kProgramName
                          << ": --perm expects "
                             "CAPABILITY=allow|confirm|deny (got '"
                          << rule << "')\n";
                print_usage(std::cerr);
                return 2;
            }
            perm_flags.emplace_back(rule);
            continue;
        }
        if (argument == "--confirm" && index + 1 < argc) {
            const std::string_view mode{argv[++index]};
            if (mode != "allow" && mode != "deny" && mode != "ipc") {
                std::cerr << kProgramName << ": --confirm expects allow|deny|ipc (got '" << mode
                          << "')\n";
                print_usage(std::cerr);
                return 2;
            }
            flag_confirm = std::string(mode);
            continue;
        }
        if (argument == "--confirm-wait-ms" && index + 1 < argc) {
            const std::string_view value{argv[++index]};
            long parsed_wait = 0;
            if (!parse_long(value, parsed_wait) || parsed_wait <= 0) {
                std::cerr << kProgramName << ": --confirm-wait-ms expects a positive integer (got '"
                          << value << "')\n";
                print_usage(std::cerr);
                return 2;
            }
            confirm_wait_ms = parsed_wait;
            continue;
        }
        std::cerr << kProgramName << ": unknown argument '" << argument << "'\n";
        print_usage(std::cerr);
        return 2;
    }

    // M1-07 DEC-011 configuration: the settings file (when requested)
    // provides the baseline, command-line flags override item by item. A
    // settings file that exists but cannot be trusted fails closed.
    if (config_file) {
        const std::filesystem::path parent = config_file->parent_path().empty()
                                                 ? std::filesystem::path(".")
                                                 : config_file->parent_path();
        const mirage::runtime::persistence::LocalStateStore store(
            parent, config_file->filename().string(),
            mirage::runtime::persistence::kMaxSettingsFileBytes);
        const auto loaded = store.load();
        if (loaded.status == mirage::runtime::persistence::LoadStatus::IoError) {
            std::cerr << kProgramName << ": cannot read config file '" << config_file->string()
                      << "': " << loaded.error << '\n';
            return 1;
        }
        if (loaded.status == mirage::runtime::persistence::LoadStatus::TooLarge) {
            std::cerr << kProgramName << ": config file '" << config_file->string()
                      << "' exceeds the byte budget\n";
            return 1;
        }
        if (loaded.status == mirage::runtime::persistence::LoadStatus::Loaded) {
            const auto decoded = mirage::runtime::persistence::decode_settings(loaded.body);
            if (!decoded.ok) {
                std::cerr << kProgramName << ": invalid config file '" << config_file->string()
                          << "': " << decoded.error << '\n';
                return 1;
            }
            const auto &settings = decoded.settings;
            if (!settings.socket_path.empty()) {
                config.socket_path = settings.socket_path;
            }
            if (!settings.read_roots.empty() && !read_roots_from_flags) {
                read_roots = settings.read_roots;
            }
            const auto apply_rule = [&](const std::optional<std::string> &text,
                                        mirage::runtime::permission::Capability capability) {
                if (!text) {
                    return;
                }
                const auto rule = mirage::runtime::permission::rule_from_name(*text);
                if (rule) {
                    config.permission_policy.rules[static_cast<std::size_t>(capability)] = *rule;
                }
            };
            apply_rule(settings.filesystem_read_rule,
                       mirage::runtime::permission::Capability::FilesystemRead);
            apply_rule(settings.filesystem_write_rule,
                       mirage::runtime::permission::Capability::FilesystemWrite);
            apply_rule(settings.process_execute_rule,
                       mirage::runtime::permission::Capability::ProcessExecute);
            if (settings.confirmation) {
                flag_confirm = *settings.confirmation;
            }
        }
    }
    if (flag_socket) {
        config.socket_path = *flag_socket;
    }
    for (const std::string &rule : perm_flags) {
        if (const auto parsed = parse_perm_rule(rule)) {
            config.permission_policy.rules[static_cast<std::size_t>(parsed->first)] =
                parsed->second;
        }
    }
    if (flag_confirm == "allow") {
        config.confirmation = std::make_shared<mirage::runtime::permission::AllowAllConfirmation>();
    } else if (flag_confirm == "deny") {
        // The service default is already fail closed (DEC-010); the
        // explicit choice documents itself.
        config.confirmation = nullptr;
    } else if (flag_confirm == "ipc") {
        // DEC-020: the M5 async confirmation surface — requests broadcast to
        // subscribed IPC clients, bounded wait, fail closed on timeout.
        config.confirmation_hub =
            std::make_shared<mirage::runtime::permission::AsyncConfirmationHub>(
                std::chrono::milliseconds(confirm_wait_ms));
    }

    // The M1 reference topology binds the Linux backend (DEC-008 item 3).
    // The M1-05 read scope is declared here: only paths at or beneath a
    // --read-root are readable, and an empty scope denies every read. The
    // M1-06 permission gate (DEC-010) sits in front of every desktop
    // action with the policy declared via --perm.
#ifdef _WIN32
    if (!read_roots.empty()) {
        // The Linux backend owns the filesystem.read scope; the Windows
        // backend reports no filesystem capability at all, so the flag
        // would be silently meaningless — refused instead.
        std::cerr << kProgramName << ": --read-root is a Linux backend surface\n";
        return 2;
    }
    auto environment =
        std::make_shared<mirage::platform::windows_backend::WindowsDesktopEnvironment>();
#else
    std::vector<std::filesystem::path> read_scope;
    read_scope.reserve(read_roots.size());
    for (const std::string &root : read_roots) {
        read_scope.emplace_back(root);
    }
    auto environment = std::make_shared<mirage::platform::linux_backend::LinuxDesktopEnvironment>(
        std::move(read_scope));
#endif
    auto binding = std::make_shared<mirage::integration::MiraEnvironmentBinding>(environment);

    mirage::runtime::RuntimeService service(config);
#ifdef _WIN32
    g_service = &service;
    ::SetConsoleCtrlHandler(on_console_ctrl, TRUE);
#else
    ::signal(SIGPIPE, SIG_IGN);
    int signal_pipe[2] = {-1, -1};
    // Non-blocking: the loop drains it only on a poll event, and the signal
    // handler's single-byte write must never block either.
    if (::pipe2(signal_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        std::cerr << kProgramName << ": cannot create the signal pipe\n";
        return 1;
    }
    g_signal_pipe_write = signal_pipe[1];
    struct sigaction action{};
    action.sa_handler = on_signal;
    ::sigemptyset(&action.sa_mask);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);
    service.register_shutdown_fd(signal_pipe[0]);
#endif

    const mirage::runtime::HostOutcome started = service.start(binding);
    if (!started.ok) {
        std::cerr << kProgramName << ": start failed (" << started.error.code
                  << "): " << started.error.message << '\n';
        return 1;
    }
    std::cout << kProgramName << " serving at " << service.socket_path() << '\n'
              << "filesystem read scope: " << read_roots.size() << " root(s)\n";
    {
        const auto &rules = config.permission_policy.rules;
        std::cout << "permission policy:" << " filesystem.read="
                  << mirage::runtime::permission::rule_name(rules[0])
                  << " filesystem.write=" << mirage::runtime::permission::rule_name(rules[1])
                  << " process.execute=" << mirage::runtime::permission::rule_name(rules[2])
                  << "; confirmations: ";
        if (config.confirmation_hub != nullptr) {
            std::cout << "async ipc (wait " << config.confirmation_hub->wait_budget().count()
                      << " ms)";
        } else {
            std::cout << (config.confirmation ? "allow" : "deny");
        }
        std::cout << '\n';
    }
    std::cout << "recovery state: ";
    if (config.persist_recovery_state) {
        const std::filesystem::path directory =
            config.recovery_directory.empty()
                ? mirage::runtime::persistence::default_state_directory()
                : config.recovery_directory;
        std::cout << (directory / "task-recovery.json").string() << '\n';
    } else {
        std::cout << "off\n";
    }
    std::cout << std::flush;

    const mirage::runtime::ServiceRunReport report = service.run();
    if (!report.clean) {
        std::cerr << kProgramName << ": shutdown not clean: " << report.diagnostic << '\n';
        return 1;
    }
    std::cout << kProgramName << " stopped cleanly\n";
    return 0;
}
