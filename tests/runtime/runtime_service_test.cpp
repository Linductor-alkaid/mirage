#include "../support/fake_desktop_environment.hpp"
#include "../support/ipc_io.hpp"

#include <mira/core_contracts.hpp>
#include <mira/model_contracts.hpp>
#include <mira/model_provider.hpp>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mirage/runtime/permission/permission.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/persistence/store.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <fcntl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace {

namespace mira = ::mira;
namespace ipc = mirage::runtime::ipc;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::runtime::ServiceRunReport;
using mirage::testing::FrameRead;

// Liveness guard against hangs, not a latency assertion: under parallel ctest
// CPU oversubscription the two service workers can lag several seconds behind
// (M2-06 investigation: 5 s produced rare false 'unavailable' under load while
// isolated runs answer in milliseconds), so the wait budget is generous.
constexpr auto kCallBudget = std::chrono::seconds{30};
constexpr auto kTaskBudget = std::chrono::seconds{10};

ServiceConfig make_config(const mirage::testing::TempDir &dir) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.4.0-test";
    config.executor_threads = 2;
    config.step_timeout = std::chrono::milliseconds{10000};
    // Recovery state stays inside the scenario's temp tree: without this the
    // service would persist into the user's real XDG state directory and
    // hydrate foreign history into every later scenario (M1-07).
    config.recovery_directory = dir.root() / "recovery";
    // M5-08 session state: same isolation discipline as recovery — scenarios
    // must not see each other's persisted sessions via the default state dir.
    config.session_state_directory = dir.root() / "session-state";
    return config;
}

/// Fresh binding over a fresh Linux desktop environment per service start.
/// M1-05: the environment's filesystem read scope is the test's temp
/// directory, so filesystem.read steps on fixtures inside it work while
/// everything outside stays denied.
std::shared_ptr<integration::MiraEnvironmentBinding>
make_binding(const mirage::testing::TempDir &dir) {
    return std::make_shared<integration::MiraEnvironmentBinding>(
        std::make_shared<linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{dir.root()}));
}

std::optional<std::string> submit_task(ipc::IpcClient &client, ipc::SubmitTaskRequest request) {
    const ipc::Response response = client.call(request, kCallBudget);
    const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response.payload);
    if (!response.ok || submitted == nullptr) {
        return std::nullopt;
    }
    return submitted->task_id;
}

/// Polls task.inspect until the task reports a terminal progress state.
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

/// Polls task.inspect until `predicate` accepts the view; a liveness wait,
/// not a latency assertion. The budget is the same generous kCallBudget the
/// event suite uses for its waits: under parallel ctest CPU oversubscription
/// the service workers can legitimately lag several seconds behind.
template <typename Predicate>
std::optional<ipc::InspectTask> wait_for_progress(const std::string &socket_path,
                                                  const std::string &task_id, Predicate predicate) {
    ipc::IpcClient client(socket_path);
    const auto deadline = std::chrono::steady_clock::now() + kCallBudget;
    for (;;) {
        const ipc::Response response = client.call(ipc::InspectTaskRequest{task_id}, kCallBudget);
        const auto *inspect = std::get_if<ipc::InspectTask>(&response.payload);
        if (inspect != nullptr && predicate(*inspect)) {
            return *inspect;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

bool looks_like_version_triple(const std::string &text) {
    const auto first = text.find('.');
    if (first == std::string::npos) {
        return false;
    }
    const auto second = text.find('.', first + 1);
    if (second == std::string::npos) {
        return false;
    }
    const std::string parts[] = {
        text.substr(0, first),
        text.substr(first + 1, second - first - 1),
        text.substr(second + 1),
    };
    for (const std::string &part : parts) {
        if (part.empty()) {
            return false;
        }
        for (const char character : part) {
            if (character < '0' || character > '9') {
                return false;
            }
        }
    }
    return true;
}

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
}

// --- lifecycle --------------------------------------------------------------

// --- observation face + definition read face (DEC-026, headless paths) ------
//
// The success path (a real semantic projection) needs window + accessibility
// providers and lives in observation_face_test (Xvfb + AT-SPI fixture). Here
// the headless topology exercises the fail-closed contract: the components
// the environment cannot deliver fail the request with the stable
// unavailable error naming the component, never a silently partial view.

void scenario_observe_fails_closed_on_headless_topology() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);

    // hello advertises the observation face; the face is always served.
    const ipc::Response hello = client.call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(hello.ok);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->observation.has_value());
        MIRAGE_CHECK(identity->observation.value_or(false));
    }

    // No X11 frontend on this topology: the always-on active-window component
    // fails the whole request (the assembler's requested-means-mandatory
    // discipline), and the stable error names the platform reason.
    const ipc::Response observe = client.call(ipc::DesktopObserveRequest{}, kCallBudget);
    MIRAGE_CHECK(!observe.ok);
    MIRAGE_CHECK(observe.error.code == "unavailable");
    MIRAGE_CHECK(observe.error.message.find("unsupported_platform") != std::string::npos);

    // The visual component without a wired registry fails closed too; the
    // capture order reports the first failed component (active window here).
    const ipc::Response visual = client.call(ipc::DesktopObserveRequest{true, true}, kCallBudget);
    MIRAGE_CHECK(!visual.ok);
    MIRAGE_CHECK(visual.error.code == "unavailable");

    // No model layer configured: the dialog face is dark (DEC-027).
    const ipc::Response chat = client.call(ipc::SessionChatRequest{"any", "hi"}, kCallBudget);
    MIRAGE_CHECK(!chat.ok);
    MIRAGE_CHECK(chat.error.code == "unavailable");
    MIRAGE_CHECK(chat.error.message == "model layer is not configured");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_workflow_get_unknown_id_is_not_found() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response response =
        client.call(ipc::WorkflowGetRequest{"no-such-workflow"}, kCallBudget);
    MIRAGE_CHECK(!response.ok);
    MIRAGE_CHECK(response.error.code == "not_found");
    MIRAGE_CHECK(response.error.message == "unknown workflow id");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// Minimal IR v1 definition for the definition-read-face scenario: one Verify
/// step whose predicate references an absent parameter (NotEvaluable under
/// DryRun), so workflow.publish passes its gate without a tool registry — the
/// same shape session_client_test publishes. `name` varies content: the pinned
/// library resolves versions by content digest and same-content records shadow
/// each other (MIRA-20260927-001).
std::string workflow_definition_json(const std::string &workflow_id, const std::string &step_id,
                                     const std::string &name) {
    return R"({"schema_version":{"major":1,"minor":0},"workflow_id":")" + workflow_id +
           R"(","name":")" + name + R"(","parameters":[],"steps":[{"step_id":")" + step_id +
           R"(","kind":"verify","verification":{"signal":"run_parameter:x","op":"eq","value":"y"}}],)"
           R"("default_policy":"strict","allowed_policies":["strict","dry_run"]})";
}

/// Definition read face success path (DEC-026): workflow.get serves the head
/// definition content the service last saved or published, addressed by that
/// version's content digest; workflow.delete removes the entry and the read
/// face answers not_found afterwards. The existing headless scenarios only
/// cover the unknown-id rejection — without this the face's main product path
/// (save -> read back -> publish -> read back -> delete) has no coverage.
void scenario_workflow_get_serves_head_definition_content() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const std::string workflow_id = mira::WorkflowId::generate().to_string();
    const std::string step_id = mira::StepId::generate().to_string();

    // Draft: the read face serves exactly the saved content under the save's
    // digest — byte-for-byte, no re-shaping on the way out.
    ipc::WorkflowSaveRequest save;
    save.definition_json = workflow_definition_json(workflow_id, step_id, "read-face draft");
    const ipc::Response saved = client.call(save, kCallBudget);
    MIRAGE_CHECK(saved.ok);
    const auto *saved_payload = std::get_if<ipc::WorkflowSaved>(&saved.payload);
    MIRAGE_CHECK(saved_payload != nullptr);
    if (saved_payload != nullptr) {
        MIRAGE_CHECK(saved_payload->workflow_id == workflow_id);
        MIRAGE_CHECK(saved_payload->digest.size() == 64);
    }

    const ipc::Response got = client.call(ipc::WorkflowGetRequest{workflow_id}, kCallBudget);
    MIRAGE_CHECK(got.ok);
    const auto *view = std::get_if<ipc::WorkflowDefinitionView>(&got.payload);
    MIRAGE_CHECK(view != nullptr);
    if (view != nullptr && saved_payload != nullptr) {
        MIRAGE_CHECK(view->workflow_id == workflow_id);
        MIRAGE_CHECK(view->digest == saved_payload->digest);
        MIRAGE_CHECK(view->definition_json == save.definition_json);
    }

    // Publish different content (the natural edit-then-publish flow): the
    // read face moves to the published head, digest included.
    ipc::WorkflowPublishRequest publish;
    publish.definition_json = workflow_definition_json(workflow_id, step_id, "read-face head");
    const ipc::Response published = client.call(publish, kCallBudget);
    MIRAGE_CHECK(published.ok);
    const auto *published_payload = std::get_if<ipc::WorkflowPublished>(&published.payload);
    MIRAGE_CHECK(published_payload != nullptr);
    if (published_payload != nullptr) {
        MIRAGE_CHECK(published_payload->digest != saved_payload->digest);
        MIRAGE_CHECK(!published_payload->idempotent);
    }

    const ipc::Response got_again = client.call(ipc::WorkflowGetRequest{workflow_id}, kCallBudget);
    MIRAGE_CHECK(got_again.ok);
    const auto *view_again = std::get_if<ipc::WorkflowDefinitionView>(&got_again.payload);
    MIRAGE_CHECK(view_again != nullptr);
    if (view_again != nullptr && published_payload != nullptr) {
        MIRAGE_CHECK(view_again->digest == published_payload->digest);
        MIRAGE_CHECK(view_again->definition_json == publish.definition_json);
    }

    // Delete removes the catalog entry: the read face answers not_found, the
    // same stable rejection as an unknown id.
    const ipc::Response deleted = client.call(ipc::WorkflowDeleteRequest{workflow_id}, kCallBudget);
    MIRAGE_CHECK(deleted.ok);
    const ipc::Response gone = client.call(ipc::WorkflowGetRequest{workflow_id}, kCallBudget);
    MIRAGE_CHECK(!gone.ok);
    MIRAGE_CHECK(gone.error.code == "not_found");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// Wire-budget projection (DEC-026, RULE-07): a service-side snapshot larger
