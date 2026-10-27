#include "host.hpp"
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/credential_store.hpp>
#include <mirage/platform/frontend_process.hpp>
#include <mirage/platform/host_environment.hpp>
#include <mirage/runtime/permission/confirmation_hub.hpp>
#include <mirage/runtime/permission/permission.hpp>
#include <mirage/runtime/persistence/paths.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/persistence/store.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <charconv>

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
        << "               [--model-endpoint ORIGIN] [--model-selector ALIAS]\n"
        << "               [--model-api-prefix PREFIX] [--model-dialect DIALECT]\n"
        << "               [--model-credential-env VAR] [--model-display-name NAME]\n"
        << "               [--overlay on|debug]\n"
        << "\n"
        << "Hosts the Mirage background runtime service (design doc section\n"
        << "12): a pinned Mira instance bound to the local desktop\n"
        << "environment, serving the Local IPC surface (DEC-007). Runs in\n"
        << "the foreground until a service.shutdown IPC request or\n"
        << "SIGINT/SIGTERM.\n"
        << "\n"
        << "Options:\n"
        << "  --ocr-manifest PATH  Verified local PP-OCR resources (opt-in build)\n"
        << "  --perception-period-ms N  Continuous sampling period 0 or 100..1000\n"
        << "                    (default 250ms; DEC-118)\n"
        << "  --socket PATH     IPC endpoint (default: XDG runtime dir)\n"
        << "  --read-root PATH  Filesystem path tasks may read (repeatable;\n"
        << "                    without one, filesystem reads are denied)\n"
        << "  --enable-basic-tools\n"
        << "                    Expose the exec/read/write/edit session tools\n"
        << "                    (DEC-095). read honors --read-root; write/edit\n"
        << "                    need --write-root; exec additionally needs\n"
        << "                    --enable-exec and a workspace\n"
        << "  --enable-exec      Enable the exec tool (implies\n"
        << "                    --enable-basic-tools); commands run with the\n"
        << "                    host user's permissions in the workspace — no\n"
        << "                    sandbox\n"
        << "  --workspace PATH   Session workspace directory (absolute);\n"
        << "                    relative tool paths and exec resolve against it\n"
        << "  --write-root PATH  Filesystem path tools may modify (repeatable;\n"
        << "                    read roots never imply write authority)\n"
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
        << "  --model-*         Model layer configuration (DEC-027). --model-endpoint\n"
        << "                    enables the dialog face (session.chat); the endpoint is\n"
        << "                    the fixed provider origin, the credential env var is read\n"
        << "                    at the transport boundary only. Default: disabled.\n"
        << "  --confirm-wait-ms N\n"
        << "                    Wait budget for --confirm ipc confirmations\n"
        << "                    (default 120000)\n"
        << "  --overlay MODE    Desktop Overlay surface (M5-09, DEC-029): off\n"
        << "                    (default) keeps the desktop clear; on mirrors\n"
        << "                    task actions, target highlights and pending\n"
        << "                    confirmations onto the platform overlay;\n"
        << "                    debug additionally shows the Observation\n"
        << "                    debug face (semantic snapshot boxes)\n"
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

