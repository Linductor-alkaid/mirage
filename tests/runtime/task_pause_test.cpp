// M5-10 task pause/resume face verification (independent verification
// pass, DEC-030). Drives a real RuntimeService over its Local IPC surface
// and checks the pinned pause family end to end: pause parks the M1 driving
// loop at the operation boundary WITHOUT mis-settling the task (the
// progress stays "Paused" — the pre-M5-10 refusal path would have billed it
// Cancelled), a subscribed connection sees the task.updated "Paused" event,
// resume re-drives the parked loop to full completion, cancel while parked
// settles Cancelled with the remainder skipped, and the guard matrix
// mirrors task.cancel (unknown not_found; terminal / non-paused transitions
// surface the pinned rejection verbatim). Protocol-level codec coverage for
// task.pause / task.resume / TaskPaused / TaskResumed lives in the golden
// vectors (v10) and ipc_protocol_test.

#include "../support/ipc_io.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace ipc = mirage::runtime::ipc;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::testing::TempDir;
using mirage::testing::unique_token;

// Liveness guards against hangs, not latency assertions (the
// task_cancel_test oversubscription rationale).
constexpr auto kCallBudget = std::chrono::seconds{30};
constexpr auto kTaskBudget = std::chrono::seconds{20};
/// The parked window: long enough for an in-flight `sleep 1` step to finish
/// and for a mis-settling driver to have (wrongly) converged, short enough
/// to keep the scenario inside its budget.
constexpr auto kParkWindow = std::chrono::milliseconds{2500};

ServiceConfig make_config(const TempDir &dir) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "pause-svc.sock").string();
    config.mirage_version = "0.6.0-pause-test";
    config.executor_threads = 2;
    // A wide step budget: only the pause family and cancel may end a step
    // inside this test; a timeout can never mask a broken park.
    config.step_timeout = std::chrono::milliseconds{60000};
    config.recovery_directory = dir.root() / "recovery";
    return config;
}

std::shared_ptr<integration::MiraEnvironmentBinding> make_binding(const TempDir &dir) {
    return std::make_shared<integration::MiraEnvironmentBinding>(
        std::make_shared<linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{dir.root()}));
}

std::optional<std::string> submit_task(ipc::IpcClient &client, ipc::SubmitTaskRequest request) {
    const ipc::Response response = client.call(request, kCallBudget);
    const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response.payload);
    if (!response.ok || submitted == nullptr) {
        std::fprintf(stderr, "[task_pause_test] task.submit failed: ok=%d code='%s'\n",
                     response.ok ? 1 : 0, response.error.code.c_str());
        return std::nullopt;
    }
    return submitted->task_id;
}

std::optional<ipc::InspectTask> inspect_once(const std::string &socket_path,
                                             const std::string &task_id) {
    ipc::IpcClient client(socket_path);
    const ipc::Response response = client.call(ipc::InspectTaskRequest{task_id}, kCallBudget);
    const auto *inspect = std::get_if<ipc::InspectTask>(&response.payload);
    if (!response.ok || inspect == nullptr) {
        return std::nullopt;
    }
    return *inspect;
}