/// than kObservationNodeWireBudget projects exactly the budget onto the wire
/// with the explicit `truncated` mark; a snapshot exactly at the budget stays
/// unmarked. The parent index remaps kNoParent to the wire's -1, and
/// focused_element comes from the projected snapshot. The real-topology
/// observation_face_test cannot size its AT-SPI tree this precisely, so the
/// fake environment's snapshot table is the knob here.
void scenario_observe_wire_budget_truncates_with_explicit_mark() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);

    // In-memory desktop: one focused window whose accessibility snapshot is
    // served from the fake's table, refillable between observations (the
    // assembler captures fresh state per request, so the next observe sees
    // the new tree without restarting the service).
    const auto desktop = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    mirage::desktop::WindowInfo window;
    window.id = "w1";
    window.title = "Budget Window";
    window.geometry = {0, 0, 640, 480};
    window.focused = true;
    desktop->windows.push_back(window);

    auto fill_snapshot = [&desktop](const std::size_t node_count) {
        mirage::desktop::SemanticSnapshot snapshot;
        snapshot.application = "BudgetApp";
        snapshot.window_title = "Budget Window";
        snapshot.nodes.reserve(node_count);
        for (std::size_t index = 0; index < node_count; ++index) {
            mirage::desktop::SemanticNode node;
            node.ref = "@e" + std::to_string(index + 1);
            node.role = "button";
            node.name = "node-" + std::to_string(index);
            node.parent = index == 0 ? mirage::desktop::kNoParent : 0;
            node.geometry = {static_cast<std::int32_t>(index), 0, 10, 10};
            node.focused = index == 7; // well inside the wire window
            node.enabled = true;
            snapshot.nodes.push_back(std::move(node));
        }
        desktop->snapshots["w1"] = std::move(snapshot);
    };

    RuntimeService service(config);
    MIRAGE_CHECK(service.start(std::make_shared<integration::MiraEnvironmentBinding>(desktop)).ok);
    ipc::IpcClient client(config.socket_path);

    // Over budget: 1025 service-side nodes project 1024 wire nodes marked
    // truncated; the projection keeps the capture order prefix.
    fill_snapshot(ipc::kObservationNodeWireBudget + 1);
    const ipc::Response over = client.call(ipc::DesktopObserveRequest{}, kCallBudget);
    MIRAGE_CHECK(over.ok);
    const auto *over_view = std::get_if<ipc::ObservationView>(&over.payload);
    MIRAGE_CHECK(over_view != nullptr);
    if (over_view != nullptr && over_view->semantic.has_value()) {
        const ipc::ObservationSemantic &semantic = *over_view->semantic;
        MIRAGE_CHECK(semantic.nodes.size() == ipc::kObservationNodeWireBudget);
        MIRAGE_CHECK(semantic.truncated);
        MIRAGE_CHECK(semantic.nodes.front().ref == "@e1");
        MIRAGE_CHECK(semantic.nodes.back().ref ==
                     "@e" + std::to_string(ipc::kObservationNodeWireBudget));
        // kNoParent has no wire form: roots project -1, real parents their
        // index.
        MIRAGE_CHECK(semantic.nodes.front().parent == -1);
        MIRAGE_CHECK(semantic.nodes[1].parent == 0);
        // Frame context still rides along: focused_element from the
        // projected snapshot (index 7 -> "@e8"), application from its root.
        MIRAGE_CHECK(over_view->focused_element == "@e8");
        MIRAGE_CHECK(over_view->active_application == "BudgetApp");
        MIRAGE_CHECK(over_view->active_window == "Budget Window");
        MIRAGE_CHECK(over_view->window_focused);
        MIRAGE_CHECK(over_view->environment_state.compare(0, 5, "test:") == 0);
    } else if (over_view != nullptr) {
        MIRAGE_CHECK(over_view->semantic.has_value());
    }

    // Exactly at the budget: the whole projection rides the wire and the
    // truncation mark stays off — incompleteness is never claimed that does
    // not exist.
    fill_snapshot(ipc::kObservationNodeWireBudget);
    const ipc::Response exact = client.call(ipc::DesktopObserveRequest{}, kCallBudget);
    MIRAGE_CHECK(exact.ok);
    const auto *exact_view = std::get_if<ipc::ObservationView>(&exact.payload);
    MIRAGE_CHECK(exact_view != nullptr);
    if (exact_view != nullptr && exact_view->semantic.has_value()) {
        MIRAGE_CHECK(exact_view->semantic->nodes.size() == ipc::kObservationNodeWireBudget);
        MIRAGE_CHECK(!exact_view->semantic->truncated);
        MIRAGE_CHECK(exact_view->semantic->nodes.back().ref ==
                     "@e" + std::to_string(ipc::kObservationNodeWireBudget));
    } else if (exact_view != nullptr) {
        MIRAGE_CHECK(exact_view->semantic.has_value());
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_run_before_start_is_rejected() {
    mirage::testing::TempDir dir;
    RuntimeService service(make_config(dir));

    const ServiceRunReport report = service.run();
    MIRAGE_CHECK(!report.clean);
    MIRAGE_CHECK(!report.diagnostic.empty());
}

void scenario_start_rejects_null_binding_fail_closed() {
    mirage::testing::TempDir dir;
    RuntimeService service(make_config(dir));

    const auto refused = service.start(std::shared_ptr<integration::DesktopEnvironmentBinding>{});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "invalid_argument");

    // Fail closed: the instance is terminal, a retry cannot revive it.
    const auto retry = service.start(make_binding(dir));
    MIRAGE_CHECK(!retry.ok);
    MIRAGE_CHECK(retry.error.code == "invalid_state");
}

void scenario_hello_identity_and_terminal_restart() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);

    MIRAGE_CHECK(service.start(make_binding(dir)).ok);
    // A second start on the live instance must be refused.
    const auto again = service.start(make_binding(dir));
    MIRAGE_CHECK(!again.ok);
    MIRAGE_CHECK(again.error.code == "invalid_state");

    ipc::IpcClient client(config.socket_path);
    const ipc::Response response = client.call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(response.ok);
    MIRAGE_CHECK(response.id == 1); // the client's correlation id
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&response.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->name == "mirage-runtime");
        MIRAGE_CHECK(identity->mirage_version == "0.4.0-test");
        MIRAGE_CHECK(identity->protocol == ipc::kProtocolVersion);
        MIRAGE_CHECK(identity->host_status == "running");
        MIRAGE_CHECK(looks_like_version_triple(identity->mira_core_version));
    }

    service.request_shutdown();
    const ServiceRunReport report = service.run();
    MIRAGE_CHECK(report.clean);
    MIRAGE_CHECK(report.host_shutdown.clean);
    MIRAGE_CHECK(!std::filesystem::exists(config.socket_path));

    // Terminal instance: restart and re-run are both refused.
    const auto restart = service.start(make_binding(dir));
    MIRAGE_CHECK(!restart.ok);
    MIRAGE_CHECK(restart.error.code == "invalid_state");
    const ServiceRunReport rerun = service.run();
    MIRAGE_CHECK(!rerun.clean);
    MIRAGE_CHECK(!rerun.diagnostic.empty());
}

void scenario_default_socket_path_resolution() {
    // Empty config selects the DEC-007 default endpoint without binding.
    RuntimeService service(ServiceConfig{});
    MIRAGE_CHECK(service.socket_path() == ipc::default_socket_path());
}

// --- task driving -----------------------------------------------------------

void scenario_task_completes_steps_with_operation_ids() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "m1-04 structured result " + token + "\n";
    write_text_file(goal_file, content);
    const std::string marker = "exec-marker-" + token;

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read the goal file and echo the marker";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "printf '" + marker + "'"});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(*task_id));

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        return;
    }
    MIRAGE_CHECK(done->progress == "Completed");
    MIRAGE_CHECK(done->has_success);
    MIRAGE_CHECK(done->success);
    MIRAGE_CHECK(done->id == *task_id);
    MIRAGE_CHECK(done->goal == request.goal);
    MIRAGE_CHECK(done->steps.size() == 2);
    if (done->steps.size() != 2) {
        return;
    }

    const ipc::StepView &read_step = done->steps[0];
    MIRAGE_CHECK(read_step.index == 0);
    MIRAGE_CHECK(read_step.kind == "filesystem.read");
    MIRAGE_CHECK(read_step.status == "ok");
    MIRAGE_CHECK(read_step.ok);
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(read_step.operation_id));
    MIRAGE_CHECK(read_step.result == content);
    MIRAGE_CHECK(!read_step.result_truncated);

    const ipc::StepView &exec_step = done->steps[1];
    MIRAGE_CHECK(exec_step.index == 1);
    MIRAGE_CHECK(exec_step.kind == "process.execute");
    MIRAGE_CHECK(exec_step.status == "ok");
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(exec_step.operation_id));
    MIRAGE_CHECK(exec_step.exit_code == 0);
    MIRAGE_CHECK(exec_step.result == marker);

    // Each desktop action entered the pinned control plane under its own
    // operation identity.
    MIRAGE_CHECK(read_step.operation_id != exec_step.operation_id);

    // task.list reports the settled task with its goal and progress.
    const ipc::Response list_response = client.call(ipc::ListTasksRequest{}, kCallBudget);
    MIRAGE_CHECK(list_response.ok);
    const auto *list = std::get_if<ipc::TaskList>(&list_response.payload);
    MIRAGE_CHECK(list != nullptr);
    if (list != nullptr) {
        bool found = false;
        for (const ipc::TaskSummary &summary : list->tasks) {
            if (summary.id == *task_id) {
                found = true;
                MIRAGE_CHECK(summary.goal == request.goal);
                MIRAGE_CHECK(summary.progress == "Completed");
            }
        }
        MIRAGE_CHECK(found);
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_task_fails_fast_and_skips_remainder() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path missing = dir.root() / ("missing-" + token + ".txt");
    const std::filesystem::path canary = dir.root() / ("canary-" + token + ".txt");

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "fail on the missing file before running the command";
    request.steps.push_back({ipc::StepKind::FilesystemRead, missing.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch " + canary.string()});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        return;
    }
    MIRAGE_CHECK(done->progress == "Failed");
    MIRAGE_CHECK(done->has_success);
    MIRAGE_CHECK(!done->success);
    MIRAGE_CHECK(done->steps.size() == 2);
    if (done->steps.size() != 2) {
        return;
    }

    const ipc::StepView &read_step = done->steps[0];
    MIRAGE_CHECK(read_step.status == "failed");
    MIRAGE_CHECK(!read_step.ok);
    MIRAGE_CHECK(read_step.error.find("not_found") != std::string::npos);
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(read_step.operation_id));

    const ipc::StepView &exec_step = done->steps[1];
    MIRAGE_CHECK(exec_step.status == "skipped");
    MIRAGE_CHECK(!exec_step.ok);

    // Fail-fast really skipped the step: the canary was never created.
    MIRAGE_CHECK(!std::filesystem::exists(canary));

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_step_result_truncated_to_cap() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.max_result_bytes = 16;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path file = dir.root() / ("wide-" + token + ".txt");
    const std::string content(64, 'w');
    write_text_file(file, content);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read a file larger than the result cap";
    request.steps.push_back({ipc::StepKind::FilesystemRead, file.string()});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done || done->steps.size() != 1) {
        MIRAGE_CHECK(done.has_value() && done->steps.size() == 1);
        return;
    }
    MIRAGE_CHECK(done->steps[0].status == "ok");
    MIRAGE_CHECK(done->steps[0].result.size() == 16);
    MIRAGE_CHECK(done->steps[0].result == content.substr(0, 16));
    MIRAGE_CHECK(done->steps[0].result_truncated);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- validation and capacity ------------------------------------------------

