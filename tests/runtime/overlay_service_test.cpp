// M5-09 Desktop Overlay service integration verification (independent
// verification pass, DEC-029). Drives a real RuntimeService with the
// overlay wired through ServiceConfig::overlay_carrier and a test-owned
// carrier: the pump lives on the service's executor blocking worker, a
// Confirm-rule permission request is mirrored onto the surface, an overlay
// confirm-entry click resolves through the hub's reply path (the task
// completes confirmed), an IPC client that answers first wins and the
// mirrored face drops on the next tick (DEC-020 snapshot discipline), and
// a carrier whose surface breaks degrades loudly without failing the
// service. Shutdown must stay clean in all three topologies (the ordered
// teardown joins the pump before the presenter dies).

#include "../support/ipc_io.hpp"
#include "../support/test.hpp"

#include <mirage/desktop/overlay_carrier.hpp>
#include <mirage/desktop/overlay_surface.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/permission/confirmation_hub.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace desktop = mirage::desktop;
namespace ipc = mirage::runtime::ipc;
namespace permission = mirage::runtime::permission;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::testing::TempDir;

// Liveness guards against hangs, not latency assertions (see
// task_permission_test for the oversubscription rationale).
constexpr auto kCallBudget = std::chrono::seconds{30};
constexpr auto kTaskBudget = std::chrono::seconds{30};
constexpr auto kSurfaceBudget = std::chrono::seconds{20};
constexpr auto kClearBudget = std::chrono::seconds{10};
constexpr auto kConfirmBudget = std::chrono::seconds{20};

/// Test-owned carrier: runs the presentation loop on the service's blocking
/// worker, records the newest composed frame and delivers test-queued
/// clicks through the carrier context (the pump thread). A construct-time
/// flag makes run() fail immediately, modelling a surface the platform
/// refused — the loud-degradation topology.
class FakeOverlayCarrier final : public desktop::OverlayCarrier {
  public:
    explicit FakeOverlayCarrier(bool fail_surface = false) : fail_surface_(fail_surface) {}

    RunReport run(const desktop::OverlayCarrierContext &context,
                  const std::function<bool()> &stop_requested) override {
        entered_.store(true, std::memory_order_release);
        if (fail_surface_) {
            return RunReport{false, "fake carrier refuses the surface"};
        }
        while (!stop_requested()) {
            if (context.on_tick) {
                context.on_tick();
            }
            desktop::OverlaySurfaceFrame frame;
            if (context.load_frame && context.load_frame(frame)) {
                const std::lock_guard lock(mutex_);
                last_frame_ = std::move(frame);
            }
            std::vector<desktop::OverlayClick> batch;
            {
                const std::lock_guard lock(mutex_);
                batch.swap(clicks_);
            }
            for (const desktop::OverlayClick &click : batch) {
                if (context.on_click) {
                    context.on_click(click);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return RunReport{true, {}};
    }

    void wakeup() override {} // no external wait to release in the fake

    /// Flow side (test thread): queue one confirm-entry click for the pump.
    void emit_click(const std::string &request_id, bool approved) {
        const std::lock_guard lock(mutex_);
        clicks_.push_back(desktop::OverlayClick{request_id, approved});
    }

    std::optional<desktop::OverlaySurfaceFrame> last_frame() const {
        const std::lock_guard lock(mutex_);
        return last_frame_;
    }

    bool entered() const { return entered_.load(std::memory_order_acquire); }

  private:
    bool fail_surface_ = false;
    std::atomic<bool> entered_{false};
    mutable std::mutex mutex_;
    std::optional<desktop::OverlaySurfaceFrame> last_frame_;
    std::vector<desktop::OverlayClick> clicks_;
};

/// Non-owning ServiceConfig wiring: the scenario stack owns the carrier,
/// the service only observes it (the no-op deleter keeps the shared_ptr
/// from ever deleting the stack object).
std::shared_ptr<desktop::OverlayCarrier> observe(desktop::OverlayCarrier &carrier) {
    return std::shared_ptr<desktop::OverlayCarrier>(&carrier, [](desktop::OverlayCarrier *) {});
}

ServiceConfig make_config(const TempDir &dir, FakeOverlayCarrier &carrier) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.5.0-test";
    config.executor_threads = 2;
    config.step_timeout = std::chrono::milliseconds{10000};
    config.recovery_directory = dir.root() / "recovery";
    // The scenario capability: filesystem.read gates behind confirmation, so
    // the hub raises a request the overlay must mirror (DEC-029 decision 5).
    config.permission_policy
        .rules[static_cast<std::size_t>(permission::Capability::FilesystemRead)] =
        permission::Rule::Confirm;
    config.confirmation_hub = std::make_shared<permission::AsyncConfirmationHub>(kConfirmBudget);
    config.overlay_carrier = observe(carrier);
    config.overlay_debug = false;
    return config;
}

std::shared_ptr<integration::MiraEnvironmentBinding> make_binding(const TempDir &dir) {
    return std::make_shared<integration::MiraEnvironmentBinding>(
        std::make_shared<linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{dir.root()}));
}

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
}