template <typename Predicate>
std::optional<ipc::InspectTask> wait_for(const std::string &socket_path, const std::string &task_id,
                                         std::chrono::milliseconds budget, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        const auto view = inspect_once(socket_path, task_id);
        if (view && predicate(*view)) {
            return view;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
}

std::optional<ipc::InspectTask> wait_terminal(const std::string &socket_path,
                                              const std::string &task_id,
                                              std::chrono::milliseconds budget = kTaskBudget) {
    return wait_for(socket_path, task_id, budget, [](const ipc::InspectTask &view) {
        return view.progress == "Completed" || view.progress == "Failed" ||
               view.progress == "Cancelled";
    });
}

/// Raw subscriber connection (the permission_ipc_test shape): receives the
/// event stream so the test can observe the pause-family progress advance.
struct Subscriber {
    ipc::IpcStream stream;
    std::string buffer;
    std::uint64_t next_id = 900;

    bool valid() const { return stream.valid(); }

    bool send(const ipc::Request &body) {
        return mirage::testing::write_all(
            stream, ipc::make_frame(ipc::encode_request(next_id++, body)), kCallBudget);
    }

    /// Subscribes and consumes the ack, discarding any event frames that
    /// race it (events are notifications, not answers).
    bool subscribe() {
        const std::uint64_t id = next_id;
        if (!send(ipc::SubscribeEventsRequest{})) {
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + kCallBudget;
        for (;;) {
            const mirage::testing::FrameRead read =
                mirage::testing::read_frame(stream, buffer, kCallBudget);
            if (read.status != mirage::testing::FrameRead::Status::Message) {
                return false;
            }
            if (const ipc::ResponseDecode response = ipc::decode_response(read.message);
                response.ok) {
                return response.response.ok &&
                       std::holds_alternative<ipc::ShutdownAccepted>(response.response.payload) &&
                       response.response.id == id;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
        }
    }

    std::optional<ipc::Event> next_event(std::chrono::milliseconds budget) {
        const mirage::testing::FrameRead read = mirage::testing::read_frame(stream, buffer, budget);
        if (read.status != mirage::testing::FrameRead::Status::Message) {
            return std::nullopt;
        }
        const ipc::EventDecode event = ipc::decode_event(read.message);
        if (event.ok) {
            return event.event;
        }
        return std::nullopt;
    }
};

/// Drains events until one task.updated event for `task_id` carries
/// `progress`; false on timeout.
bool saw_task_progress(Subscriber &subscriber, const std::string &task_id,
                       const std::string &progress, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        const std::optional<ipc::Event> event = subscriber.next_event(budget);
        if (!event) {
            return false;
        }
        if (const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event->payload)) {
            if (task->task_id == task_id && task->progress == progress) {
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
    }
}

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
}

// --- the parked drive --------------------------------------------------------

void scenario_pause_parks_the_drive_and_resume_completes() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    MIRAGE_CHECK(subscriber.subscribe());

    const std::string token = unique_token();
    const auto canary = dir.root() / ("canary-" + token);
    const auto readable = dir.root() / ("readable-" + token + ".txt");
    write_text_file(readable, "post-resume content " + token + "\n");

    // Step 0 is in flight when the pause lands; step 1 would leave the
    // canary; step 2 reads a file. The parked drive must admit none of them.
    ipc::SubmitTaskRequest request;
    request.goal = "task paused at the operation boundary";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 2"});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch '" + canary.string() + "'"});
    request.steps.push_back({ipc::StepKind::FilesystemRead, readable.string()});

    ipc::IpcClient client(config.socket_path);
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        service.request_shutdown();
        service.run();
        return;
    }

    // Wait until the driver admitted step 0, then pause: the wide in-flight
    // window makes the landing deterministic.
    const auto running =
        wait_for(config.socket_path, *task_id, kTaskBudget, [](const ipc::InspectTask &view) {
            return !view.steps.empty() && view.steps[0].status == "running";
        });
    MIRAGE_CHECK(running.has_value());
    if (!running) {
        service.request_shutdown();
        service.run();
        return;
    }

    const ipc::Response pause_response = client.call(ipc::PauseTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(pause_response.ok);
    const auto *paused = std::get_if<ipc::TaskPaused>(&pause_response.payload);
    MIRAGE_CHECK(paused != nullptr);
    if (paused != nullptr) {
        MIRAGE_CHECK(paused->task_id == *task_id);
        MIRAGE_CHECK(paused->progress == "Paused");
    }

    // The admitted command is a progress advance: subscribers saw "Paused"
    // before the ack left (DEC-012 decision 3).
    MIRAGE_CHECK(saw_task_progress(subscriber, *task_id, "Paused", kCallBudget));

    // Through the park window the task stays Paused — a pre-M5-10 driver
    // would have billed the refused next operation as Cancelled here — and
    // the canary never appeared.
    std::this_thread::sleep_for(kParkWindow);
    const auto parked = inspect_once(config.socket_path, *task_id);
    MIRAGE_CHECK(parked.has_value());
    if (parked) {
        MIRAGE_CHECK(parked->progress == "Paused");
        MIRAGE_CHECK(!std::filesystem::exists(canary));
    }

    // Resume: the parked loop re-admits its next operation and finishes.
    const ipc::Response resume_response =
        client.call(ipc::ResumeTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(resume_response.ok);
    const auto *resumed = std::get_if<ipc::TaskResumed>(&resume_response.payload);
    MIRAGE_CHECK(resumed != nullptr);
    if (resumed != nullptr) {
        MIRAGE_CHECK(resumed->task_id == *task_id);
        MIRAGE_CHECK(resumed->progress == "Active");
    }
    MIRAGE_CHECK(saw_task_progress(subscriber, *task_id, "Active", kCallBudget));

    const auto done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done) {
        MIRAGE_CHECK(done->progress == "Completed");
        MIRAGE_CHECK(done->has_success && done->success);
        MIRAGE_CHECK(done->steps.size() == 3);
        if (done->steps.size() == 3) {
            MIRAGE_CHECK(done->steps[0].status == "ok");
            MIRAGE_CHECK(done->steps[1].status == "ok");
            MIRAGE_CHECK(done->steps[2].status == "ok");
            MIRAGE_CHECK(done->steps[2].result == "post-resume content " + token + "\n");
        }
        MIRAGE_CHECK(std::filesystem::exists(canary));
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_cancel_while_parked_settles_cancelled() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = unique_token();
    const auto canary = dir.root() / ("canary-" + token);

    ipc::SubmitTaskRequest request;
    request.goal = "task cancelled while parked";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 1"});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch '" + canary.string() + "'"});

    ipc::IpcClient client(config.socket_path);
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        service.request_shutdown();
        service.run();
        return;
    }

    const auto running =
        wait_for(config.socket_path, *task_id, kTaskBudget, [](const ipc::InspectTask &view) {
            return !view.steps.empty() && view.steps[0].status == "running";
        });
    MIRAGE_CHECK(running.has_value());
    if (!running) {
        service.request_shutdown();
        service.run();
        return;
    }

    const ipc::Response pause_response = client.call(ipc::PauseTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(pause_response.ok);

    // Step 0 (sleep 1) finishes inside the park window and the drive parks
    // before step 1: the task is Paused, the canary never appears.
    std::this_thread::sleep_for(kParkWindow);
    const auto parked = inspect_once(config.socket_path, *task_id);
    MIRAGE_CHECK(parked.has_value());
    if (parked) {
        MIRAGE_CHECK(parked->progress == "Paused");
        MIRAGE_CHECK(!std::filesystem::exists(canary));
    }

    // Cancel through the park: the parked wait is cancel-aware, the pinned
    // cancel owns the settlement and the remainder is skipped.
    const ipc::Response cancel_response =
        client.call(ipc::CancelTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(cancel_response.ok);
    // The pinned view flips Cancelled inside the cancel command while the
    // parked driver converges its step bookkeeping on its next 100 ms
    // slice — wait for the full converged shape (the task_cancel_test
    // discipline), not the terminal progress alone.
    const auto done =
        wait_for(config.socket_path, *task_id, kTaskBudget, [](const ipc::InspectTask &view) {
            return view.progress == "Cancelled" && view.steps.size() == 2 &&
                   view.steps[0].status == "ok" && view.steps[1].status == "skipped";
        });
    MIRAGE_CHECK(done.has_value());
    if (done) {
        MIRAGE_CHECK(done->progress == "Cancelled");
        MIRAGE_CHECK(done->steps.size() == 2);
        if (done->steps.size() == 2) {
            MIRAGE_CHECK(done->steps[0].status == "ok");
            MIRAGE_CHECK(done->steps[1].status == "skipped");
        }
        MIRAGE_CHECK(!std::filesystem::exists(canary));
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- guard matrix ------------------------------------------------------------

void scenario_pause_resume_refusals_mirror_cancel() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);

    // Unknown ids: not_found, mirroring task.cancel.
    for (const bool pause : {true, false}) {
        const ipc::Response response = client.call(
            pause ? ipc::Request{ipc::PauseTaskRequest{"00000000000000000000000000000000"}}
                  : ipc::Request{ipc::ResumeTaskRequest{"00000000000000000000000000000000"}},
            kCallBudget);
        MIRAGE_CHECK(!response.ok);
        MIRAGE_CHECK(response.error.code == "not_found");
        MIRAGE_CHECK(!response.error.message.empty());
    }

    // Reported to the developer, deliberately not pinned here: a resume of
    // a RUNNING task currently succeeds (the pinned drive state stays Idle
    // for the whole M1 drive and Idle→Observing is a legal transition),
    // while the wire/host header docs claim a verbatim pinned rejection —
    // the contract decision (gate service-side vs amend the docs) belongs
    // to the implementation owner.
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_pause_completed_task_is_invalid_state_and_stays_terminal() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "task finished before anyone pauses it";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "printf done"});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        service.request_shutdown();
        service.run();
        return;
    }
    const auto completed = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(completed.has_value() && completed->progress == "Completed");
    if (!completed || completed->progress != "Completed") {
        service.request_shutdown();
        service.run();
        return;
    }

    for (const bool pause : {true, false}) {
        const ipc::Response response =
            pause ? client.call(ipc::PauseTaskRequest{*task_id}, kCallBudget)
                  : client.call(ipc::ResumeTaskRequest{*task_id}, kCallBudget);
        MIRAGE_CHECK(!response.ok);
        MIRAGE_CHECK(response.error.code == "pinned_runtime");
        MIRAGE_CHECK(response.error.message.find("invalid_state") != std::string::npos);
    }

    // Nothing was revived: the task stays Completed with its success.
    const auto after = inspect_once(config.socket_path, *task_id);
    MIRAGE_CHECK(after.has_value());
    if (after) {
        MIRAGE_CHECK(after->progress == "Completed");
        MIRAGE_CHECK(after->has_success && after->success);
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

} // namespace

int main() {
    scenario_pause_parks_the_drive_and_resume_completes();
    scenario_cancel_while_parked_settles_cancelled();
    scenario_pause_resume_refusals_mirror_cancel();
    scenario_pause_completed_task_is_invalid_state_and_stays_terminal();
    return mirage::testing::finish("task_pause_test");
}