void scenario_submit_and_inspect_validation() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);

    // Unknown task id.
    {
        const ipc::Response response =
            client.call(ipc::InspectTaskRequest{"00000000000000000000000000000000"}, kCallBudget);
        MIRAGE_CHECK(!response.ok);
        MIRAGE_CHECK(response.error.code == "not_found");
    }
    // Empty goal: semantic rejection by the service layer (the wire payload
    // itself decodes fine), answered invalid_argument.
    {
        ipc::SubmitTaskRequest request;
        request.goal = "";
        const ipc::Response response = client.call(request, kCallBudget);
        MIRAGE_CHECK(!response.ok);
        MIRAGE_CHECK(response.error.code == "invalid_argument");
    }
    // Goal beyond the 8 KiB budget.
    {
        ipc::SubmitTaskRequest request;
        request.goal.assign(8 * 1024 + 8, 'g');
        const ipc::Response response = client.call(request, kCallBudget);
        MIRAGE_CHECK(!response.ok);
        MIRAGE_CHECK(response.error.code == "invalid_argument");
    }
    // More than the 64 scripted steps.
    {
        ipc::SubmitTaskRequest request;
        request.goal = "too many steps";
        for (std::size_t index = 0; index < 65; ++index) {
            request.steps.push_back({ipc::StepKind::ProcessExecute, "true"});
        }
        const ipc::Response response = client.call(request, kCallBudget);
        MIRAGE_CHECK(!response.ok);
        MIRAGE_CHECK(response.error.code == "invalid_argument");
    }
    // Step argument beyond its 4 KiB budget.
    {
        ipc::SubmitTaskRequest request;
        request.goal = "argument too long";
        request.steps.push_back({ipc::StepKind::ProcessExecute, std::string(4200, 'x')});
        const ipc::Response response = client.call(request, kCallBudget);
        MIRAGE_CHECK(!response.ok);
        MIRAGE_CHECK(response.error.code == "invalid_argument");
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_registry_capacity_rejects_third_task() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.max_task_records = 2;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "registry filler";

    const std::optional<std::string> first = submit_task(client, request);
    MIRAGE_CHECK(first.has_value());
    const std::optional<std::string> second = submit_task(client, request);
    MIRAGE_CHECK(second.has_value());

    // The registry keeps settled records too, so the third submission is a
    // bounded rejection, not an eviction.
    ipc::SubmitTaskRequest third_request;
    third_request.goal = "over capacity";
    const ipc::Response third = client.call(third_request, kCallBudget);
    MIRAGE_CHECK(!third.ok);
    MIRAGE_CHECK(third.error.code == "invalid_state");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- connection multiplexing ------------------------------------------------

void scenario_sequential_requests_on_one_connection() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    std::string diagnostic;
    ipc::IpcStream stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(stream.valid());
    if (!stream.valid()) {
        return;
    }

    // Two requests in sequence on the same socket, correlation ids intact.
    for (const std::uint64_t id : {std::uint64_t{10}, std::uint64_t{11}}) {
        MIRAGE_CHECK(mirage::testing::write_all(
            stream, ipc::make_frame(ipc::encode_request(id, ipc::HelloRequest{})), kCallBudget));
        std::string buffer;
        const FrameRead read = mirage::testing::read_frame(stream, buffer, kCallBudget);
        MIRAGE_CHECK(read.status == FrameRead::Status::Message);
        if (read.status != FrameRead::Status::Message) {
            return;
        }
        const ipc::ResponseDecode decoded = ipc::decode_response(read.message);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.response.id == id);
        MIRAGE_CHECK(decoded.response.ok);
        MIRAGE_CHECK(std::holds_alternative<ipc::ServiceIdentity>(decoded.response.payload));
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_interleaved_connections_keep_correlation() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    std::string diagnostic;
    ipc::IpcStream one = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    ipc::IpcStream two = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    ipc::IpcStream three = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(one.valid() && two.valid() && three.valid());
    if (!one.valid() || !two.valid() || !three.valid()) {
        return;
    }

    // All three clients send before any of them reads; the service must keep
    // the responses correlated per connection.
    MIRAGE_CHECK(mirage::testing::write_all(
        one, ipc::make_frame(ipc::encode_request(31, ipc::HelloRequest{})), kCallBudget));
    MIRAGE_CHECK(mirage::testing::write_all(
        two, ipc::make_frame(ipc::encode_request(32, ipc::HelloRequest{})), kCallBudget));
    MIRAGE_CHECK(mirage::testing::write_all(
        three, ipc::make_frame(ipc::encode_request(33, ipc::ListTasksRequest{})), kCallBudget));

    const std::pair<ipc::IpcStream *, std::uint64_t> expected[] = {
        {&one, 31}, {&two, 32}, {&three, 33}};
    for (const auto &[stream, id] : expected) {
        std::string buffer;
        const FrameRead read = mirage::testing::read_frame(*stream, buffer, kCallBudget);
        MIRAGE_CHECK(read.status == FrameRead::Status::Message);
        if (read.status != FrameRead::Status::Message) {
            return;
        }
        const ipc::ResponseDecode decoded = ipc::decode_response(read.message);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.response.id == id);
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- framing violations on the wire -----------------------------------------

void scenario_garbage_payload_yields_protocol_error_then_close() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    std::string diagnostic;
    ipc::IpcStream stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(stream.valid());
    if (!stream.valid()) {
        return;
    }

    MIRAGE_CHECK(mirage::testing::write_all(stream, ipc::make_frame("this is definitely not json"),
                                            kCallBudget));
    std::string buffer;
    const FrameRead read = mirage::testing::read_frame(stream, buffer, kCallBudget);
    if (read.status != FrameRead::Status::Message) {
        MIRAGE_CHECK(false); // the service must answer with an error frame first
        return;
    }
    const ipc::ResponseDecode decoded = ipc::decode_response(read.message);
    MIRAGE_CHECK(decoded.ok);
    MIRAGE_CHECK(!decoded.response.ok);
    MIRAGE_CHECK(decoded.response.error.code == "protocol_error");

    // The connection is closed after the protocol violation.
    const FrameRead closing = mirage::testing::read_frame(stream, buffer, kCallBudget);
    MIRAGE_CHECK(closing.status == FrameRead::Status::Closed);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_pipelined_request_rejected() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    std::string diagnostic;
    ipc::IpcStream stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(stream.valid());
    if (!stream.valid()) {
        return;
    }

    // Two requests sent before the first response is read: DEC-007 allows at
    // most one outstanding request per connection, so the service answers
    // with a protocol error and closes.
    const std::string pipeline = ipc::make_frame(ipc::encode_request(41, ipc::HelloRequest{})) +
                                 ipc::make_frame(ipc::encode_request(42, ipc::HelloRequest{}));
    MIRAGE_CHECK(mirage::testing::write_all(stream, pipeline, kCallBudget));

    std::string buffer;
    const FrameRead read = mirage::testing::read_frame(stream, buffer, kCallBudget);
    if (read.status != FrameRead::Status::Message) {
        MIRAGE_CHECK(false); // expected a protocol_error frame before the close
        return;
    }
    const ipc::ResponseDecode decoded = ipc::decode_response(read.message);
    MIRAGE_CHECK(decoded.ok);
    MIRAGE_CHECK(!decoded.response.ok);
    MIRAGE_CHECK(decoded.response.error.code == "protocol_error");

    const FrameRead closing = mirage::testing::read_frame(stream, buffer, kCallBudget);
    MIRAGE_CHECK(closing.status == FrameRead::Status::Closed);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_oversized_frame_closes_connection() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    std::string diagnostic;
    ipc::IpcStream stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(stream.valid());
    if (!stream.valid()) {
        return;
    }

    // A header declaring twice the frame cap is a protocol violation: the
    // service answers with a protocol_error frame, then closes (DEC-007
    // framing rules; the error frame precedes the close).
    std::string header;
    const std::uint32_t declared = 2 * static_cast<std::uint32_t>(ipc::kMaxFrameBytes);
    header.push_back(static_cast<char>(declared & 0xFFu));
    header.push_back(static_cast<char>((declared >> 8) & 0xFFu));
    header.push_back(static_cast<char>((declared >> 16) & 0xFFu));
    header.push_back(static_cast<char>((declared >> 24) & 0xFFu));
    MIRAGE_CHECK(mirage::testing::write_all(stream, header, kCallBudget));

    std::string buffer;
    const FrameRead read = mirage::testing::read_frame(stream, buffer, kCallBudget);
    if (read.status != FrameRead::Status::Message) {
        MIRAGE_CHECK(false); // expected the protocol_error frame before the close
        return;
    }
    const ipc::ResponseDecode decoded = ipc::decode_response(read.message);
    MIRAGE_CHECK(decoded.ok);
    MIRAGE_CHECK(!decoded.response.ok);
    MIRAGE_CHECK(decoded.response.error.code == "protocol_error");

    const FrameRead closing = mirage::testing::read_frame(stream, buffer, kCallBudget);
    MIRAGE_CHECK(closing.status == FrameRead::Status::Closed);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- shutdown semantics -----------------------------------------------------

void scenario_shutdown_via_ipc_after_completed_task() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "quick task before shutdown";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "true"});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());

    const std::optional<ipc::InspectTask> done =
        task_id ? wait_terminal(config.socket_path, *task_id) : std::nullopt;
    MIRAGE_CHECK(done.has_value() && done->progress == "Completed");

    const ipc::Response ack = client.call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(ack.ok);
    MIRAGE_CHECK(std::holds_alternative<ipc::ShutdownAccepted>(ack.payload));

    const ServiceRunReport report = service.run();
    MIRAGE_CHECK(report.clean);
    MIRAGE_CHECK(report.host_shutdown.clean);
    MIRAGE_CHECK(!std::filesystem::exists(config.socket_path));

    const auto restart = service.start(make_binding(dir));
    MIRAGE_CHECK(!restart.ok);
    MIRAGE_CHECK(restart.error.code == "invalid_state");
}