int mirage::apps::run_service_host(int argc, char **argv, bool product) {
    std::string shell_path;
    bool open_frontend = true;
    mirage::runtime::ServiceConfig config;
    auto presentation = mirage::platform::make_presentation_context();
    config.credential_write = [](const std::string &reference, const std::string &value) {
        const auto result = mirage::platform::write_credential(reference, value);
        return mirage::runtime::CredentialWriteResult{result.ok, result.error};
    };

    config.mirage_version = std::string(kVersion);
    std::vector<std::string> read_roots;
    std::vector<std::string> write_roots;
    std::optional<std::string> workspace;
    bool basic_tools_enabled = false;
    bool exec_enabled = false;
    bool basic_tools_from_flags = false;
    std::vector<std::string> perm_flags;
    std::optional<std::string> flag_socket;
    std::optional<std::string> flag_confirm;
    long confirm_wait_ms = 120000;
    bool read_roots_from_flags = false;
    bool model_from_flags = false;
    bool runtime_from_flags = false;
    bool overlay_from_flags = false;
    bool ocr_from_flags = false;
    bool overlay_debug = false;
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
        if (product && argument == "--shell" && index + 1 < argc) {
            shell_path = argv[++index];
            continue;
        }
        if (product && argument == "--no-shell") {
            open_frontend = false;
            continue;
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
        if (argument == "--write-root" && index + 1 < argc) {
            write_roots.emplace_back(argv[++index]);
            basic_tools_from_flags = true;
            continue;
        }
        if (argument == "--workspace" && index + 1 < argc) {
            workspace = argv[++index];
            basic_tools_from_flags = true;
            continue;
        }
        if (argument == "--enable-basic-tools") {
            basic_tools_enabled = true;
            basic_tools_from_flags = true;
            continue;
        }
        if (argument == "--enable-exec") {
            exec_enabled = true;
            basic_tools_enabled = true;
            basic_tools_from_flags = true;
            continue;
        }
        if (argument == "--ocr-manifest" && index + 1 < argc) {
            const std::string path = argv[++index];
            if (!mirage::runtime::persistence::valid_ocr_manifest_path(path)) {
                std::cerr << kProgramName
                          << ": OCR manifest requires an absolute bounded path or an empty value\n";
                return 2;
            }
            ocr_from_flags = true;
            config.ocr_manifest = path;
            continue;
        }
        if (argument == "--perception-period-ms" && index + 1 < argc) {
            const std::string value = argv[++index];
            int parsed = 0;
            const auto [end, ec] =
                std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (ec != std::errc{} || end != value.data() + value.size() || parsed < 0 ||
                parsed > 1000 || (parsed > 0 && parsed < 100)) {
                std::cerr << kProgramName
                          << ": --perception-period-ms expects 0 or 100..1000 (got '"
                          << value << "')\n";
                return 2;
            }
            config.perception_sample_period_ms = parsed;
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
        if (argument == "--model-endpoint" && index + 1 < argc) {
            model_from_flags = true;
            config.model.enabled = true;
            config.model.endpoint_origin = argv[++index];
            continue;
        }
        if (argument == "--model-selector" && index + 1 < argc) {
            config.model.model_selector = argv[++index];
            continue;
        }
        if (argument == "--model-api-prefix" && index + 1 < argc) {
            config.model.api_prefix = argv[++index];
            continue;
        }
        if (argument == "--model-dialect" && index + 1 < argc) {
            config.model.dialect = argv[++index];
            continue;
        }
        if (argument == "--model-credential-env" && index + 1 < argc) {
            config.model.credential_env = argv[++index];
            continue;
        }
        if (argument == "--model-display-name" && index + 1 < argc) {
            config.model.display_name = argv[++index];
            continue;
        }
        if (argument == "--overlay" && index + 1 < argc) {
            const std::string_view mode{argv[++index]};
            if (mode != "on" && mode != "debug") {
                std::cerr << kProgramName << ": --overlay expects on|debug (got '" << mode
                          << "')\n";
                print_usage(std::cerr);
                return 2;
            }
            overlay_from_flags = true;
            overlay_debug = mode == "debug";
            continue;
        }
        std::cerr << kProgramName << ": unknown argument '" << argument << "'\n";
        print_usage(std::cerr);
        return 2;
    }

    // M7-10 (DEC-086): the runtime host owns desktop enumeration, capture
    // and input, and its process-wide DPI awareness decides whether those
    // surfaces share the desktop's physical pixel space — declare it before
    // the tray carrier, frontend or desktop environment open any window.
    const auto dpi_awareness = mirage::platform::declare_host_dpi_awareness();
    const char *dpi_mode = dpi_awareness == mirage::platform::DpiAwareness::per_monitor_v2
                               ? "per-monitor v2 (declared)"
                           : dpi_awareness == mirage::platform::DpiAwareness::system
                               ? "system aware (declared)"
                           : dpi_awareness == mirage::platform::DpiAwareness::prior_per_monitor
                               ? "per-monitor v2 (already in effect)"
                           : dpi_awareness == mirage::platform::DpiAwareness::unchanged
                               ? "unchanged (fixed by manifest or earlier call)"
                               : "not applicable on this platform";
    std::cout << kProgramName << ": host dpi awareness " << dpi_mode << '\n';

#if defined(MIRAGE_BUNDLED_VISION) && !defined(_WIN32)
    // DEC-068: relocatable product assets; settings and CLI below retain precedence.
    if (product && !ocr_from_flags) {
        const auto parent =
            std::filesystem::path(mirage::platform::current_executable_path()).parent_path();
        auto prefix = parent.parent_path();
        if (parent.filename() == "tray" && prefix.filename() == "apps")
            prefix = prefix.parent_path();
        const auto resources = prefix / "share/mirage/vision/ppocrv5-mobile-v1/manifest.json";
        config.ocr_manifest = resources;
    }
#endif
    if (product) {
#ifdef _WIN32
        config.tray_carrier = mirage::platform::windows_backend::open_tray_carrier();
        const auto executable_name = "mirage-native.exe";
#else
        config.tray_carrier = mirage::platform::linux_backend::open_tray_carrier();
        const auto executable_name = "mirage-native";
#endif
        if (!config.tray_carrier) {
            std::cerr << "mirage-tray: no notification area / StatusNotifierWatcher; frontend will "
                         "not open\n";
            return 1;
        }
        if (shell_path.empty()) {
            auto parent =
                std::filesystem::path(mirage::platform::current_executable_path()).parent_path();
            auto binary = parent / executable_name;
            if (!std::filesystem::exists(binary))
                binary = parent.parent_path() / "native" / executable_name;
            shell_path = binary.string();
        }
        shell_path = std::filesystem::absolute(shell_path).string();
        config.frontend = mirage::platform::make_frontend_process(shell_path);
        config.pet_expected = true;
        auto pet_binary =
            std::filesystem::path(mirage::platform::current_executable_path()).parent_path();
#ifdef _WIN32
        pet_binary /= "mira_pet.exe";
        if (!std::filesystem::is_regular_file(pet_binary))
            pet_binary = pet_binary.parent_path().parent_path() / "pet" / "mira_pet.exe";
#else
        pet_binary /= "mira_pet";
        if (!std::filesystem::is_regular_file(pet_binary))
            pet_binary = pet_binary.parent_path().parent_path() / "pet" / "mira_pet";
#endif
        if (std::filesystem::is_regular_file(pet_binary))
            config.pet = mirage::platform::make_pet_process(
                std::filesystem::absolute(pet_binary).string(), presentation);
#ifdef _WIN32
        config.tray_icon_path =
            (std::filesystem::path(shell_path).parent_path() / "assets/mira.ico").string();
#else
        config.tray_icon_path =
            (std::filesystem::path(shell_path).parent_path() / "assets/mira.png").string();
#endif
        config.open_frontend = open_frontend;
    }
    // M1-07 DEC-011 configuration: the settings file (when requested, or the
    // default document on first launch) provides the baseline, command-line
    // flags override item by item. A settings file that exists but cannot be
    // trusted fails closed. Both entry points share one application pass
    // (REUSE-11) so the --config and default-document surfaces cannot drift;
    // the guards below are the DEC-011 flag precedence, applied identically.
    const auto apply_settings = [&](const mirage::runtime::persistence::LocalSettings &settings) {
        if (settings.vision && !ocr_from_flags)
            config.ocr_manifest = settings.vision->ocr_manifest;
        if (settings.basic_tools && !basic_tools_from_flags) {
            // M10-08 (DEC-095): the same basic-tools surface as --config
            // on both entry paths; the harness E2E passes flags and never
            // sees the divergence.
            basic_tools_enabled = settings.basic_tools->enabled;
            exec_enabled = settings.basic_tools->exec_enabled;
            workspace = settings.basic_tools->workspace;
            if (!workspace->empty()) {
                basic_tools_from_flags = true; // a configured workspace counts as intent
            }
            write_roots = settings.basic_tools->write_roots;
        }
        if (!settings.socket_path.empty()) {
            config.socket_path = settings.socket_path;
        }
        if (!settings.read_roots.empty() && !read_roots_from_flags) {
            read_roots = settings.read_roots;
        }
        for (const auto &[capability, rule] : settings.permission_rules) {
            const auto parsed = mirage::runtime::permission::capability_from_name(capability);
            const auto parsed_rule = mirage::runtime::permission::rule_from_name(rule);
            if (parsed && parsed_rule) {
                config.permission_policy.rules[static_cast<std::size_t>(*parsed)] = *parsed_rule;
            }
        }
        if (settings.confirmation) {
            flag_confirm = *settings.confirmation;
        }
        if (settings.model.has_value() && !model_from_flags) {
            config.model = mirage::integration::ModelLayerConfig{};
            config.model.enabled =
                settings.model->enabled && !settings.model->endpoint_origin.empty();
            config.model.dialect = settings.model->dialect;
            config.model.display_name = settings.model->display_name;
            config.model.endpoint_origin = settings.model->endpoint_origin;
            config.model.api_prefix = settings.model->api_prefix;
            config.model.model_selector = settings.model->model_selector;
            config.model.credential_env = settings.model->credential_env;
            config.model.credential_ref = settings.model->credential_ref;
            config.model.context_window_tokens = settings.model->context_window_tokens;
            config.model.supports_reasoning = settings.model->supports_reasoning;
            config.model.supports_images = settings.model->supports_images;
            config.model_catalog_json = mirage::runtime::persistence::encode_settings(settings);
        }
        if (settings.runtime.has_value() && !runtime_from_flags) {
            if (settings.runtime->event_queue_capacity > 0) {
                config.event_queue_capacity = settings.runtime->event_queue_capacity;
            }
            if (settings.runtime->max_connections > 0) {
                config.max_connections = settings.runtime->max_connections;
            }
        }
    };
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
            apply_settings(decoded.settings);
        }
    }
    // DEC-011 flip (M5-07): with no explicit --config, the default settings
    // file is picked up when it exists — the policy face (M5-07) writes back
    // here, so a persisted rule set must survive restarts without flags.
    if (!config_file) {
        const std::filesystem::path default_file =
            mirage::runtime::persistence::default_config_directory() / "service.json";
        std::error_code exists_ec;
        if (std::filesystem::exists(default_file, exists_ec) && !exists_ec) {
            config_file = default_file;
            const std::filesystem::path parent = default_file.parent_path();
            const mirage::runtime::persistence::LocalStateStore store(
                parent, default_file.filename().string(),
                mirage::runtime::persistence::kMaxSettingsFileBytes);
            const auto loaded = store.load();
            if (loaded.status == mirage::runtime::persistence::LoadStatus::IoError ||
                loaded.status == mirage::runtime::persistence::LoadStatus::TooLarge) {
                std::cerr << kProgramName << ": cannot read default config file '"
                          << default_file.string() << "': " << loaded.error << '\n';
                return 1;
            }
            if (loaded.status == mirage::runtime::persistence::LoadStatus::Loaded) {
                const auto decoded = mirage::runtime::persistence::decode_settings(loaded.body);
                if (!decoded.ok) {
                    std::cerr << kProgramName << ": invalid default config file '"
                              << default_file.string() << "': " << decoded.error << '\n';
                    return 1;
                }
                apply_settings(decoded.settings);
            }
        }
    }
    // DEC-034: model.set also persists on a first launch and with --config.
    // Select the write-back destination even when the document is absent.
    const auto settings_destination = config_file.value_or(
        mirage::runtime::persistence::default_config_directory() / "service.json");
    config.settings_directory = settings_destination.parent_path().empty()
                                    ? std::filesystem::path(".")
                                    : settings_destination.parent_path();
    config.settings_file_name = settings_destination.filename().string();
    if (flag_socket) {
        config.socket_path = *flag_socket;
    }
    for (const std::string &rule : perm_flags) {
        if (const auto parsed = parse_perm_rule(rule)) {
            config.permission_policy.rules[static_cast<std::size_t>(parsed->first)] =
                parsed->second;
        }
    }
    // M5-07 policy face: the service reports (and persists against) the
    // resolved read roots; the roots themselves bound the provider at start.
    config.read_roots = read_roots;
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
    if (basic_tools_enabled || exec_enabled || !write_roots.empty() || workspace.has_value()) {
        // M10-08: the Windows backend has no FilesystemProvider and no
        // exec cwd support yet (M10-10); the flags would register nothing —
        // refused loudly instead.
        std::cerr << kProgramName
                  << ": --enable-basic-tools/--enable-exec/--workspace/--write-root are Linux "
                     "backend surfaces\n";
        return 2;
    }
    // M7-12 (DEC-066 Windows side): the product host opens the platform's
    // opt-in surfaces the same way the Linux branch passes `product` — the
    // Win32 window/screen/input and UIA accessibility surfaces ride the
    // product flag, and a probe that fails (no interactive desktop, no UIA
    // core) leaves that accessor null and the rest alive. The headless
    // developer service keeps the opt-in defaults, per DEC-066.
    auto environment =
        std::make_shared<mirage::platform::windows_backend::WindowsDesktopEnvironment>(
            mirage::platform::windows_backend::Win32Options{product},
            mirage::platform::windows_backend::UiaOptions{product},
            mirage::platform::windows_backend::ApplicationOptions{product},
            mirage::platform::windows_backend::NotificationsOptions{product});
    if (product) {
        // Startup capability line (DEC-066 capability honesty): what the
        // probes actually opened, not what was requested.
        std::cout << kProgramName << ": desktop surfaces window="
                  << (environment->window() != nullptr ? "on" : "off")
                  << " screen=" << (environment->screen() != nullptr ? "on" : "off")
                  << " input=" << (environment->input() != nullptr ? "on" : "off")
                  << " accessibility=" << (environment->accessibility() != nullptr ? "on" : "off")
                  << " application=" << (environment->application() != nullptr ? "on" : "off")
                  << " notification=" << (environment->notification() != nullptr ? "on" : "off")
                  << '\n';
    }
    if (overlay_from_flags) {
        // M5-09 (DEC-029): the layered-window overlay carrier. A null probe
        // (no interactive desktop) degrades loudly — the service continues
        // without the overlay surface.
        config.overlay_carrier = mirage::platform::windows_backend::open_overlay_carrier();
    }
