#include "../support/ipc_io.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <fcntl.h>

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

namespace ipc = mirage::runtime::ipc;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::runtime::ServiceRunReport;
using mirage::testing::FrameRead;

constexpr auto kCallBudget = std::chrono::seconds{5};
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
    return mirage::testing::finish("runtime_service_test");
}