std::optional<std::string> submit_read_task(ipc::IpcClient &client,
                                            const std::filesystem::path &goal_file) {
    ipc::SubmitTaskRequest request;
    request.goal = "read the goal file";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    const ipc::Response response = client.call(request, kCallBudget);
    const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response.payload);
    if (!response.ok || submitted == nullptr) {
        std::fprintf(stderr, "[overlay_service_test] task.submit failed: ok=%d code='%s'\n",
                     response.ok ? 1 : 0, response.error.code.c_str());
        return std::nullopt;
    }
    return submitted->task_id;
}

template <typename Predicate>
bool wait_for(Predicate &&predicate, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        if (predicate()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

/// Waits until the mirrored face shows a confirmation and returns it.
std::optional<desktop::OverlayConfirmation> wait_confirmation_shown(FakeOverlayCarrier &carrier) {
    std::optional<desktop::OverlayConfirmation> shown;
    wait_for(
        [&] {
            const auto frame = carrier.last_frame();
            if (frame.has_value() && frame->confirmation.has_value() &&
                !frame->confirmation->request_id.empty()) {
                shown = frame->confirmation;
                return true;
            }
            return false;
        },
        kSurfaceBudget);
    return shown;
}

/// Waits until the mirrored face no longer shows `request_id` (the hub
/// probe's clearing publish was composed onto the surface).
bool wait_confirmation_cleared(FakeOverlayCarrier &carrier, const std::string &request_id) {
    return wait_for(
        [&] {
            const auto frame = carrier.last_frame();
            return frame.has_value() && (!frame->confirmation.has_value() ||
                                         frame->confirmation->request_id != request_id);
        },
        kClearBudget);
}

std::optional<ipc::InspectTask> wait_terminal(const std::string &socket_path,
                                              const std::string &task_id) {
    ipc::IpcClient client(socket_path);
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    for (;;) {
        const ipc::Response response = client.call(ipc::InspectTaskRequest{task_id}, kCallBudget);
        const auto *inspect = std::get_if<ipc::InspectTask>(&response.payload);
        if (inspect != nullptr) {
            if (inspect->progress == "Completed" || inspect->progress == "Failed" ||
                inspect->progress == "Cancelled") {
                return *inspect;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

void scenario_overlay_click_resolves_the_confirmation() {
    TempDir dir;
    FakeOverlayCarrier carrier;
    ServiceConfig config = make_config(dir, carrier);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    // The pump lives on the service's blocking worker.
    MIRAGE_CHECK(wait_for([&] { return carrier.entered(); }, std::chrono::seconds{10}));

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "overlay click topology " + token + "\n";
    write_text_file(goal_file, content);

    ipc::IpcClient client(config.socket_path);
    const std::optional<std::string> task_id = submit_read_task(client, goal_file);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        service.request_shutdown();
        MIRAGE_CHECK(service.run().clean);
        return;
    }

    // The hub raised the request; the surface mirrored it.
    const std::optional<desktop::OverlayConfirmation> shown = wait_confirmation_shown(carrier);
    MIRAGE_CHECK(shown.has_value());
    if (!shown.has_value()) {
        service.request_shutdown();
        MIRAGE_CHECK(service.run().clean);
        return;
    }
    MIRAGE_CHECK(shown->capability == "filesystem.read");
    MIRAGE_CHECK(shown->resource == goal_file.string());

    // The confirm-entry click is a business decision routed through the
    // service's serial domain onto the hub's reply path (first-response-wins
    // with IPC clients, DEC-020).
    carrier.emit_click(shown->request_id, true);

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done.has_value()) {
        service.request_shutdown();
        MIRAGE_CHECK(service.run().clean);
        return;
    }
    MIRAGE_CHECK(done->progress == "Completed");
    MIRAGE_CHECK(done->steps.size() == 1);
    if (done->steps.size() == 1) {
        MIRAGE_CHECK(done->steps[0].permission == "confirmed");
        MIRAGE_CHECK(done->steps[0].status == "ok");
        MIRAGE_CHECK(done->steps[0].result == content);
    }

    // The resolved request left the surface (hub-probe revalidation).
    MIRAGE_CHECK(wait_confirmation_cleared(carrier, shown->request_id));

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_ipc_client_wins_and_the_face_drops() {
    TempDir dir;
    FakeOverlayCarrier carrier;
    ServiceConfig config = make_config(dir, carrier);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);
    MIRAGE_CHECK(wait_for([&] { return carrier.entered(); }, std::chrono::seconds{10}));

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    write_text_file(goal_file, "unreachable by the rejected task\n");

    ipc::IpcClient client(config.socket_path);
    const std::optional<std::string> task_id = submit_read_task(client, goal_file);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        service.request_shutdown();
        MIRAGE_CHECK(service.run().clean);
        return;
    }

    const std::optional<desktop::OverlayConfirmation> shown = wait_confirmation_shown(carrier);
    MIRAGE_CHECK(shown.has_value());
    if (!shown.has_value()) {
        service.request_shutdown();
        MIRAGE_CHECK(service.run().clean);
        return;
    }

    // An IPC client answers first: the hub pending set is the fact source,
    // so the mirrored face must drop without any overlay click.
    const ipc::Response response =
        client.call(ipc::RespondPermissionRequest{shown->request_id, false}, kCallBudget);
    MIRAGE_CHECK(response.ok);

    MIRAGE_CHECK(wait_confirmation_cleared(carrier, shown->request_id));

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done.has_value()) {
        MIRAGE_CHECK(done->progress == "Failed");
        MIRAGE_CHECK(done->steps.size() == 1);
        if (done->steps.size() == 1) {
            MIRAGE_CHECK(done->steps[0].permission == "confirmation_rejected");
        }
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_broken_carrier_degrades_and_the_service_continues() {
    TempDir dir;
    FakeOverlayCarrier carrier(/*fail_surface=*/true);
    ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.5.0-test";
    config.executor_threads = 2;
    config.step_timeout = std::chrono::milliseconds{10000};
    config.recovery_directory = dir.root() / "recovery";
    config.overlay_carrier = observe(carrier);

    RuntimeService service(config);
    // The overlay is a presentation surface: a broken carrier is a loud
    // degradation, never a failed start (DEC-011 posture).
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "unbothered by the dark overlay " + token + "\n";
    write_text_file(goal_file, content);

    ipc::IpcClient client(config.socket_path);
    const std::optional<std::string> task_id = submit_read_task(client, goal_file);
    MIRAGE_CHECK(task_id.has_value());
    if (task_id.has_value()) {
        const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
        MIRAGE_CHECK(done.has_value());
        if (done.has_value()) {
            MIRAGE_CHECK(done->progress == "Completed");
            if (done->steps.size() == 1) {
                MIRAGE_CHECK(done->steps[0].result == content);
            }
        }
    }

    // Teardown must stay clean even though the pump loop already exited on
    // its own (the ordered teardown joins a finished worker safely).
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

} // namespace

int main() {
    scenario_overlay_click_resolves_the_confirmation();
    scenario_ipc_client_wins_and_the_face_drops();
    scenario_broken_carrier_degrades_and_the_service_continues();
    return mirage::testing::finish("overlay_service_test");
}