#else
    std::vector<std::filesystem::path> read_scope;
    read_scope.reserve(read_roots.size());
    for (const std::string &root : read_roots) {
        read_scope.emplace_back(root);
    }
    std::vector<std::filesystem::path> write_scope;
    write_scope.reserve(write_roots.size());
    for (const std::string &root : write_roots) {
        write_scope.emplace_back(root);
    }
    // M10-08 (DEC-095): read roots never imply write authority — the write
    // scope is its own configuration surface.
    auto environment = std::make_shared<mirage::platform::linux_backend::LinuxDesktopEnvironment>(
        std::move(read_scope),
        mirage::platform::linux_backend::X11Options{product || !config.ocr_manifest.empty(), {}},
        mirage::platform::linux_backend::AtspiOptions{product},
        mirage::platform::linux_backend::ApplicationOptions{product},
        mirage::platform::linux_backend::NotificationsOptions{product},
        mirage::platform::linux_backend::FilesystemWriteOptions{std::move(write_scope)},
        presentation);
    if (basic_tools_enabled) {
        config.basic_tools.enabled = true;
        config.basic_tools.exec_enabled = exec_enabled;
        if (workspace && !workspace->empty()) {
            config.basic_tools.workspace = *workspace;
        }
        if (config.basic_tools.exec_enabled && config.basic_tools.workspace.empty()) {
            std::cerr << kProgramName
                      << ": --enable-exec requires a workspace (--workspace PATH or the "
                         "settings file); exec is refused without one\n";
            return 2;
        }
        // Retained command output lives under the state directory; the
        // store clears it at construction (refs are runtime state).
        if (config.basic_output_directory.empty()) {
            config.basic_output_directory =
                mirage::runtime::persistence::default_state_directory() / "basic-output";
        }
    }
    if (overlay_from_flags) {
        // M5-09 (DEC-029): the X11 shape overlay carrier. Null without an
        // X/XWayland connection (e.g. a Wayland-native session) — loud
        // degradation, never a faked surface.
        config.overlay_carrier = mirage::platform::linux_backend::open_overlay_carrier();
    }