void scenario_shutdown_with_inflight_task_is_bounded() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "long task cancelled by shutdown";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 2"});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    // Let the driver enter the sleep step before shutting down.
    std::this_thread::sleep_for(std::chrono::milliseconds{300});

    const auto started = std::chrono::steady_clock::now();
    const ipc::Response ack = client.call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(ack.ok);
    MIRAGE_CHECK(std::holds_alternative<ipc::ShutdownAccepted>(ack.payload));

    const ServiceRunReport report = service.run();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    // Teardown must converge promptly instead of waiting out the step budget.
    MIRAGE_CHECK(elapsed < std::chrono::seconds{10});
    MIRAGE_CHECK(report.clean);
    MIRAGE_CHECK(!std::filesystem::exists(config.socket_path));
}

void scenario_shutdown_fd_triggers_stop() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);

    int pipe_fds[2] = {-1, -1};
    MIRAGE_CHECK(::pipe2(pipe_fds, O_NONBLOCK | O_CLOEXEC) == 0);
    service.register_shutdown_fd(pipe_fds[0]);

    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    // The signal self-pipe path: one byte written to the registered
    // descriptor stops the loop, exactly like SIGTERM in mirage-service.
    const char token = 's';
    MIRAGE_CHECK(::write(pipe_fds[1], &token, 1) == 1);

    const ServiceRunReport report = service.run();
    MIRAGE_CHECK(report.clean);
    MIRAGE_CHECK(!std::filesystem::exists(config.socket_path));

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[runtime_service_test] scenario: %s\n", name);
    scenario();
}

} // namespace

// --- M5-04 session faces (DEC-021) -------------------------------------------