#endif
    if (overlay_from_flags && config.overlay_carrier == nullptr) {
        std::cerr << kProgramName
                  << ": overlay carrier unavailable on this session (no interactive display / "
                     "no X or XWayland connection); the service continues without the overlay\n";
    }
    config.overlay_debug = overlay_debug;
    auto binding = std::make_shared<mirage::integration::MiraEnvironmentBinding>(environment);

    config.model.credential_lookup =
        [](const std::string &reference) -> std::optional<std::string> {
        const auto result = mirage::platform::read_credential(reference);
        if (result.ok)
            return result.value;
        return {};
    };
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
    struct sigaction action {};
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
              << "filesystem read scope: " << read_roots.size() << " root(s)\n"
              << "desktop overlay: "
              << (config.overlay_carrier != nullptr ? (config.overlay_debug ? "debug" : "on")
                                                    : "off")
              << '\n';
    if (config.basic_tools.enabled) {
        // M10 operator diagnostics: the session tools are configuration
        // driven and invisible at IPC level until a turn runs; state them
        // at startup so a silent config divergence cannot recur unnoticed.
        std::cout << "basic tools: read/write/edit exposed; exec "
                  << (config.basic_tools.exec_enabled ? "enabled" : "off") << ", workspace "
                  << (workspace && !workspace->empty() ? *workspace : "(none)") << ", "
                  << write_roots.size() << " write root(s)\n";
    }
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