void scenario_session_list_open_and_history_flow() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);

    // hello advertises the session face (DEC-021).
    const ipc::Response hello = client.call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(hello.ok);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->sessions.has_value());
        MIRAGE_CHECK(identity->sessions.value_or(false));
    }

    // The primary session is visible without any session.open.
    const ipc::Response first_list = client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(first_list.ok);
    const auto *primary_list = std::get_if<ipc::SessionList>(&first_list.payload);
    MIRAGE_CHECK(primary_list != nullptr);
    MIRAGE_CHECK(primary_list != nullptr && primary_list->sessions.size() == 1);
    if (primary_list == nullptr || primary_list->sessions.size() != 1) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const std::string primary_id = primary_list->sessions[0].id;
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(primary_id));
    MIRAGE_CHECK(primary_list->sessions[0].created_at_ms > 0);
    MIRAGE_CHECK(primary_list->sessions[0].state == "autonomous" ||
                 primary_list->sessions[0].state == "opening");

    // session.open admits one more session.
    const ipc::Response opened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(opened.ok);
    const auto *opened_session = std::get_if<ipc::SessionOpened>(&opened.payload);
    MIRAGE_CHECK(opened_session != nullptr);
    if (opened_session == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(opened_session->session_id));
    MIRAGE_CHECK(opened_session->session_id != primary_id);

    const ipc::Response second_list = client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(second_list.ok);
    const auto *both = std::get_if<ipc::SessionList>(&second_list.payload);
    MIRAGE_CHECK(both != nullptr);
    MIRAGE_CHECK(both != nullptr && both->sessions.size() == 2);
    bool found_opened = false;
    if (both != nullptr) {
        for (const ipc::SessionSummary &session : both->sessions) {
            if (session.id == opened_session->session_id) {
                found_opened = true;
                MIRAGE_CHECK(session.created_at_ms > 0);
            }
        }
    }
    MIRAGE_CHECK(found_opened);

    // A task submitted into the session echoes the binding and lands its
    // conversation in that session's history.
    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path file = dir.root() / ("session-" + token + ".txt");
    const std::string content = "session conversation fixture " + token;
    write_text_file(file, content);
    ipc::SubmitTaskRequest submit;
    submit.goal = "read the session fixture";
    submit.session_id = opened_session->session_id;
    submit.steps.push_back({ipc::StepKind::FilesystemRead, file.string()});
    const ipc::Response submitted = client.call(submit, kCallBudget);
    MIRAGE_CHECK(submitted.ok);
    const auto *ack = std::get_if<ipc::TaskSubmitted>(&submitted.payload);
    MIRAGE_CHECK(ack != nullptr);
    if (ack == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    MIRAGE_CHECK(ack->session_id.has_value());
    MIRAGE_CHECK(ack->session_id.value_or("") == opened_session->session_id);

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, ack->task_id);
    MIRAGE_CHECK(done.has_value());
    MIRAGE_CHECK(done.has_value() && done->progress == "Completed");

    // The outcome message is appended by the driver thread after the task
    // record settles; poll the history face until the conversation shows
    // both entries (liveness guard, not a latency assertion).
    std::optional<ipc::SessionHistory> history;
    const auto history_deadline = std::chrono::steady_clock::now() + kTaskBudget;
    while (!history.has_value()) {
        const ipc::Response history_response =
            client.call(ipc::SessionHistoryRequest{opened_session->session_id, {}}, kCallBudget);
        MIRAGE_CHECK(history_response.ok);
        if (history_response.ok) {
            const auto *snapshot = std::get_if<ipc::SessionHistory>(&history_response.payload);
            MIRAGE_CHECK(snapshot != nullptr);
            if (snapshot != nullptr && snapshot->entries.size() == 2) {
                history = *snapshot;
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= history_deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    MIRAGE_CHECK(history.has_value());
    if (history.has_value()) {
        MIRAGE_CHECK(history->session_id == opened_session->session_id);
        MIRAGE_CHECK(!history->truncated);
        MIRAGE_CHECK(history->entries.size() == 2);
        if (history->entries.size() == 2) {
            const ipc::SessionHistoryEntry &user = history->entries[0];
            const ipc::SessionHistoryEntry &outcome = history->entries[1];
            MIRAGE_CHECK(user.kind == "user");
            MIRAGE_CHECK(user.text == submit.goal);
            MIRAGE_CHECK(user.sequence >= 1);
            MIRAGE_CHECK(user.recorded_at_ms > 0);
            MIRAGE_CHECK(outcome.kind == "outcome");
            MIRAGE_CHECK(outcome.text == "loop settled: Completed (steps 1)");
            MIRAGE_CHECK(outcome.sequence > user.sequence);
        }
    }

    // The requested limit selects the newest window and reports truncation.
    const ipc::Response window =
        client.call(ipc::SessionHistoryRequest{opened_session->session_id, 1}, kCallBudget);
    MIRAGE_CHECK(window.ok);
    const auto *windowed = std::get_if<ipc::SessionHistory>(&window.payload);
    MIRAGE_CHECK(windowed != nullptr);
    if (windowed != nullptr) {
        MIRAGE_CHECK(windowed->truncated);
        MIRAGE_CHECK(windowed->entries.size() == 1);
        MIRAGE_CHECK(!windowed->entries.empty() && windowed->entries[0].kind == "outcome");
    }

    // Unknown sessions fail closed on both faces.
    const ipc::Response unknown_history =
        client.call(ipc::SessionHistoryRequest{"no-such-session", {}}, kCallBudget);
    MIRAGE_CHECK(!unknown_history.ok);
    MIRAGE_CHECK(unknown_history.error.code == "not_found");
    ipc::SubmitTaskRequest unknown_submit;
    unknown_submit.goal = "nobody can admit this";
    unknown_submit.session_id = "no-such-session";
    const ipc::Response unknown_task = client.call(unknown_submit, kCallBudget);
    MIRAGE_CHECK(!unknown_task.ok);
    MIRAGE_CHECK(unknown_task.error.code == "not_found");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- session.chat dialog face (DEC-027) --------------------------------------

/// A scripted pinned model provider for the dialog face scene (DEC-027):
/// binds to the layer-assembled profile and answers every inference with a
/// fixed assistant text — the same hermetic pattern the pinned M14/M15
/// harnesses use (the production socket provider denies private/loopback
/// endpoints by design, so a local origin can never serve this test).
class ScriptedModelProvider final : public mira::IModelProvider {
  public:
    explicit ScriptedModelProvider(const mira::ModelProfile &profile) : profile_(profile) {}
    [[nodiscard]] const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        ++calls_;
        mira::ModelResponse response;
        response.contract_version = mira::SchemaVersion{1, 0};
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.requested_model = profile_.model_selector;
        response.status = mira::ModelCompletionStatus::Completed;
        mira::MessageOutput message;
        message.role = mira::ModelRole::Assistant;
        mira::OutputTextPart text;
        text.text = "桌面回复正常。";
        message.content.emplace_back(std::move(text));
        response.output.emplace_back(std::move(message));
        return response;
    }
    [[nodiscard]] int calls() const { return calls_.load(); }

  private:
    mira::ModelProfile profile_;
    std::atomic<int> calls_{0};
};

/// A gate-blocked scripted provider (DEC-027): an inference holds inside
/// infer() until the test releases it — the window the in-flight latch and
/// the pending history shape live in — and later inferences return a
/// scripted outcome (the canned reply, or a Failed status), so the failure
/// settlement path is reachable deterministically. The cancellation probe is
/// honored so teardown is never blocked.
class GatedModelProvider final : public mira::IModelProvider {
  public:
    GatedModelProvider() = default;
    [[nodiscard]] const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &context,
                                            const mira::ProviderInferOptions &) override {
        ++calls_;
        while (hold_.load() && !context.cancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        if (context.cancelled()) {
            mira::ModelResponse cancelled;
            cancelled.contract_version = mira::SchemaVersion{1, 0};
            cancelled.request_id = request.request_id;
            cancelled.operation_id = request.operation_id;
            cancelled.profile_id = profile_.id;
            cancelled.requested_model = profile_.model_selector;
            cancelled.status = mira::ModelCompletionStatus::Cancelled;
            return cancelled;
        }
        mira::ModelResponse response;
        response.contract_version = mira::SchemaVersion{1, 0};
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.requested_model = profile_.model_selector;
        if (fail_after_release_.load()) {
            response.status = mira::ModelCompletionStatus::Failed;
            return response;
        }
        response.status = mira::ModelCompletionStatus::Completed;
        mira::MessageOutput message;
        message.role = mira::ModelRole::Assistant;
        mira::OutputTextPart text;
        text.text = "门控回复正常。";
        message.content.emplace_back(std::move(text));
        response.output.emplace_back(std::move(message));
        return response;
    }

    void hold_open() { hold_.store(true); }
    void release() { hold_.store(false); }
    void fail_subsequent() { fail_after_release_.store(true); }

    /// Binds the layer-assembled profile (the override factory receives it).
    void bind_profile(const mira::ModelProfile &profile) { profile_ = profile; }

  private:
    mira::ModelProfile profile_;
    std::atomic<int> calls_{0};
    std::atomic<bool> hold_{false};
    std::atomic<bool> fail_after_release_{false};
};

/// The dialog latch and failure settlement (DEC-027): a second session.chat
/// while one turn's model call is in flight is refused invalid_state, the
/// pending turn's history entry carries neither reply nor error, a Failed
/// model status settles the turn "failed" with the stable error, and every
/// settlement — ok or failed — clears the latch so the next turn accepts.
/// The 16 KiB text budget refuses instead of cropping.
void scenario_session_chat_latch_and_failure() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.model.enabled = true;
    config.model.dialect = "openai.responses.v1";
    config.model.model_selector = "test-model";
    auto gated_provider = std::make_shared<GatedModelProvider>();
    const auto scripted = std::make_shared<mirage::integration::ModelProviderOverride>(
        [&gated_provider](
            const mira::ModelProfile &profile) -> std::shared_ptr<mira::IModelProvider> {
            gated_provider->bind_profile(profile);
            return gated_provider;
        });
    config.model_provider_override = scripted;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response opened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(opened.ok);
    const auto *opened_session = std::get_if<ipc::SessionOpened>(&opened.payload);
    MIRAGE_CHECK(opened_session != nullptr);
    if (opened_session == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const std::string session_id = opened_session->session_id;

    // Over-budget text is refused before any turn is registered.
    const std::string oversized(16 * 1024 + 1, 'x');
    const ipc::Response oversized_reply =
        client.call(ipc::SessionChatRequest{session_id, oversized}, kCallBudget);
    MIRAGE_CHECK(!oversized_reply.ok);
    MIRAGE_CHECK(oversized_reply.error.code == "invalid_argument");
    MIRAGE_CHECK(oversized_reply.error.message.find("byte budget") != std::string::npos);

    // Turn 1 enters the model call and stays there (the gate is closed).
    gated_provider->hold_open();
    const ipc::Response accepted =
        client.call(ipc::SessionChatRequest{session_id, "第一个问题"}, kCallBudget);
    MIRAGE_CHECK(accepted.ok);
    const auto *turn = std::get_if<ipc::DialogTurnAccepted>(&accepted.payload);
    MIRAGE_CHECK(turn != nullptr);
    if (turn == nullptr) {
        gated_provider->release();
        service.request_shutdown();
        (void)service.run();
        return;
    }

    // The in-flight latch refuses a second turn for the same session.
    const ipc::Response latch =
        client.call(ipc::SessionChatRequest{session_id, "第二个问题"}, kCallBudget);
    MIRAGE_CHECK(!latch.ok);
    MIRAGE_CHECK(latch.error.code == "invalid_state");
    MIRAGE_CHECK(latch.error.message == "a dialog turn is already in flight for this session");

    // The pending history entry carries neither reply nor error.
    const ipc::Response pending_snapshot =
        client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
    MIRAGE_CHECK(pending_snapshot.ok);
    const auto *pending_history = std::get_if<ipc::DialogHistory>(&pending_snapshot.payload);
    MIRAGE_CHECK(pending_history != nullptr);
    if (pending_history != nullptr) {
        MIRAGE_CHECK(pending_history->turns.size() == 1);
        if (pending_history->turns.size() == 1) {
            const ipc::DialogTurnEntry &entry = pending_history->turns[0];
            MIRAGE_CHECK(entry.turn_id == turn->turn_id);
            MIRAGE_CHECK(entry.status == "pending");
            MIRAGE_CHECK(!entry.has_reply);
            MIRAGE_CHECK(!entry.has_error);
        }
    }

    // Release the gate: the turn settles ok, the latch clears, and the
    // follow-up turn accepts.
    gated_provider->release();
    std::optional<ipc::DialogTurnEntry> settled;
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    while (!settled.has_value()) {
        const ipc::Response snapshot =
            client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
        MIRAGE_CHECK(snapshot.ok);
        if (snapshot.ok) {
            const auto *dialog = std::get_if<ipc::DialogHistory>(&snapshot.payload);
            MIRAGE_CHECK(dialog != nullptr);
            if (dialog != nullptr && dialog->turns.size() == 1 && dialog->turns[0].status == "ok") {
                settled = dialog->turns[0];
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    MIRAGE_CHECK(settled.has_value());
    if (settled.has_value()) {
        MIRAGE_CHECK(settled->turn_id == turn->turn_id);
        MIRAGE_CHECK(settled->has_reply);
        MIRAGE_CHECK(settled->reply_text == "门控回复正常。");
        MIRAGE_CHECK(!settled->has_error);
    }

    // Subsequent turns run in failure mode: the Failed model status settles
    // the turn "failed" with the stable error, and the latch still clears.
    gated_provider->fail_subsequent();
    const ipc::Response second =
        client.call(ipc::SessionChatRequest{session_id, "第二个问题"}, kCallBudget);
    MIRAGE_CHECK(second.ok);
    const auto *second_turn = std::get_if<ipc::DialogTurnAccepted>(&second.payload);
    MIRAGE_CHECK(second_turn != nullptr);
    if (second_turn == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    std::optional<ipc::DialogTurnEntry> failed;
    const auto failed_deadline = std::chrono::steady_clock::now() + kTaskBudget;
    while (!failed.has_value()) {
        const ipc::Response snapshot =
            client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
        MIRAGE_CHECK(snapshot.ok);
        if (snapshot.ok) {
            const auto *dialog = std::get_if<ipc::DialogHistory>(&snapshot.payload);
            MIRAGE_CHECK(dialog != nullptr);
            if (dialog != nullptr && dialog->turns.size() == 2 &&
                dialog->turns[1].status == "failed") {
                failed = dialog->turns[1];
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= failed_deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    MIRAGE_CHECK(failed.has_value());
    if (failed.has_value()) {
        MIRAGE_CHECK(failed->turn_id == second_turn->turn_id);
        MIRAGE_CHECK(failed->sequence == 2);
        MIRAGE_CHECK(failed->has_error);
        MIRAGE_CHECK(failed->error.find("model layer did not complete the turn") !=
                     std::string::npos);
        MIRAGE_CHECK(!failed->has_reply);
    }

    // The failed settlement cleared the latch: the next turn accepts (and
    // fails the same way; the settle wait below keeps teardown clean).
    const ipc::Response third =
        client.call(ipc::SessionChatRequest{session_id, "第三个问题"}, kCallBudget);
    MIRAGE_CHECK(third.ok);
    const auto third_deadline = std::chrono::steady_clock::now() + kTaskBudget;
    for (;;) {
        const ipc::Response snapshot =
            client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
        MIRAGE_CHECK(snapshot.ok);
        if (snapshot.ok) {
            const auto *dialog = std::get_if<ipc::DialogHistory>(&snapshot.payload);
            MIRAGE_CHECK(dialog != nullptr);
            if (dialog != nullptr && dialog->turns.size() == 3 &&
                dialog->turns[2].status == "failed") {
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= third_deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_session_chat_dialog_face() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.model.enabled = true;
    config.model.dialect = "openai.responses.v1";
    config.model.model_selector = "test-model";
    const auto scripted = std::make_shared<mirage::integration::ModelProviderOverride>(
        [](const mira::ModelProfile &profile) -> std::shared_ptr<mira::IModelProvider> {
            return std::make_shared<ScriptedModelProvider>(profile);
        });
    config.model_provider_override = scripted;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response hello = client.call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(hello.ok);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->chat.has_value());
        MIRAGE_CHECK(identity->chat.value_or(false));
    }

    const ipc::Response opened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(opened.ok);
    const auto *opened_session = std::get_if<ipc::SessionOpened>(&opened.payload);
    MIRAGE_CHECK(opened_session != nullptr);
    if (opened_session == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const std::string session_id = opened_session->session_id;

    // Unknown sessions are refused before any model work.
    const ipc::Response unknown =
        client.call(ipc::SessionChatRequest{"no-such-session", "hi"}, kCallBudget);
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "not_found");

    // The dialog turn is accepted (pending) and settles ok with the canned
    // reply text; the history snapshot converges to the same record.
    const ipc::Response accepted =
        client.call(ipc::SessionChatRequest{session_id, "列出当前桌面上打开的应用"}, kCallBudget);
    MIRAGE_CHECK(accepted.ok);
    const auto *turn = std::get_if<ipc::DialogTurnAccepted>(&accepted.payload);
    MIRAGE_CHECK(turn != nullptr);
    if (turn == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(turn->turn_id));

    std::optional<ipc::DialogHistory> history;
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    while (!history.has_value()) {
        const ipc::Response snapshot =
            client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
        MIRAGE_CHECK(snapshot.ok);
        if (snapshot.ok) {
            const auto *dialog = std::get_if<ipc::DialogHistory>(&snapshot.payload);
            MIRAGE_CHECK(dialog != nullptr);
            if (dialog != nullptr && dialog->turns.size() == 1 && dialog->turns[0].status == "ok") {
                history = *dialog;
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    MIRAGE_CHECK(history.has_value());
    if (history.has_value()) {
        MIRAGE_CHECK(history->session_id == session_id);
        MIRAGE_CHECK(!history->truncated);
        MIRAGE_CHECK(history->turns.size() == 1);
        const ipc::DialogTurnEntry &settled = history->turns[0];
        MIRAGE_CHECK(settled.turn_id == turn->turn_id);
        MIRAGE_CHECK(settled.status == "ok");
        MIRAGE_CHECK(settled.user_text == "列出当前桌面上打开的应用");
        MIRAGE_CHECK(settled.has_reply);
        MIRAGE_CHECK(settled.reply_text == "桌面回复正常。");
        MIRAGE_CHECK(!settled.has_error);
        MIRAGE_CHECK(settled.sequence == 1);
    }

    // The settled turn cleared the in-flight latch: the next turn accepts.
    const ipc::Response second =
        client.call(ipc::SessionChatRequest{session_id, "第二个问题"}, kCallBudget);
    MIRAGE_CHECK(second.ok);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- session.close (DEC-026 backlog item 2) ----------------------------------

/// Regression (independent verification, M5-06 round 4): session.close must
/// remove the session's dialog registry entry — without it, the bounded
/// dialog registry (capacity = max_sessions) fills with dead threads and the
/// dialog face goes permanently `unavailable` for every new session after
/// max_sessions open→chat→close cycles.
/// M5-07 (DEC-020 / DEC-011 evolution): the policy face — policy.get
/// reports the full DEC-010 rule set, policy.set applies it live (the next
/// gated action obeys the new rule) and persists the merged document to the
/// settings store; the document decodes back with the new rules.
void scenario_policy_face_get_set_persists() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.read_roots = {"/home/user/work"};
    config.settings_directory = dir.root() / "settings";
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response hello = client.call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(hello.ok);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->policy.has_value());
        MIRAGE_CHECK(identity->policy.value_or(false));
    }

    // policy.get: the full DEC-010 set with the service defaults (DEC-010:
    // read/execute allowed, filesystem.write denied).
    const ipc::Response got = client.call(ipc::GetPolicyRequest{}, kCallBudget);
    MIRAGE_CHECK(got.ok);
    const auto *view = std::get_if<ipc::PolicyView>(&got.payload);
    MIRAGE_CHECK(view != nullptr);
    MIRAGE_CHECK(view != nullptr && view->rules.size() == 11);
    if (view != nullptr && view->rules.size() == 11) {
        MIRAGE_CHECK(view->rules.at("filesystem.read") == "allow");
        MIRAGE_CHECK(view->rules.at("filesystem.write") == "deny");
        MIRAGE_CHECK(view->rules.at("input.inject") == "allow");
    }
    MIRAGE_CHECK(view != nullptr && view->read_roots.size() == 1 &&
                 view->read_roots[0] == "/home/user/work");

    // policy.set: tighten clipboard.write to deny and drop the read root.
    // The wire decode validated the full coverage; the live effect is
    // observable through the persisted document (and the next gated action).
    ipc::SetPolicyRequest set_policy;
    set_policy.rules = view->rules;
    set_policy.rules["clipboard.write"] = "deny";
    set_policy.read_roots = {};
    set_policy.has_read_roots = true;
    const ipc::Response applied = client.call(set_policy, kCallBudget);
    MIRAGE_CHECK(applied.ok);
    const auto *applied_view = std::get_if<ipc::PolicyView>(&applied.payload);
    MIRAGE_CHECK(applied_view != nullptr);
    if (applied_view != nullptr) {
        MIRAGE_CHECK(applied_view->rules.at("clipboard.write") == "deny");
        MIRAGE_CHECK(applied_view->read_roots.empty());
    }

    // The persisted document carries the new rules (read-modify-write kept
    // the store authoritative for the next start).
    const mirage::runtime::persistence::LocalStateStore store(
        config.settings_directory, config.settings_file_name,
        mirage::runtime::persistence::kMaxSettingsFileBytes);
    const auto loaded = store.load();
    MIRAGE_CHECK(loaded.status == mirage::runtime::persistence::LoadStatus::Loaded);
    const auto decoded = mirage::runtime::persistence::decode_settings(loaded.body);
    MIRAGE_CHECK(decoded.ok);
    if (decoded.ok) {
        MIRAGE_CHECK(decoded.settings.permission_rules.at("clipboard.write") == "deny");
        MIRAGE_CHECK(decoded.settings.read_roots.empty());
    }

    // Partial coverage is refused without touching anything.
    ipc::SetPolicyRequest partial_request;
    partial_request.rules.insert_or_assign("filesystem.read", "allow");
    const ipc::Response partial = client.call(partial_request, kCallBudget);
    MIRAGE_CHECK(!partial.ok);
    MIRAGE_CHECK(partial.error.code == "invalid_argument");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// M5-08 session state across restarts (DEC-021 backlog ①, service-level
/// round trip): a session opened on one service instance — with a settled
/// dialog turn and a completed task's journal entries — is hydrated by the
/// next instance from the same state directory (registry + conversation +
/// dialog thread), the rebuilt dialog thread continues its sequence, and a
/// closed session stays closed across the restart (the close must re-persist
/// the snapshot — regression for the round-1 persist-point finding).
void scenario_session_state_round_trip_across_restart() {
    mirage::testing::TempDir dir;
    auto make_local_config = [&dir]() {
        ServiceConfig config = make_config(dir);
        config.persist_session_state = true;
        config.session_state_directory = dir.root() / "state";
        config.model.enabled = true;
        config.model.dialect = "openai.responses.v1";
        config.model.model_selector = "test-model";
        return config;
    };
    ServiceConfig config = make_local_config();
    const auto scripted = std::make_shared<mirage::integration::ModelProviderOverride>(
        [](const mira::ModelProfile &profile) -> std::shared_ptr<mira::IModelProvider> {
            return std::make_shared<ScriptedModelProvider>(profile);
        });
    config.model_provider_override = scripted;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response opened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(opened.ok);
    const auto *opened_session = std::get_if<ipc::SessionOpened>(&opened.payload);
    MIRAGE_CHECK(opened_session != nullptr);
    if (opened_session == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const std::string session_id = opened_session->session_id;

    // One settled dialog turn and one completed task on the session.
    const ipc::Response accepted =
        client.call(ipc::SessionChatRequest{session_id, "重启前的问题"}, kCallBudget);
    MIRAGE_CHECK(accepted.ok);
    const auto *turn = std::get_if<ipc::DialogTurnAccepted>(&accepted.payload);
    MIRAGE_CHECK(turn != nullptr);
    if (turn == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const auto chat_deadline = std::chrono::steady_clock::now() + kTaskBudget;
    bool settled = false;
    while (!settled) {
        const ipc::Response snapshot =
            client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
        MIRAGE_CHECK(snapshot.ok);
        if (snapshot.ok) {
            const auto *dialog = std::get_if<ipc::DialogHistory>(&snapshot.payload);
            MIRAGE_CHECK(dialog != nullptr);
            if (dialog != nullptr && dialog->turns.size() == 1 && dialog->turns[0].status == "ok") {
                settled = true;
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= chat_deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    MIRAGE_CHECK(settled);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path note = dir.root() / ("note-" + token + ".txt");
    write_text_file(note, "session state fixture " + token + "\n");
    ipc::SubmitTaskRequest submit;
    submit.goal = "跨重启任务";
    submit.session_id = session_id;
    submit.steps.push_back({ipc::StepKind::FilesystemRead, note.string()});
    const std::optional<std::string> task_id = submit_task(client, submit);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const auto done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done.has_value()) {
        MIRAGE_CHECK(done->progress == "Completed");
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);

    // Second instance over the same state directory: the session, its
    // conversation and its dialog thread come back.
    RuntimeService second(config);
    MIRAGE_CHECK(second.start(make_binding(dir)).ok);
    ipc::IpcClient second_client(config.socket_path);

    const ipc::Response listed = second_client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(listed.ok);
    const auto *sessions = std::get_if<ipc::SessionList>(&listed.payload);
    MIRAGE_CHECK(sessions != nullptr);
    bool hydrated = false;
    if (sessions != nullptr) {
        for (const ipc::SessionSummary &entry : sessions->sessions) {
            hydrated = hydrated || entry.id == session_id;
        }
    }
    MIRAGE_CHECK(hydrated);

    // The conversation journal was re-appended in order: user goal first,
    // outcome after.
    const ipc::Response history =
        second_client.call(ipc::SessionHistoryRequest{session_id, {}}, kCallBudget);
    MIRAGE_CHECK(history.ok);
    const auto *conversation = std::get_if<ipc::SessionHistory>(&history.payload);
    MIRAGE_CHECK(conversation != nullptr);
    if (conversation != nullptr) {
        MIRAGE_CHECK(conversation->entries.size() == 2);
        if (conversation->entries.size() == 2) {
            MIRAGE_CHECK(conversation->entries[0].kind == "user");
            MIRAGE_CHECK(conversation->entries[0].text == "跨重启任务");
            MIRAGE_CHECK(conversation->entries[1].kind == "outcome");
        }
    }

    // The dialog thread came back with the persisted turn...
    const ipc::Response dialog =
        second_client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
    MIRAGE_CHECK(dialog.ok);
    const auto *dialog_history = std::get_if<ipc::DialogHistory>(&dialog.payload);
    MIRAGE_CHECK(dialog_history != nullptr);
    if (dialog_history != nullptr) {
        MIRAGE_CHECK(dialog_history->turns.size() == 1);
        if (dialog_history->turns.size() == 1) {
            MIRAGE_CHECK(dialog_history->turns[0].turn_id == turn->turn_id);
            MIRAGE_CHECK(dialog_history->turns[0].status == "ok");
            MIRAGE_CHECK(dialog_history->turns[0].reply_text == "桌面回复正常。");
            MIRAGE_CHECK(dialog_history->turns[0].sequence == 1);
        }
    }

    // ...and a fresh turn continues the persisted sequence.
    const ipc::Response followup =
        second_client.call(ipc::SessionChatRequest{session_id, "重启后追问"}, kCallBudget);
    MIRAGE_CHECK(followup.ok);
    const auto follow_deadline = std::chrono::steady_clock::now() + kTaskBudget;
    bool follow_settled = false;
    while (!follow_settled) {
        const ipc::Response snapshot =
            second_client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
        MIRAGE_CHECK(snapshot.ok);
        if (snapshot.ok) {
            const auto *next = std::get_if<ipc::DialogHistory>(&snapshot.payload);
            MIRAGE_CHECK(next != nullptr);
            if (next != nullptr && next->turns.size() == 2 && next->turns[1].status == "ok") {
                MIRAGE_CHECK(next->turns[1].sequence == 2);
                MIRAGE_CHECK(next->turns[1].user_text == "重启后追问");
                follow_settled = true;
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= follow_deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    MIRAGE_CHECK(follow_settled);

    // Close-does-not-resurrect regression (round-2 finding): the hydrated
    // session is closed on the second instance — the close is service-side
    // (the pinned counterpart is gone with the previous era) — then a third
    // instance over the same state directory must NOT bring it back: the
    // session is absent from session.list and its faces answer not_found.
    const ipc::Response closed =
        second_client.call(ipc::CloseSessionRequest{session_id}, kCallBudget);
    MIRAGE_CHECK(closed.ok);
    const auto *closed_session = std::get_if<ipc::SessionClosed>(&closed.payload);
    MIRAGE_CHECK(closed_session != nullptr);
    if (closed_session != nullptr) {
        MIRAGE_CHECK(closed_session->session_id == session_id);
        MIRAGE_CHECK(closed_session->state == "closed");
    }

    second.request_shutdown();
    MIRAGE_CHECK(second.run().clean);

    RuntimeService third(config);
    MIRAGE_CHECK(third.start(make_binding(dir)).ok);
    ipc::IpcClient third_client(config.socket_path);

    const ipc::Response third_listed = third_client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(third_listed.ok);
    const auto *third_sessions = std::get_if<ipc::SessionList>(&third_listed.payload);
    MIRAGE_CHECK(third_sessions != nullptr);
    bool resurrected = false;
    if (third_sessions != nullptr) {
        for (const ipc::SessionSummary &entry : third_sessions->sessions) {
            resurrected = resurrected || entry.id == session_id;
        }
    }
    MIRAGE_CHECK(!resurrected);

    const ipc::Response ghost_history =
        third_client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
    MIRAGE_CHECK(!ghost_history.ok);
    MIRAGE_CHECK(ghost_history.error.code == "not_found");
    const ipc::Response ghost_chat =
        third_client.call(ipc::SessionChatRequest{session_id, "hi"}, kCallBudget);
    MIRAGE_CHECK(!ghost_chat.ok);
    MIRAGE_CHECK(ghost_chat.error.code == "not_found");

    third.request_shutdown();
    MIRAGE_CHECK(third.run().clean);
}

/// M5-08: a corrupt session state document degrades loudly (DEC-011
/// posture) — the instance starts, hydrates nothing, and the stale
/// session's faces answer not_found.
/// NOTE (independent verification, M5-08 round 1): the hydration round-trip
/// (open + dialog turn + task on one instance, rehydrated by the next) is
/// NOT asserted yet — the decoder rejects every document carrying journal
/// entries (session_state.cpp reads the entry object as the 'kind' string),
/// so hydration loud-degrades for any task-bearing session. The round-trip
/// assertions land together with the fix.
void scenario_session_state_corrupt_document_degrades_loudly() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.persist_session_state = true;
    config.session_state_directory = dir.root() / "state";
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);

    // A malformed document in the state directory must not block the start.
    write_text_file(config.session_state_directory / "session-state.json", "{oops");
    RuntimeService second(config);
    MIRAGE_CHECK(second.start(make_binding(dir)).ok);
    ipc::IpcClient client(config.socket_path);
    const ipc::Response listed = client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(listed.ok);
    const auto *sessions = std::get_if<ipc::SessionList>(&listed.payload);
    MIRAGE_CHECK(sessions != nullptr);
    if (sessions != nullptr) {
        // Nothing was hydrated from the corrupt document: only the primary
        // session (32-hex id) is registry-resident.
        std::size_t non_primary = 0;
        for (const ipc::SessionSummary &entry : sessions->sessions) {
            MIRAGE_CHECK(entry.id.size() == 32);
        }
        MIRAGE_CHECK(non_primary == 0);
    }
    const ipc::Response gone =
        client.call(ipc::ChatHistoryRequest{"5a4b3c2d1e0f4938576a5b4c3d2e1f0a", {}}, kCallBudget);
    MIRAGE_CHECK(!gone.ok);
    MIRAGE_CHECK(gone.error.code == "not_found");
    second.request_shutdown();
    MIRAGE_CHECK(second.run().clean);
}

/// M5-07 rules-immediately-effective plus the DEC-011 fail-closed write
/// posture: a policy.set rule governs the very next gated desktop action
/// (the driver and the policy face share one controller), an absent
/// read_roots member keeps the persisted scope, and a corrupt settings
/// document refuses the WRITE while the applied rules stay live — the
/// response reports the persistence failure, not a silently rolled-back
/// policy.
void scenario_policy_set_live_effect_and_fail_closed_persist() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.read_roots = {"/home/user/work"};
    config.settings_directory = dir.root() / "settings";
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response baseline = client.call(ipc::GetPolicyRequest{}, kCallBudget);
    MIRAGE_CHECK(baseline.ok);
    const auto *baseline_view = std::get_if<ipc::PolicyView>(&baseline.payload);
    MIRAGE_CHECK(baseline_view != nullptr);
    if (baseline_view == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }

    // Set with the read_roots member absent: the persisted scope is kept.
    ipc::SetPolicyRequest deny_execute;
    deny_execute.rules = baseline_view->rules;
    deny_execute.rules["process.execute"] = "deny";
    const ipc::Response applied = client.call(deny_execute, kCallBudget);
    MIRAGE_CHECK(applied.ok);
    const auto *applied_view = std::get_if<ipc::PolicyView>(&applied.payload);
    MIRAGE_CHECK(applied_view != nullptr);
    if (applied_view != nullptr) {
        MIRAGE_CHECK(applied_view->rules.at("process.execute") == "deny");
        MIRAGE_CHECK(applied_view->read_roots.size() == 1 &&
                     applied_view->read_roots[0] == "/home/user/work");
    }

    // The rule governs the next gated action immediately: a process.execute
    // step is refused before any desktop action (the driver reads the same
    // controller the policy face just replaced).
    ipc::SubmitTaskRequest gated;
    gated.goal = "run a command the freshly-set policy denies";
    gated.steps.push_back({ipc::StepKind::ProcessExecute, "printf after-deny"});
    const std::optional<std::string> task_id = submit_task(client, gated);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const auto done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done.has_value()) {
        MIRAGE_CHECK(done->progress == "Failed");
        MIRAGE_CHECK(!done->steps.empty());
        if (!done->steps.empty()) {
            const ipc::StepView &denied = done->steps[0];
            MIRAGE_CHECK(denied.status == "failed");
            MIRAGE_CHECK(denied.permission == "denied");
            MIRAGE_CHECK(denied.error.find("process.execute denied by policy") !=
                         std::string::npos);
        }
    }

    // Corrupt the persisted document behind the service's back, then set
    // again: the write is refused (DEC-011 fail-closed posture for
    // user-authoritative state), and the response says so...
    const std::filesystem::path settings_file =
        config.settings_directory / config.settings_file_name;
    MIRAGE_CHECK(std::filesystem::exists(settings_file));
    write_text_file(settings_file, "{oops");
    ipc::SetPolicyRequest deny_clipboard;
    deny_clipboard.rules = baseline_view->rules;
    deny_clipboard.rules["process.execute"] = "deny";
    deny_clipboard.rules["clipboard.write"] = "deny";
    const ipc::Response refused = client.call(deny_clipboard, kCallBudget);
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "internal");
    MIRAGE_CHECK(refused.error.message.find("policy persistence failed") != std::string::npos);

    // ...but the applied rules are live: policy.get reports the new
    // clipboard rule (the asymmetry is the documented posture — live effect
    // first, refused write reported honestly).
    const ipc::Response after = client.call(ipc::GetPolicyRequest{}, kCallBudget);
    MIRAGE_CHECK(after.ok);
    const auto *after_view = std::get_if<ipc::PolicyView>(&after.payload);
    MIRAGE_CHECK(after_view != nullptr);
    if (after_view != nullptr) {
        MIRAGE_CHECK(after_view->rules.at("clipboard.write") == "deny");
        MIRAGE_CHECK(after_view->rules.at("process.execute") == "deny");
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_session_close_frees_dialog_registry_slot() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    // The primary session occupies one of the two slots; every other slot
    // must be reusable across open→chat→close cycles.
    config.max_sessions = 2;
    config.model.enabled = true;
    config.model.dialect = "openai.responses.v1";
    config.model.model_selector = "test-model";
    const auto scripted = std::make_shared<mirage::integration::ModelProviderOverride>(
        [](const mira::ModelProfile &profile) -> std::shared_ptr<mira::IModelProvider> {
            return std::make_shared<ScriptedModelProvider>(profile);
        });
    config.model_provider_override = scripted;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    for (int cycle = 0; cycle < 2; ++cycle) {
        const ipc::Response opened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
        MIRAGE_CHECK(opened.ok);
        const auto *opened_session = std::get_if<ipc::SessionOpened>(&opened.payload);
        MIRAGE_CHECK(opened_session != nullptr);
        if (opened_session == nullptr) {
            service.request_shutdown();
            (void)service.run();
            return;
        }
        const std::string session_id = opened_session->session_id;
        const ipc::Response accepted =
            client.call(ipc::SessionChatRequest{session_id, "turn text"}, kCallBudget);
        MIRAGE_CHECK(accepted.ok);

        // The turn must settle ok before the close so the cycle ends clean.
        const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
        bool settled = false;
        while (!settled) {
            const ipc::Response snapshot =
                client.call(ipc::ChatHistoryRequest{session_id, {}}, kCallBudget);
            MIRAGE_CHECK(snapshot.ok);
            if (snapshot.ok) {
                const auto *dialog = std::get_if<ipc::DialogHistory>(&snapshot.payload);
                MIRAGE_CHECK(dialog != nullptr);
                if (dialog != nullptr && dialog->turns.size() == 1 &&
                    dialog->turns[0].status == "ok") {
                    settled = true;
                    break;
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
        }
        MIRAGE_CHECK(settled);

        const ipc::Response closed = client.call(ipc::CloseSessionRequest{session_id}, kCallBudget);
        MIRAGE_CHECK(closed.ok);
    }

    // The registry slots freed by the closes admit a fresh session whose
    // dialog face works — the leak regression turned this acceptance into
    // `unavailable: dialog registry capacity exhausted (2)`.
    const ipc::Response reopened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(reopened.ok);
    const auto *reopened_session = std::get_if<ipc::SessionOpened>(&reopened.payload);
    MIRAGE_CHECK(reopened_session != nullptr);
    if (reopened_session != nullptr) {
        const ipc::Response accepted = client.call(
            ipc::SessionChatRequest{reopened_session->session_id, "第三轮"}, kCallBudget);
        MIRAGE_CHECK(accepted.ok);
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_session_close_lifecycle() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    // The primary session occupies the only registry slot: after closing the
    // opened session the freed capacity admits a new one.
    config.max_sessions = 2;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    // The primary session is the only registry entry at start.
    const ipc::Response initial_list = client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(initial_list.ok);
    const auto *initial_sessions = std::get_if<ipc::SessionList>(&initial_list.payload);
    MIRAGE_CHECK(initial_sessions != nullptr);
    MIRAGE_CHECK(initial_sessions != nullptr && initial_sessions->sessions.size() == 1);
    const std::string primary_id =
        initial_sessions != nullptr && initial_sessions->sessions.size() == 1
            ? initial_sessions->sessions[0].id
            : std::string{};
    MIRAGE_CHECK(!primary_id.empty());

    // The primary session is product equipment: closing it is refused
    // without touching the pinned runtime or the registry.
    const ipc::Response close_primary =
        client.call(ipc::CloseSessionRequest{primary_id}, kCallBudget);
    MIRAGE_CHECK(!close_primary.ok);
    MIRAGE_CHECK(close_primary.error.code == "invalid_state");
    MIRAGE_CHECK(close_primary.error.message == "the primary session cannot be closed");

    // Unknown ids are the same stable not_found as the other session faces.
    const ipc::Response close_unknown =
        client.call(ipc::CloseSessionRequest{"no-such-session"}, kCallBudget);
    MIRAGE_CHECK(!close_unknown.ok);
    MIRAGE_CHECK(close_unknown.error.code == "not_found");
    MIRAGE_CHECK(close_unknown.error.message == "unknown session id");

    // session.open admits one more session; closing it removes the registry
    // entry and reports the pinned post-close state.
    const ipc::Response opened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(opened.ok);
    const auto *opened_session = std::get_if<ipc::SessionOpened>(&opened.payload);
    MIRAGE_CHECK(opened_session != nullptr);
    if (opened_session == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const std::string session_id = opened_session->session_id;
    const ipc::Response closed = client.call(ipc::CloseSessionRequest{session_id}, kCallBudget);
    MIRAGE_CHECK(closed.ok);
    const auto *closed_session = std::get_if<ipc::SessionClosed>(&closed.payload);
    MIRAGE_CHECK(closed_session != nullptr);
    if (closed_session != nullptr) {
        MIRAGE_CHECK(closed_session->session_id == session_id);
        MIRAGE_CHECK(closed_session->state == "closed");
    }

    // The registry entry is gone: session.list reports only the primary,
    // and a second close of the same id is not_found (nothing to close).
    const ipc::Response list = client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(list.ok);
    const auto *sessions = std::get_if<ipc::SessionList>(&list.payload);
    MIRAGE_CHECK(sessions != nullptr);
    MIRAGE_CHECK(sessions != nullptr && sessions->sessions.size() == 1);
    if (sessions != nullptr && sessions->sessions.size() == 1) {
        MIRAGE_CHECK(sessions->sessions[0].id == primary_id);
    }
    const ipc::Response close_again =
        client.call(ipc::CloseSessionRequest{session_id}, kCallBudget);
    MIRAGE_CHECK(!close_again.ok);
    MIRAGE_CHECK(close_again.error.code == "not_found");

    // The freed slot admits a fresh session (capacity was 2).
    const ipc::Response reopened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(reopened.ok);
    const auto *reopened_session = std::get_if<ipc::SessionOpened>(&reopened.payload);
    MIRAGE_CHECK(reopened_session != nullptr);
    MIRAGE_CHECK(reopened_session != nullptr && reopened_session->session_id != session_id);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// Closing a session cooperatively interrupts its own non-terminal tasks
/// (DEC-026 backlog item 2): the same two-step cancel task.cancel uses (cancel
/// token + executor task cancel) runs before the pinned close, so an
/// in-flight desktop action ends at its next observation point and the task
/// settles terminal promptly. The 60 s step budget makes timeout masking
/// impossible: only the cancellation path can end the sleep inside the
/// scenario's wait budget.
void scenario_session_close_cancels_inflight_task() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.step_timeout = std::chrono::milliseconds{60000};
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response opened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(opened.ok);
    const auto *opened_session = std::get_if<ipc::SessionOpened>(&opened.payload);
    MIRAGE_CHECK(opened_session != nullptr);
    if (opened_session == nullptr) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    const std::string session_id = opened_session->session_id;

    // One long sleep step bound to the opened session (explicit binding, so
    // the primary session keeps no task and stays closeable-by-refusal).
    ipc::SubmitTaskRequest submit;
    submit.goal = "long task cancelled by session.close";
    submit.session_id = session_id;
    submit.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 30"});
    const std::optional<std::string> task_id = submit_task(client, submit);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        service.request_shutdown();
        (void)service.run();
        return;
    }

    // Let the driver enter the sleep step before closing the session.
    const auto running =
        wait_for_progress(config.socket_path, *task_id, [](const ipc::InspectTask &view) {
            return !view.steps.empty() && view.steps[0].status == "running";
        });
    MIRAGE_CHECK(running.has_value());
    if (!running) {
        service.request_shutdown();
        (void)service.run();
        return;
    }

    const ipc::Response closed = client.call(ipc::CloseSessionRequest{session_id}, kCallBudget);
    MIRAGE_CHECK(closed.ok);
    const auto *closed_session = std::get_if<ipc::SessionClosed>(&closed.payload);
    MIRAGE_CHECK(closed_session != nullptr);
    if (closed_session != nullptr) {
        MIRAGE_CHECK(closed_session->session_id == session_id);
        MIRAGE_CHECK(closed_session->state == "closed");
    }

    // The task settles terminal promptly AND the driver's step bookkeeping
    // converges to the same picture (the pinned progress can flip a poll
    // slice before the driver finishes marking steps — same shape
    // task_cancel_test waits for). The 30 s wait budget stays far below the
    // 60 s step budget, so only the cancellation path can produce this.
    const auto done =
        wait_for_progress(config.socket_path, *task_id, [](const ipc::InspectTask &view) {
            return view.progress == "Cancelled" && !view.steps.empty() &&
                   view.steps[0].status == "cancelled";
        });
    MIRAGE_CHECK(done.has_value());
    if (done.has_value()) {
        MIRAGE_CHECK(done->progress == "Cancelled");
        MIRAGE_CHECK(!done->steps.empty());
        if (!done->steps.empty()) {
            MIRAGE_CHECK(done->steps[0].status == "cancelled");
        }
    }

    // The closed session's registry entry is gone; the primary stays.
    const ipc::Response list = client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(list.ok);
    const auto *sessions = std::get_if<ipc::SessionList>(&list.payload);
    MIRAGE_CHECK(sessions != nullptr);
    MIRAGE_CHECK(sessions != nullptr && sessions->sessions.size() == 1);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_session_open_capacity_fail_closed() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    // The primary session occupies the only slot (DEC-021: it counts
    // against the bound); session.open must refuse explicitly.
    config.max_sessions = 1;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response opened = client.call(ipc::OpenSessionRequest{}, kCallBudget);
    MIRAGE_CHECK(!opened.ok);
    MIRAGE_CHECK(opened.error.code == "unavailable");

    // The refusal left the registry untouched.
    const ipc::Response list = client.call(ipc::ListSessionsRequest{}, kCallBudget);
    MIRAGE_CHECK(list.ok);
    const auto *sessions = std::get_if<ipc::SessionList>(&list.payload);
    MIRAGE_CHECK(sessions != nullptr);
    MIRAGE_CHECK(sessions != nullptr && sessions->sessions.size() == 1);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

int main() {
    run_scenario("run_before_start_is_rejected", scenario_run_before_start_is_rejected);
    run_scenario("start_rejects_null_binding_fail_closed",
                 scenario_start_rejects_null_binding_fail_closed);
    run_scenario("hello_identity_and_terminal_restart",
                 scenario_hello_identity_and_terminal_restart);
    run_scenario("default_socket_path_resolution", scenario_default_socket_path_resolution);
    run_scenario("task_completes_steps_with_operation_ids",
                 scenario_task_completes_steps_with_operation_ids);
    run_scenario("task_fails_fast_and_skips_remainder",
                 scenario_task_fails_fast_and_skips_remainder);
    run_scenario("step_result_truncated_to_cap", scenario_step_result_truncated_to_cap);
    run_scenario("submit_and_inspect_validation", scenario_submit_and_inspect_validation);
    run_scenario("registry_capacity_rejects_third_task",
                 scenario_registry_capacity_rejects_third_task);
    run_scenario("sequential_requests_on_one_connection",
                 scenario_sequential_requests_on_one_connection);
    run_scenario("interleaved_connections_keep_correlation",
                 scenario_interleaved_connections_keep_correlation);
    run_scenario("garbage_payload_yields_protocol_error_then_close",
                 scenario_garbage_payload_yields_protocol_error_then_close);
    run_scenario("pipelined_request_rejected", scenario_pipelined_request_rejected);
    run_scenario("oversized_frame_closes_connection", scenario_oversized_frame_closes_connection);
    run_scenario("shutdown_via_ipc_after_completed_task",
                 scenario_shutdown_via_ipc_after_completed_task);
    run_scenario("shutdown_with_inflight_task_is_bounded",
                 scenario_shutdown_with_inflight_task_is_bounded);
    run_scenario("shutdown_fd_triggers_stop", scenario_shutdown_fd_triggers_stop);
    run_scenario("session_list_open_and_history_flow", scenario_session_list_open_and_history_flow);
    run_scenario("session_open_capacity_fail_closed", scenario_session_open_capacity_fail_closed);
    run_scenario("session_close_lifecycle", scenario_session_close_lifecycle);
    run_scenario("session_chat_dialog_face", scenario_session_chat_dialog_face);
    run_scenario("session_close_frees_dialog_registry_slot",
                 scenario_session_close_frees_dialog_registry_slot);
    run_scenario("policy_face_get_set_persists", scenario_policy_face_get_set_persists);
    run_scenario("policy_set_live_effect_and_fail_closed_persist",
                 scenario_policy_set_live_effect_and_fail_closed_persist);
    run_scenario("session_state_corrupt_document_degrades_loudly",
                 scenario_session_state_corrupt_document_degrades_loudly);
    run_scenario("session_state_round_trip_across_restart",
                 scenario_session_state_round_trip_across_restart);
    run_scenario("session_chat_latch_and_failure", scenario_session_chat_latch_and_failure);
    run_scenario("session_close_cancels_inflight_task",
                 scenario_session_close_cancels_inflight_task);
    run_scenario("observe_fails_closed_on_headless_topology",
                 scenario_observe_fails_closed_on_headless_topology);
    run_scenario("workflow_get_unknown_id_is_not_found",
                 scenario_workflow_get_unknown_id_is_not_found);
    run_scenario("workflow_get_serves_head_definition_content",
                 scenario_workflow_get_serves_head_definition_content);
    run_scenario("observe_wire_budget_truncates_with_explicit_mark",
                 scenario_observe_wire_budget_truncates_with_explicit_mark);
    return mirage::testing::finish("runtime_service_test");
}
