#include <mirage/runtime/runtime_service.hpp>

#include <mirage/desktop/observation_assembler.hpp>
#include <mirage/integration/session_journal.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mirage/runtime/persistence/paths.hpp>
#include <mirage/runtime/persistence/recovery.hpp>
#include <mirage/runtime/persistence/session_state.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/persistence/store.hpp>

// DEC-027 dialog task glue: the pinned operation-context and id types the
// model layer's completion call consumes (runtime/service already links the
// pinned core transitively through the integration adapter).
#include <mira/environment.hpp>
#include <mira/model_contracts.hpp>

#include "service_core.hpp"
#include "service_loop.hpp"
#include "task_driver.hpp"

#include <executor/blocking_io.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mirage::runtime {
namespace {

namespace persistence = mirage::runtime::persistence;
using detail::progress_name;
using detail::WorkflowCatalogEntry;
using detail::WorkflowRunRecord;

constexpr const char *kServiceName = "mirage-runtime";
constexpr std::size_t kMaxGoalBytes = 8 * 1024;
constexpr std::size_t kMaxArgumentBytes = 4 * 1024;
/// Default entry budget for one session.history response (DEC-021); the
/// service clamps the requested limit to ServiceConfig::max_history_entries.
constexpr std::size_t kDefaultHistoryLimit = 50;
/// Byte budget for one workflow.save / workflow.publish definition (DEC-023);
/// the same ceiling the pinned WorkflowLimits enforces on decode.
constexpr std::size_t kMaxWorkflowDefinitionBytes = 256 * 1024;
/// Byte budget for one session.chat user text (DEC-027); larger inputs are
/// refused instead of silently cropped.
constexpr std::size_t kMaxDialogTextBytes = 16 * 1024;
/// Read-roots bound carried by the policy face (mirrors the persistence
/// module's settings bounds).
constexpr std::size_t kMaxPolicyReadRoots = 64;
/// Rendered-transcript bound of one dialog turn: the newest settled turns
/// that fit the model layer's input budget are rendered oldest-first.
constexpr std::size_t kDialogTranscriptTurns = 20;

bool terminal_progress(TaskProgress progress) {
    return progress == TaskProgress::Completed || progress == TaskProgress::Failed ||
           progress == TaskProgress::Cancelled;
}

std::int64_t wall_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// 32-hex dialog turn identity (the pinned id wire form); a correlation id,
/// not a secret — std::random_device suffices.
std::string make_turn_id() {
    std::random_device source;
    std::string out;
    out.reserve(32);
    while (out.size() < 32) {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%08x", source());
        out += buffer;
    }
    out.resize(32);
    return out;
}

/// Renders the newest settled turns as the dialog transcript block
/// (oldest-first, "user:" / "assistant:" lines, DEC-027). Bounded by the
/// turn count; the model layer enforces the byte budget on top.
std::string render_dialog_transcript(const std::shared_ptr<detail::ServiceCore> &core,
                                     const std::string &session_id) {
    std::lock_guard lock(core->dialogs.mutex);
    const auto found = core->dialogs.sessions.find(session_id);
    if (found == core->dialogs.sessions.end()) {
        return {};
    }
    auto &log = found->second;
    std::size_t begin =
        log.turns.size() > kDialogTranscriptTurns ? log.turns.size() - kDialogTranscriptTurns : 0;
    std::string transcript;
    for (std::size_t index = begin; index < log.turns.size(); ++index) {
        const detail::DialogTurnRecord &record = log.turns[index];
        if (record.status == "pending") {
            continue;
        }
        if (!transcript.empty()) {
            transcript += "\n";
        }
        transcript += "user: " + record.user_text;
        transcript += record.status == "ok" ? "\nassistant: " + record.reply_text
                                            : "\nassistant: (previous turn failed)";
    }
    return transcript;
}

/// Settles one dialog turn (DEC-027): records the outcome, clears the
/// in-flight latch and publishes the notification. Worker-thread entry (the
/// dialog task); the Topic publish is thread-safe and per-connection queues
/// stay bounded — session.chat.history is the resync face. A session that
/// closed (or a registry that vanished) swallows the turn.
void settle_dialog_turn(const std::shared_ptr<detail::ServiceCore> &core,
                        const std::string &session_id, const std::string &turn_id,
                        const mirage::integration::DialogCompletion &completion) {
    std::string status;
    ipc::ChatTurnUpdatedEvent event;
    event.session_id = session_id;
    event.turn_id = turn_id;
    if (completion.ok) {
        status = "ok";
        event.reply_text = completion.reply_text;
        event.has_reply = true;
    } else {
        status = "failed";
        event.error =
            completion.cancelled ? std::string{"dialog turn was cancelled"} : completion.error;
        event.has_error = true;
    }
    {
        std::lock_guard lock(core->dialogs.mutex);
        const auto found = core->dialogs.sessions.find(session_id);
        if (found == core->dialogs.sessions.end()) {
            return; // the session closed: the thread is gone, nothing to settle
        }
        auto &log = found->second;
        bool settled = false;
        for (auto &record : log.turns) {
            if (record.turn_id == turn_id) {
                record.status = status;
                record.reply_text = event.reply_text;
                record.error = event.error;
                event.status = status;
                event.user_text = record.user_text;
                event.sequence = record.sequence;
                settled = true;
                break;
            }
        }
        if (log.in_flight_turn_id == turn_id) {
            log.in_flight_turn_id.clear();
        }
        if (!settled) {
            return;
        }
    }
    core->events.publish_chat_turn(std::move(event));
    persist_session_state(core);
}

/// Body of one dialog turn task (DEC-027): the bounded model completion and
/// its settlement. Never throws.
void run_dialog_turn(const std::shared_ptr<detail::ServiceCore> &core, std::string session_id,
                     std::string turn_id, const std::string &transcript, const std::string &text,
                     executor::StopToken stop) {
    mira::OperationContext context;
    context.session = mira::SessionId::parse(session_id).value_or(mira::SessionId{});
    context.operation = mira::OperationId::generate();
    context.started_at = mira::Timestamp::now();
    context.deadline = std::chrono::steady_clock::now() + core->model.request_deadline;
    context.cancellation_requested = [stop]() { return stop.stop_requested(); };
    mirage::integration::DialogCompletion completion =
        core->model_layer ? core->model_layer->complete_dialog_turn(transcript, text, context)
                          : mirage::integration::DialogCompletion{};
    {
        std::lock_guard lock(core->drivers_mutex);
        core->drivers.erase("dialog-" + turn_id);
    }
    settle_dialog_turn(core, session_id, turn_id, completion);
}

} // namespace

namespace detail {

/// Worker-thread-safe session state persist (M5-08, DEC-021 backlog ①):
/// snapshots the session registry, dialog threads and the raw journal appends
/// into the state document. File IO on the calling thread matches the
/// recovery persist precedent; failures are recorded on stderr once per
/// change of reason and never propagate.
void persist_session_state(const std::shared_ptr<ServiceCore> &core) {
    if (core->session_state_store == nullptr) {
        return;
    }
    // Serializes concurrent persist entry (serial context, task drivers,
    // dialog settle): the store's temp file name carries only the pid, so
    // unsynchronized saves would collide on O_EXCL and skip a snapshot.
    std::lock_guard state_lock(core->session_state_mutex);
    static std::mutex report_mutex;
    static std::string last_error;
    persistence::SessionState state;
    {
        // Canonical nesting order for these three mutexes (handle_session_close
        // nests dialogs.mutex -> journal_raw_mutex, the same relative order):
        // sessions.mutex -> dialogs.mutex -> journal_raw_mutex. Any other
        // order is a lock-order inversion (TSan reported exactly that cycle
        // against the previous journal_raw-first nesting here).
        std::lock_guard sessions_lock(core->sessions.mutex);
        std::lock_guard dialogs_lock(core->dialogs.mutex);
        std::lock_guard raw_lock(core->journal_raw_mutex);
        for (const auto &[session_id, created] : core->sessions.created_at_ms) {
            persistence::PersistedSession session;
            session.id = session_id;
            session.created_at_ms = created;
            const auto raw = core->journal_raw.find(session_id);
            if (raw != core->journal_raw.end()) {
                session.journal = raw->second;
            }
            const auto dialogs = core->dialogs.sessions.find(session_id);
            if (dialogs != core->dialogs.sessions.end()) {
                for (const auto &record : dialogs->second.turns) {
                    if (record.status == "pending") {
                        continue; // in-flight turns are not persisted
                    }
                    persistence::PersistedChatTurn turn;
                    turn.turn_id = record.turn_id;
                    turn.status = record.status;
                    turn.user_text = record.user_text;
                    turn.reply_text = record.reply_text;
                    turn.error = record.error;
                    turn.sequence = record.sequence;
                    turn.recorded_at_ms = record.recorded_at_ms;
                    session.chat_turns.push_back(std::move(turn));
                }
            }
            state.sessions.push_back(std::move(session));
        }
    }
    const auto saved = core->session_state_store->save(persistence::encode_session_state(state));
    if (!saved.ok) {
        std::lock_guard report_lock(report_mutex);
        if (last_error != saved.error) {
            last_error = saved.error;
            std::cerr << "mirage-service: session state persist failed: " << saved.error << '\n';
        }
    }
}

} // namespace detail

ServiceInfo runtime_service_info() {
    // The service owns the process's Executor and the hosted pinned runtime
    // and outlives any GUI (design doc section 12, EXEC-01, DEC-007).
    return {kServiceName, true};
}

struct RuntimeService::Impl {
    enum class Lifecycle { New, Running, Terminal };

    ServiceConfig config;
    std::string socket_path;
    /// Fail-closed hook used when the config carries neither the DEC-020
    /// async hub nor a legacy confirmation handler (DEC-010): headless
    /// services reject every Confirm rule.
    permission::DenyAllConfirmation fail_closed_confirmation;
    /// Non-owning observer of the configured async confirmation surface
    /// (DEC-020); null unless config.confirmation_hub is set. The config
    /// shared_ptr owns the hub.
    permission::AsyncConfirmationHub *confirmation_hub = nullptr;
    std::shared_ptr<detail::ServiceCore> core = std::make_shared<detail::ServiceCore>();
    /// Non-owning observer of the loop object; the executor's blocking
    /// worker owns the loop itself. Valid from a successful start_worker()
    /// until the lifecycle leaves Running (the run()/destructor path joins
    /// the loop before touching anything else).
    /// Atomic: request_shutdown() is legal from any thread, including while
    /// the owning thread's teardown has already detached the loop.
    std::atomic<detail::ServiceLoop *> loop{nullptr};
    /// Serializes request_shutdown's stop_serving() dereference against
    /// the teardown that detaches and destroys the loop.
    std::mutex loop_mutex;
    ipc::IpcListener listener;
    executor::WorkerHandle loop_worker;
    std::promise<void> loop_done;
    std::future<void> loop_done_future;
    std::atomic<Lifecycle> lifecycle{Lifecycle::New};
    int shutdown_fd = -1;
    ServiceRunReport run_report;

    explicit Impl(ServiceConfig service_config)
        : config(std::move(service_config)),
          socket_path(config.socket_path.empty() ? ipc::default_socket_path()
                                                 : config.socket_path) {
        core->mirage_version = config.mirage_version;
        core->max_steps_per_task = config.max_steps_per_task;
        core->max_task_records = config.max_task_records;
        core->max_sessions = config.max_sessions;
        core->max_history_entries = config.max_history_entries;
        core->max_result_bytes = config.max_result_bytes;
        core->step_timeout = config.step_timeout;
        core->command_wait = config.command_wait;
        core->visual_registry = config.visual_registry;
        {
            std::lock_guard lock(core->registry.mutex);
            core->registry.capacity = config.max_task_records;
        }
        {
            std::lock_guard lock(core->sessions.mutex);
            core->sessions.capacity = config.max_sessions;
        }
        // The conversation journal (DEC-021): bounded pinned in-memory event
        // store behind the pinned-free SessionJournal adapter; constructed
        // with the service, so the session.* faces are always served.
        core->journal = std::make_shared<mirage::integration::SessionJournal>();
        {
            std::lock_guard lock(core->workflows.mutex);
            core->workflows.capacity = config.max_workflow_definitions;
        }
        {
            std::lock_guard lock(core->workflow_runs.mutex);
            core->workflow_runs.capacity = config.max_workflow_runs;
        }
        // The workflow event bridge (DEC-023): constructed with the service,
        // so the workflow.* faces are always served. Its sink publishes
        // directly into the hub — appends arrive on pinned drive threads and
        // may fire from within a serial handler (the publish gate drive), so
        // a serial re-submission would re-enter the serial domain; the Topic
        // is thread-safe and the per-connection queues stay bounded
        // (workflow.runs is the snapshot truth).
        core->workflow_bridge = std::make_shared<mirage::integration::WorkflowEventBridge>();
        core->workflow_bridge->set_sink([weak = std::weak_ptr<detail::ServiceCore>(core)](
                                            const mirage::integration::WorkflowRunEventView &view) {
            const auto live_core = weak.lock();
            if (!live_core) {
                return;
            }
            ipc::WorkflowRunUpdatedEvent event;
            event.run_id = view.run_id;
            event.workflow_id = view.workflow_id;
            event.state = view.state;
            event.run_epoch = view.run_epoch;
            if (!view.summary.empty()) {
                event.summary = view.summary;
            }
            live_core->events.publish_workflow_run(std::move(event));
        });
        // One controller for the whole service (DEC-010): the configured
        // policy plus the configured confirmation surface — the DEC-020
        // async hub, the legacy sync hook, or the fail-closed default.
        permission::ConfirmationHandler *handler = &fail_closed_confirmation;
        if (config.confirmation_hub != nullptr) {
            confirmation_hub = config.confirmation_hub.get();
            handler = confirmation_hub;
        } else if (config.confirmation) {
            handler = config.confirmation.get();
        }
        core->permission =
            std::make_shared<permission::PermissionController>(config.permission_policy, *handler);
        if (confirmation_hub != nullptr) {
            // The hub raises requests from the driver thread; the publish
            // rides the same serial-domain best-effort path as the task
            // events (DEC-020 decision 6). By-value core capture: the hook
            // may fire while teardown is draining drivers.
            confirmation_hub->set_publish_hook(
                [core = core](const permission::PendingConfirmation &pending) {
                    ipc::PermissionRequestedEvent event;
                    event.request_id = pending.request_id;
                    event.capability = pending.capability;
                    event.resource = pending.resource;
                    event.task_id = pending.task_id;
                    event.timeout_ms = pending.timeout_ms;
                    detail::publish_permission_request_best_effort(core, std::move(event));
                });
        }
        loop_done_future = loop_done.get_future();
    }

    ipc::ServiceIdentity identity() const {
        ipc::ServiceIdentity result;
        result.name = kServiceName;
        result.mirage_version = config.mirage_version.empty() ? "unknown" : config.mirage_version;
        result.mira_core_version = mira_core_version_string();
        result.host_status = host_status_name(core->host.status());
        result.protocol = ipc::kProtocolVersion;
        // DEC-012: an event-capable service always advertises the member.
        result.events = true;
        // DEC-020: the async confirmation face is advertised by presence and
        // value — a hub-equipped service is true, a headless one false.
        result.permissions = confirmation_hub != nullptr;
        // DEC-021: the session faces (list / open / history) are always
        // served; the journal is constructed with the service.
        result.sessions = true;
        // DEC-023: the workflow faces are always served; the bridge is
        // constructed with the service and the surface attaches at start().
        result.workflows = true;
        // DEC-026: the observation face is always served; a request before
        // an environment is bound fails closed with `unavailable`.
        result.observation = true;
        // DEC-027: the dialog face rides the configured model layer
        // (equipment-dependent, like the `permissions` bit).
        result.chat = core->model_layer != nullptr && core->model_layer->running();
        // M5-07: the policy face is core equipment — always served.
        result.policy = true;
        return result;
    }

    // --- response helpers (loop thread via posted payloads) ---------------

    void respond(std::uint64_t connection_id, std::uint64_t correlation_id,
                 ipc::ResponsePayload payload) {
        ipc::Response response;
        response.ok = true;
        response.id = correlation_id;
        response.payload = std::move(payload);
        detail::ServiceLoop *active = loop.load(std::memory_order_acquire);
        if (active != nullptr) {
            active->post_response(connection_id, ipc::encode_response(response));
        }
    }

    void fail(std::uint64_t connection_id, std::uint64_t correlation_id, std::string code,
              std::string message, bool close_after = false) {
        ipc::Response response;
        response.ok = false;
        response.id = correlation_id;
        response.error = {std::move(code), std::move(message)};
        detail::ServiceLoop *active = loop.load(std::memory_order_acquire);
        if (active == nullptr) {
            return;
        }
        const std::string payload = ipc::encode_response(response);
        if (close_after) {
            active->post_response_and_close(connection_id, payload);
        } else {
            active->post_response(connection_id, payload);
        }
    }

    /// Host status transitions publish into the hub (DEC-012 decision 5):
    /// the LatestMailbox keeps the newest status for subscribe-time seeds
    /// and the change enters the Topic publish path. Bounded and
    /// thread-safe; also legal after the executor drained (teardown), where
    /// subscribers no longer exist but the mailbox stays truthful.
    void publish_host_status(HostStatus status) {
        ipc::HostStatusEvent event;
        event.status = host_status_name(status);
        core->events.publish_host_status(std::move(event));
    }

    // --- request handling --------------------------------------------------

    /// Loop thread: pushes the frame through the service's serial context so
    /// every request (and every host operation it performs) runs serialized
    /// on one thread, and waits for the bounded handler to finish.
    void handle_frame(std::uint64_t connection_id, const std::string &payload) {
        auto handled = core->executor.submit_on(core->serial, [this, connection_id, payload] {
            process_request(connection_id, payload);
        });
        try {
            handled.get();
        } catch (const std::exception &) {
            fail(connection_id, 0, "internal", "request was not admitted by the service runtime",
                 true);
        }
    }

    /// Serial thread: decode and dispatch one request.
    void process_request(std::uint64_t connection_id, const std::string &payload) {
        ipc::RequestDecode decoded = ipc::decode_request(payload);
        if (!decoded.ok) {
            fail(connection_id, 0, "protocol_error", decoded.error, true);
            return;
        }
        const std::uint64_t correlation_id = decoded.id;
        if (auto *request = std::get_if<ipc::HelloRequest>(&decoded.body)) {
            (void)request;
            respond(connection_id, correlation_id, identity());
            return;
        }
        if (auto *request = std::get_if<ipc::SubmitTaskRequest>(&decoded.body)) {
            handle_submit(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::ListTasksRequest>(&decoded.body)) {
            (void)request;
            handle_list(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::InspectTaskRequest>(&decoded.body)) {
            handle_inspect(connection_id, correlation_id, std::move(request->task_id));
            return;
        }
        if (auto *request = std::get_if<ipc::CancelTaskRequest>(&decoded.body)) {
            handle_cancel(connection_id, correlation_id, std::move(request->task_id));
            return;
        }
        if (auto *request = std::get_if<ipc::ShutdownRequest>(&decoded.body)) {
            (void)request;
            respond(connection_id, correlation_id, ipc::ShutdownAccepted{});
            // The response above is queued; the loop flushes it on its
            // ordered exit (DEC-007 service.shutdown semantics).
            if (detail::ServiceLoop *active = loop.load(std::memory_order_acquire)) {
                active->stop_serving();
            }
            return;
        }
        if (auto *request = std::get_if<ipc::SubscribeEventsRequest>(&decoded.body)) {
            (void)request;
            handle_subscribe(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::UnsubscribeEventsRequest>(&decoded.body)) {
            (void)request;
            handle_unsubscribe(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::RespondPermissionRequest>(&decoded.body)) {
            handle_permission_respond(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::ListPermissionsRequest>(&decoded.body)) {
            (void)request;
            handle_permission_list(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::ListSessionsRequest>(&decoded.body)) {
            (void)request;
            handle_session_list(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::OpenSessionRequest>(&decoded.body)) {
            (void)request;
            handle_session_open(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::CloseSessionRequest>(&decoded.body)) {
            handle_session_close(connection_id, correlation_id, std::move(request->session_id));
            return;
        }
        if (auto *request = std::get_if<ipc::SessionChatRequest>(&decoded.body)) {
            handle_session_chat(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::ChatHistoryRequest>(&decoded.body)) {
            handle_session_chat_history(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::GetPolicyRequest>(&decoded.body)) {
            (void)request;
            handle_policy_get(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::SetPolicyRequest>(&decoded.body)) {
            handle_policy_set(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::SessionHistoryRequest>(&decoded.body)) {
            handle_session_history(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowListRequest>(&decoded.body)) {
            (void)request;
            handle_workflow_list(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowSaveRequest>(&decoded.body)) {
            handle_workflow_save(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowPublishRequest>(&decoded.body)) {
            handle_workflow_publish(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowDeleteRequest>(&decoded.body)) {
            handle_workflow_delete(connection_id, correlation_id, std::move(request->workflow_id));
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowAtomCatalogRequest>(&decoded.body)) {
            (void)request;
            handle_workflow_atom_catalog(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowRunsRequest>(&decoded.body)) {
            (void)request;
            handle_workflow_runs(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowRunRequest>(&decoded.body)) {
            handle_workflow_run(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowCancelRunRequest>(&decoded.body)) {
            handle_workflow_cancel(connection_id, correlation_id, std::move(request->run_id));
            return;
        }
        if (auto *request = std::get_if<ipc::WorkflowGetRequest>(&decoded.body)) {
            handle_workflow_get(connection_id, correlation_id, std::move(request->workflow_id));
            return;
        }
        if (auto *request = std::get_if<ipc::DesktopObserveRequest>(&decoded.body)) {
            handle_desktop_observe(connection_id, correlation_id, *request);
            return;
        }
        fail(connection_id, correlation_id, "unsupported", "unknown request");
    }

    std::chrono::milliseconds
    effective_step_timeout(const std::optional<std::chrono::milliseconds> &requested) const {
        const std::chrono::milliseconds raw = requested.value_or(config.step_timeout);
        return std::clamp<std::chrono::milliseconds>(raw, std::chrono::milliseconds{1},
                                                     config.step_timeout);
    }

    void handle_submit(std::uint64_t connection_id, std::uint64_t correlation_id,
                       ipc::SubmitTaskRequest request) {
        if (request.goal.empty()) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "task.submit requires a non-empty 'goal'");
            return;
        }
        if (request.goal.size() > kMaxGoalBytes) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "goal exceeds " + std::to_string(kMaxGoalBytes) + " bytes");
            return;
        }
        if (request.steps.size() > core->max_steps_per_task) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "task declares more than " + std::to_string(core->max_steps_per_task) + " steps");
            return;
        }
        for (const auto &step : request.steps) {
            if (step.argument.size() > kMaxArgumentBytes) {
                fail(connection_id, correlation_id, "invalid_argument",
                     "step argument exceeds " + std::to_string(kMaxArgumentBytes) + " bytes");
                return;
            }
        }
        const std::chrono::milliseconds step_timeout = effective_step_timeout(request.step_timeout);

        // Session resolution (DEC-021): the explicit binding must be a known
        // session; absence means the primary session (the M1 wire form).
        std::string session_id;
        {
            std::lock_guard lock(core->sessions.mutex);
            if (request.session_id) {
                if (!core->sessions.contains(*request.session_id)) {
                    fail(connection_id, correlation_id, "not_found", "unknown session id");
                    return;
                }
                session_id = *request.session_id;
            } else {
                session_id = core->host.primary_session().id;
            }
        }

        // On the serial thread: direct host call is the owner discipline.
        const TaskSubmissionResult submission =
            core->host.submit_task(SessionIdentity{session_id}, request.goal);
        if (!submission.ok) {
            fail(connection_id, correlation_id, submission.error.code, submission.error.message);
            return;
        }
        {
            std::lock_guard lock(core->registry.mutex);
            if (core->registry.full()) {
                // Explicit bounded-registry rejection; the just-admitted
                // pinned task is rolled back instead of leaking (RULE-07).
                core->host.cancel_task(submission.task);
                fail(connection_id, correlation_id, "invalid_state",
                     "task registry at capacity (" + std::to_string(core->registry.capacity) + ")");
                return;
            }
            detail::TaskRecord record;
            record.id = submission.task.id;
            record.goal = request.goal;
            record.session_id = session_id;
            record.step_timeout = step_timeout;
            record.steps.reserve(request.steps.size());
            for (const auto &step : request.steps) {
                detail::StepRecord entry;
                entry.spec = step;
                record.steps.push_back(std::move(entry));
            }
            core->registry.tasks.emplace(record.id, std::move(record));
        }
        auto driver_submission = core->executor.submit_cancellable(
            [core = core, id = submission.task.id](executor::StopToken stop) {
                detail::run_driver(stop, core, id);
            });
        {
            std::lock_guard lock(core->drivers_mutex);
            core->drivers.emplace(submission.task.id, std::move(driver_submission));
        }
        // The task goal enters the session conversation (DEC-021): the
        // journal record is the fact, the message event the notification.
        // Serial context, so the direct publish keeps event order aligned
        // with the serialized state change.
        if (core->journal != nullptr) {
            const auto appended =
                core->journal->append_user_message(session_id, submission.task.id, request.goal);
            if (appended.ok) {
                {
                    std::lock_guard raw_lock(core->journal_raw_mutex);
                    core->journal_raw[session_id].push_back(persistence::PersistedJournalEntry{
                        "user", submission.task.id, request.goal, {}, 0});
                }
                ipc::SessionMessageEvent message;
                message.session_id = session_id;
                message.task_id = submission.task.id;
                message.kind = "user";
                message.text = request.goal;
                message.sequence = appended.sequence;
                core->events.publish_session_message(std::move(message));
                detail::persist_session_state(core);
            }
        }
        // Creation event (DEC-012 decision 3); published before the
        // acknowledgement so a subscriber never observes the ack for a task
        // whose created event is still queued behind serial work.
        detail::publish_task_updated(core, submission.task.id);
        ipc::TaskSubmitted submitted;
        submitted.task_id = submission.task.id;
        submitted.session_id = session_id;
        respond(connection_id, correlation_id, std::move(submitted));
    }

    void handle_list(std::uint64_t connection_id, std::uint64_t correlation_id) {
        // Copy the fields the summary needs while holding the registry lock:
        // the blocking task_view call below must run outside the lock, and a
        // bare TaskRecord pointer would race mark_driver_done's
        // final_progress write once the lock is dropped (TSAN, CI run
        // 35504750823).
        struct ListedTask {
            std::string id;
            std::string goal;
            std::string final_progress;
        };
        std::vector<ListedTask> tasks;
        {
            std::lock_guard lock(core->registry.mutex);
            tasks.reserve(core->registry.tasks.size());
            for (const auto &entry : core->registry.tasks) {
                tasks.push_back({entry.first, entry.second.goal, entry.second.final_progress});
            }
        }
        ipc::TaskList list;
        list.tasks.reserve(tasks.size());
        for (auto &task : tasks) {
            ipc::TaskSummary summary;
            if (!task.final_progress.empty()) {
                // Settled (this run or hydrated from the M1-07 recovery
                // file): the recorded terminal name is the truth.
                summary.progress = task.final_progress;
            } else {
                const TaskViewResult view = core->host.task_view(TaskIdentity{task.id});
                summary.progress = view.ok ? progress_name(view.view.progress) : "Unknown";
            }
            summary.id = std::move(task.id);
            summary.goal = std::move(task.goal);
            list.tasks.push_back(std::move(summary));
        }
        respond(connection_id, correlation_id, std::move(list));
    }

    void handle_inspect(std::uint64_t connection_id, std::uint64_t correlation_id,
                        std::string task_id) {
        std::optional<detail::TaskRecord> snapshot;
        {
            std::lock_guard lock(core->registry.mutex);
            if (auto entry = core->registry.tasks.find(task_id);
                entry != core->registry.tasks.end()) {
                snapshot = entry->second;
            }
        }
        if (!snapshot) {
            fail(connection_id, correlation_id, "not_found", "unknown task id");
            return;
        }
        ipc::InspectTask inspect;
        inspect.id = snapshot->id;
        inspect.goal = snapshot->goal;
        if (!snapshot->final_progress.empty()) {
            // Settled (this run or hydrated from the M1-07 recovery file):
            // the recorded terminal name and outcome are the truth.
            inspect.progress = snapshot->final_progress;
            inspect.has_success = snapshot->has_success;
            inspect.success = snapshot->success;
        } else {
            const TaskViewResult view = core->host.task_view(TaskIdentity{snapshot->id});
            inspect.progress = view.ok ? progress_name(view.view.progress) : "Unknown";
            if (view.ok && terminal_progress(view.view.progress)) {
                inspect.has_success = true;
                inspect.success = view.view.success;
            }
        }
        inspect.steps.reserve(snapshot->steps.size());
        for (std::size_t index = 0; index < snapshot->steps.size(); ++index) {
            const detail::StepRecord &record = snapshot->steps[index];
            ipc::StepView step;
            step.index = static_cast<int>(index);
            step.kind = ipc::step_kind_name(record.spec.kind);
            step.status = record.status;
            step.operation_id = record.operation_id;
            step.permission = record.permission;
            step.ok = record.ok;
            step.exit_code = record.exit_code;
            step.result = record.result;
            step.result_truncated = record.result_truncated;
            step.error = record.error;
            inspect.steps.push_back(std::move(step));
        }
        respond(connection_id, correlation_id, std::move(inspect));
    }

    /// Serial thread: cooperative task cancellation (M1-05 cancellation
    /// path). The order matters: the desktop cancel token goes first so an
    /// in-flight provider action ends at its next observation point, then
    /// the driver's executor stop token stops between-step progress, and
    /// the pinned cancel settles the task — the pinned state stays
    /// authoritative and a terminal task is never revived.
    void handle_cancel(std::uint64_t connection_id, std::uint64_t correlation_id,
                       std::string task_id) {
        bool known = false;
        bool from_recovery = false;
        {
            std::lock_guard lock(core->registry.mutex);
            auto entry = core->registry.tasks.find(task_id);
            known = entry != core->registry.tasks.end();
            if (known) {
                from_recovery = entry->second.from_recovery;
                if (!from_recovery) {
                    entry->second.cancel.request_cancel();
                }
            }
        }
        if (!known) {
            fail(connection_id, correlation_id, "not_found", "unknown task id");
            return;
        }
        if (from_recovery) {
            // The task settled in an earlier service era (M1-07); this
            // service's pinned instance has no counterpart to cancel, and
            // a terminal task is never revived.
            fail(connection_id, correlation_id, "invalid_state",
                 "task belongs to a previous service run and is already "
                 "settled");
            return;
        }
        {
            std::lock_guard lock(core->drivers_mutex);
            if (auto driver = core->drivers.find(task_id);
                driver != core->drivers.end() && driver->second.handle.valid()) {
                core->executor.request_task_cancel(driver->second.handle);
            }
        }
        const HostOutcome cancelled = core->host.cancel_task(TaskIdentity{task_id});
        if (!cancelled.ok) {
            fail(connection_id, correlation_id, cancelled.error.code, cancelled.error.message);
            return;
        }
        ipc::TaskCancelled acknowledgement;
        acknowledgement.task_id = task_id;
        const TaskViewResult view = core->host.task_view(TaskIdentity{task_id});
        acknowledgement.progress = view.ok ? progress_name(view.view.progress) : "Unknown";
        // Cancellation admission is a progress advance (DEC-012 decision 3):
        // subscribers see "Cancelling" (or the terminal state the pinned
        // cancel already settled under) before the ack leaves.
        detail::publish_task_updated(core, task_id);
        respond(connection_id, correlation_id, std::move(acknowledgement));
    }

    /// Serial thread: attach the connection to the service event stream
    /// (DEC-012). The bounded per-connection queue lives in the returned
    /// subscription; the loop drains it after the responses of each pass.
    /// The current host status seeds the stream (seq 1) so a fresh
    /// subscriber starts from the live state, mirroring the frontend mock.
    void handle_subscribe(std::uint64_t connection_id, std::uint64_t correlation_id) {
        detail::ServiceLoop *active = loop.load(std::memory_order_acquire);
        if (active == nullptr) {
            fail(connection_id, correlation_id, "internal", "service loop is not running");
            return;
        }
        auto subscription = core->events.subscribe(config.event_queue_capacity);
        std::optional<ipc::EventPayload> seed;
        if (auto status = core->events.current_host_status()) {
            seed = ipc::EventPayload{std::move(*status)};
        }
        respond(connection_id, correlation_id, ipc::ShutdownAccepted{});
        active->post_attach_events(connection_id, std::move(subscription), std::move(seed));
    }

    /// Serial thread: drop the connection's subscription; idempotent.
    void handle_unsubscribe(std::uint64_t connection_id, std::uint64_t correlation_id) {
        detail::ServiceLoop *active = loop.load(std::memory_order_acquire);
        if (active == nullptr) {
            fail(connection_id, correlation_id, "internal", "service loop is not running");
            return;
        }
        active->post_detach_events(connection_id);
        respond(connection_id, correlation_id, ipc::ShutdownAccepted{});
    }

    /// Serial thread: resolve one pending permission confirmation (DEC-020).
    /// The hub is mutex-guarded, so the resolution order — and with it the
    /// first-response-wins winner — is whoever the serial context lets
    /// through first. Without the async surface the request face is
    /// explicitly `unavailable` instead of silently succeeding.
    void handle_permission_respond(std::uint64_t connection_id, std::uint64_t correlation_id,
                                   ipc::RespondPermissionRequest request) {
        if (confirmation_hub == nullptr) {
            fail(connection_id, correlation_id, "unavailable",
                 "permission confirmation surface is not active");
            return;
        }
        if (!confirmation_hub->resolve(request.request_id, request.approved)) {
            fail(connection_id, correlation_id, "not_found",
                 "unknown or already decided permission request id");
            return;
        }
        ipc::PermissionResponded responded;
        responded.request_id = std::move(request.request_id);
        respond(connection_id, correlation_id, std::move(responded));
    }

    /// Serial thread: the pending-confirmation snapshot (DEC-020) — the
    /// resync face of the permission confirmation stream (DEC-012 decision
    /// 4: events are notifications, snapshots are the source of truth).
    void handle_permission_list(std::uint64_t connection_id, std::uint64_t correlation_id) {
        if (confirmation_hub == nullptr) {
            fail(connection_id, correlation_id, "unavailable",
                 "permission confirmation surface is not active");
            return;
        }
        ipc::PermissionPendingList list;
        list.pending.reserve(confirmation_hub->pending().size());
        for (const auto &pending : confirmation_hub->pending()) {
            ipc::PendingPermission entry;
            entry.request_id = pending.request_id;
            entry.capability = pending.capability;
            entry.resource = pending.resource;
            entry.task_id = pending.task_id;
            entry.timeout_ms = pending.timeout_ms;
            list.pending.push_back(std::move(entry));
        }
        respond(connection_id, correlation_id, std::move(list));
    }

    /// Serial thread: the session registry snapshot (DEC-021) — the resync
    /// face of the session event stream. State is projected live from the
    /// pinned runtime; an entry whose pinned view fails surfaces the
    /// conservative `failed` name instead of an invented state.
    void handle_session_list(std::uint64_t connection_id, std::uint64_t correlation_id) {
        std::vector<std::pair<std::string, std::int64_t>> registered;
        {
            std::lock_guard lock(core->sessions.mutex);
            registered.reserve(core->sessions.created_at_ms.size());
            for (const auto &entry : core->sessions.created_at_ms) {
                registered.emplace_back(entry.first, entry.second);
            }
        }
        ipc::SessionList list;
        list.sessions.reserve(registered.size());
        for (auto &session : registered) {
            ipc::SessionSummary summary;
            summary.id = session.first;
            summary.created_at_ms = session.second;
            const SessionViewResult view = core->host.session_view(SessionIdentity{session.first});
            summary.state = view.ok ? view.view.state : "failed";
            list.sessions.push_back(std::move(summary));
        }
        respond(connection_id, correlation_id, std::move(list));
    }

    /// Serial thread: open one more session on the hosted pinned runtime
    /// (DEC-021). Capacity is refused explicitly; the pinned receipt waits
    /// for the bounded command budget, exactly like the primary session at
    /// start().
    void handle_session_open(std::uint64_t connection_id, std::uint64_t correlation_id) {
        {
            std::lock_guard lock(core->sessions.mutex);
            if (core->sessions.full()) {
                fail(connection_id, correlation_id, "unavailable",
                     "session capacity exhausted (" + std::to_string(core->sessions.capacity) +
                         ")");
                return;
            }
        }
        const SessionOpenResult opened = core->host.open_session();
        if (!opened.ok) {
            fail(connection_id, correlation_id, opened.error.code, opened.error.message);
            return;
        }
        std::string state = "opening";
        {
            std::lock_guard lock(core->sessions.mutex);
            core->sessions.created_at_ms.emplace(opened.session.id, wall_now_ms());
        }
        const SessionViewResult view = core->host.session_view(opened.session);
        if (view.ok) {
            state = view.view.state;
        }
        // The registry entry is the fact; the event the notification.
        ipc::SessionUpdatedEvent event;
        event.session_id = opened.session.id;
        event.state = state;
        core->events.publish_session_update(std::move(event));
        detail::persist_session_state(core);
        respond(connection_id, correlation_id, ipc::SessionOpened{opened.session.id});
    }

    /// Serial thread: close one session and drop its registry entry
    /// (DEC-026 backlog item 2). The pinned close cancels the session's
    /// non-terminal tasks and settles it Closed; the local drivers of those
    /// tasks get the same cooperative interruption task.cancel uses so
    /// in-flight provider actions end promptly. The primary session is
    /// product equipment (opened at start, task.submit's default binding)
    /// and is refused before anything is touched; unknown ids are the same
    /// stable not_found as the other session faces. Removal is the fact;
    /// session.updated carries the closed state as the notification.
    void handle_session_close(std::uint64_t connection_id, std::uint64_t correlation_id,
                              std::string session_id) {
        if (core->host.primary_session().id == session_id) {
            fail(connection_id, correlation_id, "invalid_state",
                 "the primary session cannot be closed");
            return;
        }
        {
            std::lock_guard lock(core->sessions.mutex);
            if (!core->sessions.contains(session_id)) {
                fail(connection_id, correlation_id, "not_found", "unknown session id");
                return;
            }
        }
        // Cooperative interruption for the session's own drivers: the same
        // two-step (cancel token + executor task cancel) task.cancel uses,
        // so an in-flight desktop action ends at its next observation point
        // instead of outrunning the pinned cancel.
        {
            std::vector<std::string> driver_ids;
            {
                std::lock_guard lock(core->registry.mutex);
                for (auto &[task_id, record] : core->registry.tasks) {
                    if (record.session_id == session_id && !record.driver_done) {
                        record.cancel.request_cancel();
                        driver_ids.push_back(task_id);
                    }
                }
            }
            {
                std::lock_guard lock(core->drivers_mutex);
                for (const std::string &task_id : driver_ids) {
                    if (auto driver = core->drivers.find(task_id);
                        driver != core->drivers.end() && driver->second.handle.valid()) {
                        core->executor.request_task_cancel(driver->second.handle);
                    }
                }
            }
        }
        // Hydrated sessions (M5-08): a previous-era session has no pinned
        // counterpart, so there is nothing to close pinned-side — the close
        // is a pure product-state removal. Live sessions go through the
        // pinned close (which cancels their non-terminal pinned tasks).
        const bool hydrated = core->hydrated_sessions.count(session_id) != 0;
        if (!hydrated) {
            const HostOutcome closed = core->host.close_session(SessionIdentity{session_id});
            if (!closed.ok) {
                fail(connection_id, correlation_id, closed.error.code, closed.error.message);
                return;
            }
        }
        {
            std::lock_guard lock(core->sessions.mutex);
            core->sessions.created_at_ms.erase(session_id);
        }
        core->hydrated_sessions.erase(session_id);
        detail::persist_session_state(core);
        detail::persist_session_state(core);
        // The dialog thread dies with the session (DEC-027): the log entry is
        // removed (freeing the dialog registry slot for reuse) and any
        // in-flight turn's task is cancelled so it neither lingers nor
        // publishes into a closed session — its settle path finds the log
        // gone and swallows the outcome.
        {
            std::string in_flight_turn;
            {
                // dialogs.mutex -> journal_raw_mutex keeps the canonical
                // nesting order shared with persist_session_state (see the
                // comment there); sessions.mutex was already released above.
                std::lock_guard lock(core->dialogs.mutex);
                if (const auto found = core->dialogs.sessions.find(session_id);
                    found != core->dialogs.sessions.end()) {
                    in_flight_turn = found->second.in_flight_turn_id;
                    core->dialogs.sessions.erase(found);
                }
                std::lock_guard raw_lock(core->journal_raw_mutex);
                core->journal_raw.erase(session_id);
            }
            if (!in_flight_turn.empty()) {
                const std::string driver_key = "dialog-" + in_flight_turn;
                std::lock_guard lock(core->drivers_mutex);
                if (auto driver = core->drivers.find(driver_key);
                    driver != core->drivers.end() && driver->second.handle.valid()) {
                    core->executor.request_task_cancel(driver->second.handle);
                }
            }
        }
        // Post-close state from the pinned view; the close command's own
        // settled post-condition is Closed, so a failed view read still
        // reports the command's outcome rather than an invented state.
        std::string state = "closed";
        const SessionViewResult view = core->host.session_view(SessionIdentity{session_id});
        if (view.ok) {
            state = view.view.state;
        }
        ipc::SessionUpdatedEvent event;
        event.session_id = session_id;
        event.state = state;
        core->events.publish_session_update(std::move(event));
        respond(connection_id, correlation_id, ipc::SessionClosed{std::move(session_id), state});
    }

    /// Serial thread: accept one dialog turn (DEC-027, M5-06; DEC-025
    /// backlog item 3). The model inference is a bounded long-running work
    /// unit, so the handler only validates, registers the pending turn and
    /// submits a cancellable executor task — the ack carries the turn id,
    /// and the settled reply/error rides session.chat_updated +
    /// session.chat.history. One turn per session at a time; the task
    /// settles through the model layer's own deadlines and cancellation.
    void handle_session_chat(std::uint64_t connection_id, std::uint64_t correlation_id,
                             ipc::SessionChatRequest request) {
        if (core->model_layer == nullptr || !core->model_layer->running()) {
            fail(connection_id, correlation_id, "unavailable", "model layer is not configured");
            return;
        }
        if (request.text.size() > kMaxDialogTextBytes) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "dialog text exceeds the " + std::to_string(kMaxDialogTextBytes) + " byte budget");
            return;
        }
        std::string turn_id;
        std::uint64_t sequence = 0;
        {
            std::lock_guard lock(core->sessions.mutex);
            if (!core->sessions.contains(request.session_id)) {
                fail(connection_id, correlation_id, "not_found", "unknown session id");
                return;
            }
        }
        {
            std::lock_guard lock(core->dialogs.mutex);
            auto found = core->dialogs.sessions.find(request.session_id);
            if (found == core->dialogs.sessions.end()) {
                // Lazy first-turn creation (the session itself is known — the
                // session registry check above passed); the dialog registry
                // is capacity-bounded alongside it.
                if (core->dialogs.full()) {
                    fail(connection_id, correlation_id, "unavailable",
                         "dialog registry capacity exhausted (" +
                             std::to_string(core->dialogs.capacity) + ")");
                    return;
                }
                found =
                    core->dialogs.sessions.emplace(request.session_id, detail::SessionDialogLog{})
                        .first;
            }
            auto &log = found->second;
            (void)found;
            if (!log.in_flight_turn_id.empty()) {
                fail(connection_id, correlation_id, "invalid_state",
                     "a dialog turn is already in flight for this session");
                return;
            }
            if (log.turns.size() >= log.max_turns) {
                log.turns.erase(log.turns.begin());
            }
            detail::DialogTurnRecord record;
            record.turn_id = make_turn_id();
            record.status = "pending";
            record.user_text = request.text;
            record.sequence = log.next_sequence++;
            record.recorded_at_ms = wall_now_ms();
            log.in_flight_turn_id = record.turn_id;
            log.turns.push_back(record);
            turn_id = record.turn_id;
            sequence = record.sequence;
        }
        // The pending notification leaves before the acknowledgement so a
        // subscriber never observes the ack for a turn whose creation event
        // is still queued (DEC-012 decision 3 ordering precedent).
        ipc::ChatTurnUpdatedEvent pending;
        pending.session_id = request.session_id;
        pending.turn_id = turn_id;
        pending.status = "pending";
        pending.user_text = request.text;
        pending.sequence = sequence;
        core->events.publish_chat_turn(std::move(pending));
        ipc::DialogTurnAccepted accepted;
        accepted.turn_id = turn_id;
        respond(connection_id, correlation_id, std::move(accepted));

        // Bounded worker: the pinned model stack enforces its own transport
        // deadlines; the executor stop token ends the wait on teardown.
        const std::string session_id = request.session_id;
        const std::string transcript = render_dialog_transcript(core, session_id);
        auto dialog_submission =
            core->executor.submit_cancellable([core = core, session_id, turn_id, transcript,
                                               text = request.text](executor::StopToken stop) {
                run_dialog_turn(core, session_id, turn_id, transcript, text, stop);
            });
        {
            std::lock_guard lock(core->drivers_mutex);
            core->drivers.emplace("dialog-" + turn_id, std::move(dialog_submission));
        }
    }

    /// Serial thread: one session's dialog thread snapshot (DEC-027) — the
    /// resync face of the session.chat_updated stream.
    void handle_session_chat_history(std::uint64_t connection_id, std::uint64_t correlation_id,
                                     ipc::ChatHistoryRequest request) {
        {
            std::lock_guard lock(core->sessions.mutex);
            if (!core->sessions.contains(request.session_id)) {
                fail(connection_id, correlation_id, "not_found", "unknown session id");
                return;
            }
        }
        const std::size_t requested =
            request.limit ? static_cast<std::size_t>(*request.limit) : kDefaultHistoryLimit;
        ipc::DialogHistory history;
        history.session_id = request.session_id;
        {
            std::lock_guard lock(core->dialogs.mutex);
            const auto found = core->dialogs.sessions.find(request.session_id);
            if (found == core->dialogs.sessions.end()) {
                respond(connection_id, correlation_id, std::move(history));
                return;
            }
            auto &log = found->second;
            const std::size_t count = std::min(requested, log.turns.size());
            history.turns.reserve(count);
            for (std::size_t index = log.turns.size() - count; index < log.turns.size(); ++index) {
                const detail::DialogTurnRecord &record = log.turns[index];
                ipc::DialogTurnEntry entry;
                entry.turn_id = record.turn_id;
                entry.status = record.status;
                entry.user_text = record.user_text;
                entry.reply_text = record.reply_text;
                entry.has_reply = record.status == "ok";
                entry.error = record.error;
                entry.has_error = record.status == "failed";
                entry.sequence = record.sequence;
                entry.recorded_at_ms = record.recorded_at_ms;
                history.turns.push_back(std::move(entry));
            }
            history.truncated = log.total_recorded > history.turns.size();
        }
        respond(connection_id, correlation_id, std::move(history));
    }

    /// Serial thread: the live desktop permission policy (M5-07, DEC-010) —
    /// the full DEC-010 rule set projected from the controller plus the
    /// read-roots resource scope mirror. Always served.
    void handle_policy_get(std::uint64_t connection_id, std::uint64_t correlation_id) {
        ipc::PolicyView view;
        const permission::PermissionPolicy policy = core->permission->policy();
        for (std::size_t index = 0;
             index < policy.rules.size() &&
             index < static_cast<std::size_t>(permission::Capability::NotificationPost) + 1;
             ++index) {
            const auto capability = static_cast<permission::Capability>(index);
            view.rules.insert_or_assign(permission::capability_name(capability),
                                        permission::rule_name(policy.rule_for(capability)));
        }
        view.read_roots = core->read_roots;
        respond(connection_id, correlation_id, std::move(view));
    }

    /// Serial thread: apply a new rule set (M5-07, DEC-010) and persist the
    /// merged settings document (DEC-011 Desktop Permissions entry). Rules
    /// take effect immediately (the controller is shared with every task
    /// driver and the atom toolset gate); read roots take effect for the
    /// bound provider at the next start and are persisted as scope state.
    void handle_policy_set(std::uint64_t connection_id, std::uint64_t correlation_id,
                           ipc::SetPolicyRequest request) {
        // Validate coverage before touching anything: the wire face carries
        // the full DEC-010 set (strict decode above already validated the
        // vocabulary).
        permission::PermissionPolicy policy;
        bool coverage_ok = true;
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(permission::Capability::NotificationPost) + 1 &&
             coverage_ok;
             ++index) {
            const auto capability = static_cast<permission::Capability>(index);
            const auto found = request.rules.find(permission::capability_name(capability));
            if (found == request.rules.end()) {
                coverage_ok = false;
                break;
            }
            const auto rule = permission::rule_from_name(found->second);
            if (!rule) {
                coverage_ok = false;
                break;
            }
            policy.rules[index] = *rule;
        }
        if (!coverage_ok) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "policy.set 'rules' must cover every DEC-010 capability");
            return;
        }
        if (request.read_roots.size() > kMaxPolicyReadRoots) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "policy.set 'read_roots' exceeds " + std::to_string(kMaxPolicyReadRoots) +
                     " entries");
            return;
        }

        // Live effect first: the controller is shared with every task driver
        // and the atom toolset gate, so the new rules govern the next
        // authorize() call.
        core->permission->set_policy(std::move(policy));
        if (request.has_read_roots) {
            core->read_roots = request.read_roots;
        }

        // Persistence (DEC-011 Desktop Permissions entry): read-modify-write
        // the settings document, preserving the members this face does not
        // own (socket, confirmation). A corrupt or unreadable existing
        // document refuses the write instead of overwriting user state
        // (DEC-011's fail-closed posture for user-authoritative input).
        if (core->settings_store == nullptr) {
            // Write-back disabled (tests): the live effect above still holds.
            ipc::PolicyView view;
            const permission::PermissionPolicy applied = core->permission->policy();
            for (std::size_t index = 0;
                 index < static_cast<std::size_t>(permission::Capability::NotificationPost) + 1;
                 ++index) {
                const auto capability = static_cast<permission::Capability>(index);
                view.rules.insert_or_assign(permission::capability_name(capability),
                                            permission::rule_name(applied.rule_for(capability)));
            }
            view.read_roots = core->read_roots;
            respond(connection_id, correlation_id, std::move(view));
            return;
        }
        std::string persist_error;
        if (!persist_policy_document(core, core->read_roots, &persist_error)) {
            fail(connection_id, correlation_id, "internal",
                 "policy persistence failed: " + persist_error);
            return;
        }
        ipc::PolicyView view;
        const permission::PermissionPolicy applied = core->permission->policy();
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(permission::Capability::NotificationPost) + 1;
             ++index) {
            const auto capability = static_cast<permission::Capability>(index);
            view.rules.insert_or_assign(permission::capability_name(capability),
                                        permission::rule_name(applied.rule_for(capability)));
        }
        view.read_roots = core->read_roots;
        respond(connection_id, correlation_id, std::move(view));
    }

    /// Read-modify-write of the settings document (DEC-011): carries the new
    /// rule set and read roots, preserves every other member. Returns false
    /// with a stable reason when the existing document cannot be trusted.
    static bool persist_policy_document(const std::shared_ptr<detail::ServiceCore> &core,
                                        const std::vector<std::string> &read_roots,
                                        std::string *error) {
        persistence::LocalSettings document;
        if (const auto loaded = core->settings_store->load();
            loaded.status == persistence::LoadStatus::Loaded) {
            const auto decoded = persistence::decode_settings(loaded.body);
            if (!decoded.ok) {
                *error = "existing settings document is invalid: " + decoded.error;
                return false;
            }
            document = decoded.settings;
        } else if (loaded.status == persistence::LoadStatus::IoError) {
            // Absent is fine (defaults); a real read failure refuses.
            *error = "existing settings document cannot be read: " + loaded.error;
            return false;
        }
        document.permission_rules.clear();
        const permission::PermissionPolicy applied = core->permission->policy();
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(permission::Capability::NotificationPost) + 1;
             ++index) {
            const auto capability = static_cast<permission::Capability>(index);
            document.permission_rules.insert_or_assign(
                permission::capability_name(capability),
                permission::rule_name(applied.rule_for(capability)));
        }
        document.read_roots = read_roots;
        const auto saved = core->settings_store->save(persistence::encode_settings(document));
        if (!saved.ok) {
            *error = saved.error;
            return false;
        }
        return true;
    }

    /// Serial thread: one session's conversation history (DEC-021) — the
    /// resync face of the session.message stream, rebuilt from the journal
    /// through the pinned conversation projection.
    void handle_session_history(std::uint64_t connection_id, std::uint64_t correlation_id,
                                ipc::SessionHistoryRequest request) {
        {
            std::lock_guard lock(core->sessions.mutex);
            if (!core->sessions.contains(request.session_id)) {
                fail(connection_id, correlation_id, "not_found", "unknown session id");
                return;
            }
        }
        const std::size_t requested =
            request.limit ? static_cast<std::size_t>(*request.limit) : kDefaultHistoryLimit;
        const std::size_t limit = std::clamp<std::size_t>(requested, 1, core->max_history_entries);
        const auto history = core->journal->history(request.session_id, limit);
        if (!history.ok) {
            fail(connection_id, correlation_id, "internal", history.error);
            return;
        }
        ipc::SessionHistory response;
        response.session_id = std::move(request.session_id);
        response.truncated = history.truncated;
        response.entries.reserve(history.entries.size());
        for (const auto &entry : history.entries) {
            ipc::SessionHistoryEntry item;
            item.kind = entry.kind;
            item.text = entry.text;
            item.sequence = entry.sequence;
            item.recorded_at_ms = entry.recorded_at_ms;
            response.entries.push_back(std::move(item));
        }
        respond(connection_id, correlation_id, std::move(response));
    }

    // --- workflow face (DEC-023) -------------------------------------------

    /// Serial thread: the workflow catalog snapshot (DEC-023) — the resync
    /// face of the product catalog. Identity and validation are registry
    /// state written by save/publish; the pinned library stays the
    /// execution-side authority.
    void handle_workflow_list(std::uint64_t connection_id, std::uint64_t correlation_id) {
        std::map<std::string, WorkflowCatalogEntry> entries;
        {
            std::lock_guard lock(core->workflows.mutex);
            entries = core->workflows.workflows;
        }
        ipc::WorkflowList list;
        list.workflows.reserve(entries.size());
        for (auto &[workflow_id, entry] : entries) {
            ipc::WorkflowSummary summary;
            summary.workflow_id = workflow_id;
            summary.name = entry.name;
            summary.head_digest = entry.head_digest;
            summary.validation = entry.validation;
            summary.runnable = entry.runnable;
            summary.updated_at_ms = entry.updated_at_ms;
            list.workflows.push_back(std::move(summary));
        }
        respond(connection_id, correlation_id, std::move(list));
    }

    /// Serial thread: append one draft version (DEC-023). Capacity and byte
    /// budget are refused before the host call so a rejection never leaves a
    /// pinned library record behind.
    void handle_workflow_save(std::uint64_t connection_id, std::uint64_t correlation_id,
                              ipc::WorkflowSaveRequest request) {
        if (request.definition_json.size() > kMaxWorkflowDefinitionBytes) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "workflow definition exceeds the " + std::to_string(kMaxWorkflowDefinitionBytes) +
                     " byte budget");
            return;
        }
        const auto saved =
            core->host.save_workflow_definition(request.definition_json, "workflow.save draft");
        if (!saved.ok) {
            fail(connection_id, correlation_id, saved.error.code, saved.error.message);
            return;
        }
        {
            std::lock_guard lock(core->workflows.mutex);
            if (!core->workflows.contains(saved.workflow_id) && core->workflows.full()) {
                fail(connection_id, correlation_id, "unavailable",
                     "workflow registry capacity exhausted (" +
                         std::to_string(core->workflows.capacity) + ")");
                return;
            }
            WorkflowCatalogEntry entry;
            entry.name = saved.name;
            entry.head_digest = saved.digest;
            entry.validation = "not_validated";
            entry.runnable = false;
            entry.updated_at_ms = wall_now_ms();
            // DEC-026: the head definition content rides the product index
            // (the pinned library keeps version records only), so
            // workflow.get can serve the cross-session editing face.
            entry.definition_json = request.definition_json;
            core->workflows.workflows[saved.workflow_id] = std::move(entry);
        }
        respond(connection_id, correlation_id, ipc::WorkflowSaved{saved.workflow_id, saved.digest});
    }

    /// Serial thread: run the pinned publish gate on the definition (DEC-023).
    /// The gate dry-runs synchronously on this (serial) context; the run
    /// registry is untouched. Capacity rules match workflow.save.
    void handle_workflow_publish(std::uint64_t connection_id, std::uint64_t correlation_id,
                                 ipc::WorkflowPublishRequest request) {
        if (request.definition_json.size() > kMaxWorkflowDefinitionBytes) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "workflow definition exceeds the " + std::to_string(kMaxWorkflowDefinitionBytes) +
                     " byte budget");
            return;
        }
        const auto published = core->host.publish_workflow_definition(request.definition_json,
                                                                      "workflow.publish gate");
        if (!published.ok) {
            fail(connection_id, correlation_id, published.error.code, published.error.message);
            return;
        }
        {
            std::lock_guard lock(core->workflows.mutex);
            if (!core->workflows.contains(published.workflow_id) && core->workflows.full()) {
                fail(connection_id, correlation_id, "unavailable",
                     "workflow registry capacity exhausted (" +
                         std::to_string(core->workflows.capacity) + ")");
                return;
            }
            WorkflowCatalogEntry entry;
            entry.name = published.name;
            entry.head_digest = published.digest;
            entry.validation = "dry_run_passed";
            entry.runnable = true;
            entry.updated_at_ms = wall_now_ms();
            // DEC-026: head definition content for workflow.get (see
            // handle_workflow_save).
            entry.definition_json = request.definition_json;
            core->workflows.workflows[published.workflow_id] = std::move(entry);
        }
        ipc::WorkflowPublished payload;
        payload.workflow_id = published.workflow_id;
        payload.digest = published.digest;
        payload.dry_run_id = published.dry_run_id;
        payload.idempotent = published.idempotent;
        respond(connection_id, correlation_id, std::move(payload));
    }

    /// Serial thread: remove the product catalog entry (DEC-023). The pinned
    /// append-only history is untouched; a workflow with non-terminal runs
    /// refuses deletion (the runs still reference their content).
    void handle_workflow_delete(std::uint64_t connection_id, std::uint64_t correlation_id,
                                std::string workflow_id) {
        bool known = false;
        {
            std::lock_guard lock(core->workflows.mutex);
            known = core->workflows.contains(workflow_id);
        }
        if (!known) {
            fail(connection_id, correlation_id, "not_found", "unknown workflow id");
            return;
        }
        if (workflow_has_non_terminal_runs(workflow_id)) {
            fail(connection_id, correlation_id, "invalid_state", "workflow has non-terminal runs");
            return;
        }
        {
            std::lock_guard lock(core->workflows.mutex);
            core->workflows.workflows.erase(workflow_id);
        }
        respond(connection_id, correlation_id, ipc::WorkflowDeleted{std::move(workflow_id)});
    }

    /// Serial thread: the exposed view of the hosted BuiltIn tool registry
    /// (DEC-022 decision 1, DEC-023) — the desktop atom toolset built at
    /// start() (DEC-024). Empty only when the bound environment carries no
    /// mapped providers; the wire shape is the contract.
    void handle_workflow_atom_catalog(std::uint64_t connection_id, std::uint64_t correlation_id) {
        ipc::WorkflowAtomCatalog catalog;
        if (core->workflow_tools) {
            const auto atoms = core->workflow_tools->exposed_atoms();
            catalog.tools.reserve(atoms.size());
            for (const auto &atom : atoms) {
                ipc::ExposedTool tool;
                tool.wire_name = atom.wire_name;
                tool.version = atom.version;
                tool.description = atom.description;
                tool.has_side_effects = atom.has_side_effects;
                tool.parameters_schema_json = atom.parameters_schema_json;
                catalog.tools.push_back(std::move(tool));
            }
        }
        respond(connection_id, correlation_id, std::move(catalog));
    }

    /// Serial thread: the run registry snapshot (DEC-023) — the resync face
    /// of the workflow.run_updated stream. States re-project live from the
    /// pinned runtime; a failed snapshot surfaces the conservative `failed`.
    void handle_workflow_runs(std::uint64_t connection_id, std::uint64_t correlation_id) {
        std::map<std::string, WorkflowRunRecord> runs;
        {
            std::lock_guard lock(core->workflow_runs.mutex);
            runs = core->workflow_runs.runs;
        }
        ipc::WorkflowRunList list;
        list.runs.reserve(runs.size());
        for (auto &[run_id, record] : runs) {
            ipc::WorkflowRunSummary summary;
            summary.run_id = run_id;
            summary.workflow_id = record.workflow_id;
            summary.state = "failed";
            summary.created_at_ms = record.created_at_ms;
            const WorkflowRunViewResult view = core->host.workflow_run_view(run_id);
            if (view.ok) {
                summary.state = view.view.state;
                summary.run_epoch = view.view.run_epoch;
            }
            list.runs.push_back(std::move(summary));
        }
        respond(connection_id, correlation_id, std::move(list));
    }

    /// Serial thread: admit one asynchronous run (DEC-023). The version pins
    /// by digest (the registry head when absent); the run registry evicts
    /// terminal entries before refusing admission.
    void handle_workflow_run(std::uint64_t connection_id, std::uint64_t correlation_id,
                             ipc::WorkflowRunRequest request) {
        std::string digest = request.digest;
        {
            std::lock_guard lock(core->workflows.mutex);
            const auto entry = core->workflows.workflows.find(request.workflow_id);
            if (entry == core->workflows.workflows.end()) {
                fail(connection_id, correlation_id, "not_found", "unknown workflow id");
                return;
            }
            if (digest.empty()) {
                digest = entry->second.head_digest;
            }
        }
        if (!make_room_for_run()) {
            fail(connection_id, correlation_id, "unavailable",
                 "workflow run registry capacity exhausted (" +
                     std::to_string(core->workflow_runs.capacity) + ")");
            return;
        }
        const auto started = core->host.start_workflow_run(request.workflow_id, digest,
                                                           request.parameters_json, request.policy);
        if (!started.ok) {
            fail(connection_id, correlation_id, started.error.code, started.error.message);
            return;
        }
        {
            std::lock_guard lock(core->workflow_runs.mutex);
            WorkflowRunRecord record;
            record.workflow_id = request.workflow_id;
            record.created_at_ms = wall_now_ms();
            core->workflow_runs.runs[started.run_id] = std::move(record);
        }
        respond(connection_id, correlation_id, ipc::WorkflowRunStarted{started.run_id});
    }

    /// Serial thread: idempotent run cancellation (DEC-023); the reply
    /// carries the pinned view's post-call state.
    void handle_workflow_cancel(std::uint64_t connection_id, std::uint64_t correlation_id,
                                std::string run_id) {
        const WorkflowRunCancelResult cancelled = core->host.cancel_workflow_run(run_id);
        if (!cancelled.ok) {
            fail(connection_id, correlation_id, cancelled.error.code, cancelled.error.message);
            return;
        }
        ipc::WorkflowRunCancelled payload;
        payload.run_id = std::move(run_id);
        payload.state = cancelled.state;
        respond(connection_id, correlation_id, std::move(payload));
    }

    /// Serial thread: the definition read face (DEC-026, DEC-023 backlog
    /// item 1) — the head definition content the service last saved or
    /// published for the workflow, projected from the product catalog. The
    /// pinned library keeps version records only (W-03 content addressing,
    /// no body read API), so the catalog content is the only honest read
    /// source; an unknown id is the same stable not_found as workflow.run.
    void handle_workflow_get(std::uint64_t connection_id, std::uint64_t correlation_id,
                             std::string workflow_id) {
        WorkflowCatalogEntry entry;
        {
            std::lock_guard lock(core->workflows.mutex);
            const auto found = core->workflows.workflows.find(workflow_id);
            if (found == core->workflows.workflows.end()) {
                fail(connection_id, correlation_id, "not_found", "unknown workflow id");
                return;
            }
            entry = found->second;
        }
        ipc::WorkflowDefinitionView view;
        view.workflow_id = std::move(workflow_id);
        view.digest = std::move(entry.head_digest);
        view.definition_json = std::move(entry.definition_json);
        respond(connection_id, correlation_id, std::move(view));
    }

    /// Serial thread: one on-demand desktop observation (DEC-026, M5-06) —
    /// the assembler's projection enters the UI observation face, visual
    /// generation included when one is published and the request asks for
    /// it. Requested components are mandatory (the assembler's fail-closed
    /// discipline): a component the environment cannot deliver fails the
    /// request with the stable `unavailable` error naming the component,
    /// and the client re-asks without it. The capture is bounded by the
    /// providers' own budgets; there is no event form (DEC-026: the M1
    /// driver form has no observation producer to stream from).
    void handle_desktop_observe(std::uint64_t connection_id, std::uint64_t correlation_id,
                                const ipc::DesktopObserveRequest &request) {
        if (!core->environment) {
            fail(connection_id, correlation_id, "unavailable", "no desktop environment bound");
            return;
        }
        mirage::desktop::ObservationComponents components;
        components.active_window = true;
        components.pointer_state = true;
        components.environment_state = true;
        components.semantic_snapshot = request.semantic;
        components.visual_snapshot = request.visual;
        mirage::desktop::ObservationAssembler assembler(*core->environment, core->visual_registry);
        const mirage::desktop::ObservationAssemblyLimits limits;
        const auto outcome = assembler.assemble(components, limits, mirage::desktop::CancelToken{});
        if (outcome.cancelled) {
            // No cancellation path feeds this capture today; the stable
            // shape stays explicit instead of pretending success.
            fail(connection_id, correlation_id, "unavailable", "observation was cancelled");
            return;
        }
        if (!outcome.ok) {
            const mirage::desktop::ObservationComponentResult *failed = nullptr;
            if (outcome.semantic_snapshot.requested && !outcome.semantic_snapshot.captured) {
                failed = &outcome.semantic_snapshot;
            } else if (outcome.visual_snapshot.requested && !outcome.visual_snapshot.captured) {
                failed = &outcome.visual_snapshot;
            } else if (outcome.active_window.requested && !outcome.active_window.captured) {
                failed = &outcome.active_window;
            } else if (outcome.pointer_state.requested && !outcome.pointer_state.captured) {
                failed = &outcome.pointer_state;
            } else if (outcome.environment_state.requested && !outcome.environment_state.captured) {
                failed = &outcome.environment_state;
            }
            std::string detail = "observation unavailable";
            if (failed != nullptr && !failed->error.code.empty()) {
                detail += ": " + failed->error.code;
                if (!failed->error.message.empty()) {
                    detail += ": " + failed->error.message;
                }
            }
            fail(connection_id, correlation_id, "unavailable", std::move(detail));
            return;
        }
        respond(connection_id, correlation_id, project_observation(outcome.observation, request));
    }

    /// Projects a captured DesktopObservation onto the wire view (DEC-026).
    /// The semantic component is capped at the wire node budget with the
    /// explicit `truncated` mark — the service-side snapshot stays whole
    /// (assembler budget), the wire view is bounded (RULE-07). Producer
    /// confidence stays off the wire: no UI consumer today (DEC-023
    /// add-what-is-consumed discipline).
    static ipc::ObservationView
    project_observation(const mirage::desktop::DesktopObservation &source,
                        const ipc::DesktopObserveRequest &request) {
        ipc::ObservationView view;
        view.active_application = source.active_application;
        view.active_window = source.active_window;
        view.window_geometry = {source.window_geometry.x, source.window_geometry.y,
                                source.window_geometry.width, source.window_geometry.height};
        view.window_focused = source.window_focused;
        view.focused_element = source.focused_element;
        view.pointer_x = source.pointer_state.x;
        view.pointer_y = source.pointer_state.y;
        view.environment_state = source.environment_state;
        if (request.semantic) {
            ipc::ObservationSemantic semantic;
            semantic.application = source.semantic_snapshot.application;
            semantic.window_title = source.semantic_snapshot.window_title;
            const auto &nodes = source.semantic_snapshot.nodes;
            const auto budget =
                std::min<std::size_t>(nodes.size(), ipc::kObservationNodeWireBudget);
            semantic.nodes.reserve(budget);
            for (std::size_t index = 0; index < budget; ++index) {
                const auto &node = nodes[index];
                ipc::ObservationNode projected;
                projected.ref = node.ref;
                projected.role = node.role;
                projected.name = node.name;
                projected.description = node.description;
                projected.parent = node.parent == mirage::desktop::kNoParent
                                       ? -1
                                       : static_cast<std::int64_t>(node.parent);
                projected.geometry = {node.geometry.x, node.geometry.y, node.geometry.width,
                                      node.geometry.height};
                projected.focused = node.focused;
                projected.enabled = node.enabled;
                semantic.nodes.push_back(std::move(projected));
            }
            semantic.truncated = nodes.size() > budget;
            view.semantic = std::move(semantic);
        }
        if (request.visual) {
            // The pair is set together or not at all: an empty scope ref
            // means no generation was captured, which the assembler already
            // reports as a failed component above.
            if (!source.visual_snapshot.scope_ref.empty()) {
                view.visual_snapshot_ref = source.visual_snapshot.scope_ref;
                std::vector<ipc::ObservationRegion> regions;
                regions.reserve(source.visual_snapshot.regions.size());
                for (const auto &region : source.visual_snapshot.regions) {
                    ipc::ObservationRegion projected;
                    projected.ref = region.ref;
                    switch (region.source) {
                    case mirage::desktop::VisualRegionSource::kOcr:
                        projected.source = "ocr";
                        break;
                    case mirage::desktop::VisualRegionSource::kDetector:
                        projected.source = "detector";
                        break;
                    case mirage::desktop::VisualRegionSource::kTemplate:
                        projected.source = "template";
                        break;
                    case mirage::desktop::VisualRegionSource::kGeometry:
                        projected.source = "geometry";
                        break;
                    }
                    projected.geometry = {region.bounds.x, region.bounds.y, region.bounds.width,
                                          region.bounds.height};
                    projected.text = region.text;
                    projected.template_id = region.template_id;
                    regions.push_back(std::move(projected));
                }
                view.visual_regions = std::move(regions);
            }
        }
        return view;
    }

    /// Serial thread: true when at least one run of the workflow sits in a
    /// non-terminal pinned state (live projection).
    bool workflow_has_non_terminal_runs(const std::string &workflow_id) {
        std::vector<std::string> run_ids;
        {
            std::lock_guard lock(core->workflow_runs.mutex);
            for (const auto &[run_id, record] : core->workflow_runs.runs) {
                if (record.workflow_id == workflow_id) {
                    run_ids.push_back(run_id);
                }
            }
        }
        for (const auto &run_id : run_ids) {
            const WorkflowRunViewResult view = core->host.workflow_run_view(run_id);
            if (!view.ok) {
                continue; // unknown runs are not live obstacles
            }
            if (view.view.state != "completed" && view.view.state != "failed" &&
                view.view.state != "cancelled") {
                return true;
            }
        }
        return false;
    }

    /// Serial thread: evict oldest terminal entries until the run registry
    /// has room; false when the registry is full of non-terminal runs. Host
    /// views are read under the registry mutex — both are leaves on the
    /// serial thread and nothing else takes them together.
    bool make_room_for_run() {
        std::lock_guard lock(core->workflow_runs.mutex);
        if (!core->workflow_runs.full()) {
            return true;
        }
        for (auto entry = core->workflow_runs.runs.begin();
             entry != core->workflow_runs.runs.end();) {
            if (!core->workflow_runs.full()) {
                return true;
            }
            const WorkflowRunViewResult view = core->host.workflow_run_view(entry->first);
            const bool terminal =
                view.ok && (view.view.state == "completed" || view.view.state == "failed" ||
                            view.view.state == "cancelled");
            entry = terminal ? core->workflow_runs.runs.erase(entry) : std::next(entry);
        }
        return !core->workflow_runs.full();
    }

    // --- lifecycle ---------------------------------------------------------

    void request_loop_stop() {
        if (lifecycle.load(std::memory_order_acquire) != Lifecycle::Running) {
            return;
        }
        std::lock_guard lock(loop_mutex);
        detail::ServiceLoop *active = loop.load(std::memory_order_acquire);
        if (active != nullptr) {
            active->stop_serving();
        } else if (loop_worker.started()) {
            loop_worker.request_stop();
        }
    }

    void teardown() {
        // Ordered shutdown (AGENTS.md rule 7): producers are already stopped
        // (loop exited, listener closed by run()). Recover the blocking
        // worker, cancel drivers and tasks, drain the executor, then release
        // the hosted pinned runtime on this (non-worker) thread.
        {
            // Detach and destroy the loop under the mutex so a concurrent
            // request_shutdown never dereferences a dying loop (TSan-clean).
            std::lock_guard lock(loop_mutex);
            loop.store(nullptr, std::memory_order_release);
            if (loop_worker.started()) {
                loop_worker.stop();
            }
        }
        std::vector<executor::TaskHandle> handles;
        std::vector<std::future<void>> driver_futures;
        {
            std::lock_guard lock(core->drivers_mutex);
            handles.reserve(core->drivers.size());
            driver_futures.reserve(core->drivers.size());
            for (auto &[id, submission] : core->drivers) {
                handles.push_back(submission.handle);
                driver_futures.push_back(std::move(submission.future));
            }
            core->drivers.clear();
        }
        for (const auto &handle : handles) {
            core->executor.request_task_cancel(handle);
        }
        std::vector<std::string> ids;
        {
            std::lock_guard lock(core->registry.mutex);
            ids = core->registry.ids();
        }
        for (const auto &id : ids) {
            // End in-flight desktop actions promptly: the desktop cancel
            // tokens break provider calls out of their budgets, the executor
            // stop tokens stop the drivers between steps.
            {
                std::lock_guard lock(core->registry.mutex);
                if (auto entry = core->registry.tasks.find(id);
                    entry != core->registry.tasks.end()) {
                    entry->second.cancel.request_cancel();
                }
            }
            // By-value id capture: a timed-out wait abandons the future and
            // the closure may run after this loop iteration ended.
            auto cancelled = core->executor.submit_on(core->serial, [core = core, id] {
                return core->host.cancel_task(TaskIdentity{id});
            });
            try {
                if (cancelled.valid() &&
                    cancelled.wait_for(core->command_wait) == std::future_status::ready) {
                    cancelled.get();
                }
            } catch (const std::exception &) {
                // Terminal or already cancelled tasks surface pinned
                // rejections here; the drain below settles the rest.
            }
        }
        // The workflow surface converges before its Executor (DEC-023 pinned
        // order: WorkflowRuntime shutdown -> MiraRuntime stop -> Executor
        // shutdown): cancel active runs and drain their drives while the
        // executor can still settle them.
        (void)core->host.shutdown_workflow_surface();
        // The model layer's transport workers are executor-backed (DEC-027):
        // settle their in-flight exchanges while the executor can still
        // drain them, before the executor itself shuts down.
        if (core->model_layer) {
            core->model_layer->shutdown();
        }
        // M5-08: the final session state snapshot lands while the executor
        // can still settle the write (same ordering rationale as recovery).
        persist_session_state(core);
        core->executor.shutdown(true);
        for (auto &future : driver_futures) {
            try {
                if (future.valid()) {
                    future.get();
                }
            } catch (const std::exception &) {
            }
        }
        // M1-07: drivers settled (or gave up on) every task above; the
        // final snapshot captures the exact terminal state this era leaves
        // behind, including tasks cancelled by the teardown itself.
        if (config.persist_recovery_state) {
            core->recovery.persist(core->registry);
        }
        // Host status transitions after the loop is gone have no live
        // subscribers, but the hub's LatestMailbox stays truthful for the
        // record (and for any hub-observing diagnostics).
        publish_host_status(HostStatus::Stopping);
        const ShutdownResult host_shutdown = core->host.shutdown();
        publish_host_status(host_shutdown.ok && host_shutdown.report.clean ? HostStatus::Stopped
                                                                           : HostStatus::Failed);
        core->serial.shutdown();
        run_report.clean = host_shutdown.ok && host_shutdown.report.clean;
        if (!run_report.clean) {
            run_report.diagnostic =
                host_shutdown.ok ? host_shutdown.report.diagnostic : host_shutdown.error.message;
        }
        run_report.host_shutdown = host_shutdown.report;
        lifecycle.store(Lifecycle::Terminal, std::memory_order_release);
    }
};

RuntimeService::RuntimeService(ServiceConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

RuntimeService::~RuntimeService() {
    if (impl_->lifecycle.load(std::memory_order_acquire) == Impl::Lifecycle::Running) {
        impl_->request_loop_stop();
        impl_->loop_done_future.wait();
        impl_->listener.close();
        impl_->teardown();
    }
}

const std::string &RuntimeService::socket_path() const { return impl_->socket_path; }

void RuntimeService::request_shutdown() { impl_->request_loop_stop(); }

void RuntimeService::register_shutdown_fd(int fd) { impl_->shutdown_fd = fd; }

ipc::ServiceIdentity RuntimeService::identity() const { return impl_->identity(); }

HostOutcome
RuntimeService::start(std::shared_ptr<mirage::integration::DesktopEnvironmentBinding> binding) {
    HostOutcome outcome;
    if (impl_->lifecycle.load(std::memory_order_acquire) != Impl::Lifecycle::New) {
        outcome.error = {"invalid_state", "service already started or terminal"};
        return outcome;
    }
    if (!binding) {
        outcome.error = {"invalid_argument", "binding is null"};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }
    if (impl_->config.step_timeout <= std::chrono::milliseconds::zero() ||
        impl_->config.max_steps_per_task == 0 || impl_->config.max_task_records == 0 ||
        impl_->config.max_result_bytes == 0 || impl_->config.event_queue_capacity == 0) {
        outcome.error = {"invalid_argument", "service config limits are empty"};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }
    if (impl_->config.confirmation != nullptr && impl_->config.confirmation_hub != nullptr) {
        // DEC-020 decision 9: the surfaces are mutually exclusive — a
        // double-configured service would resolve Confirm rules through an
        // ambiguous path.
        outcome.error = {"invalid_argument",
                         "confirmation and confirmation_hub are mutually exclusive"};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }
    if (impl_->config.confirmation_hub != nullptr &&
        impl_->config.confirmation_hub->wait_budget() <= std::chrono::milliseconds::zero()) {
        // A non-positive confirmation budget would converge every request to
        // TimedOut before any client can answer; refused instead (DEC-020).
        outcome.error = {"invalid_argument", "confirmation_hub wait budget must be positive"};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    // M1-07 recovery persistence: point the writer at the configured (or
    // default) state directory, then hydrate the terminal records of the
    // previous service era into the registry before any new task can be
    // admitted. A broken recovery file degrades loudly (writer records the
    // reason on stderr) instead of failing the start.
    if (impl_->config.persist_recovery_state) {
        const std::filesystem::path directory = impl_->config.recovery_directory.empty()
                                                    ? persistence::default_state_directory()
                                                    : impl_->config.recovery_directory;
        impl_->core->recovery.enable(
            persistence::LocalStateStore(directory, "task-recovery.json",
                                         persistence::kMaxRecoveryFileBytes),
            impl_->config.mirage_version, impl_->config.max_result_bytes);
        const std::size_t recovered =
            impl_->core->recovery.hydrate(impl_->core->registry, impl_->config.max_task_records);
        if (recovered > 0) {
            std::cerr << "mirage-service: recovered " << recovered << " task(s) from "
                      << (directory / "task-recovery.json").string() << '\n';
        }
    }

    impl_->core->environment = binding->bound_environment();
    if (!impl_->core->environment) {
        outcome.error = {"invalid_argument", "binding does not expose a mirage desktop "
                                             "environment; the M1 service cannot drive tasks"};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    executor::ExecutorConfig executor_config;
    if (impl_->config.executor_threads > 0) {
        // A fixed pool: the floor is the point. With an adaptive minimum the
        // pool may start with as few as two workers on a small machine, and
        // the first workflow ToolCall run deadlocks (the drive blocks on its
        // nested dispatch future while holding its worker; DEC-024).
        executor_config.min_threads = impl_->config.executor_threads;
        executor_config.max_threads = impl_->config.executor_threads;
    }
    const auto initialized = impl_->core->executor.initialize_ex(executor_config);
    if (!initialized.ok) {
        outcome.error = {"internal", "executor initialization failed: " + initialized.message};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    // Model layer (DEC-027): assembled once the executor is up (its pinned
    // transport runs blocking I/O workers on it) and before the host starts.
    // An enabled-but-invalid config fails start() closed; an enabled-but-
    // unassemblable one does too (a half-wired dialog face would be worse
    // than a dark one). A disabled config leaves the layer null.
    impl_->core->model = impl_->config.model;
    if (impl_->core->model.enabled) {
        std::string model_error;
        if (!impl_->core->model.valid(model_error)) {
            outcome.error = {"invalid_argument", model_error};
            impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
            return outcome;
        }
        impl_->core->model_layer = std::make_unique<mirage::integration::ModelLayer>(
            impl_->core->executor, impl_->core->model, impl_->config.model_provider_override.get());
        if (!impl_->core->model_layer->running()) {
            outcome.error = {"internal", "model layer assembly failed"};
            impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
            return outcome;
        }
    }
    {
        std::lock_guard lock(impl_->core->dialogs.mutex);
        impl_->core->dialogs.capacity = impl_->config.max_sessions;
    }
    // M5-07 policy face: the settings write-back store (DEC-011 Desktop
    // Permissions entry). The directory is created by the store's save
    // path; a null store (write-back disabled) keeps policy.set live-only.
    impl_->core->read_roots = impl_->config.read_roots;
    if (!impl_->config.settings_directory.empty() && impl_->config.persist_settings) {
        impl_->core->settings_store =
            std::make_unique<mirage::runtime::persistence::LocalStateStore>(
                impl_->config.settings_directory, impl_->config.settings_file_name,
                mirage::runtime::persistence::kMaxSettingsFileBytes);
    }
    // M5-08 session state store (DEC-021 backlog ①): the conversation and
    // dialog thread write-back document lives in the state directory.
    if (impl_->config.persist_session_state) {
        const std::filesystem::path session_directory =
            impl_->config.session_state_directory.empty() ? persistence::default_state_directory()
                                                          : impl_->config.session_state_directory;
        impl_->core->session_state_store =
            std::make_unique<mirage::runtime::persistence::LocalStateStore>(
                session_directory, "session-state.json",
                mirage::runtime::persistence::kMaxSettingsFileBytes * 8);
    }

    impl_->publish_host_status(HostStatus::Starting);

    // M5-08 session state hydration (DEC-021 backlog ①): re-register the
    // persisted sessions, re-append their conversation entries into the
    // journal (the pinned store re-derives the same per-session sequence
    // numbers from append order) and rebuild the dialog threads. A broken
    // session state file degrades loudly instead of failing the start.
    if (impl_->config.persist_session_state) {
        const std::filesystem::path session_directory =
            impl_->config.session_state_directory.empty() ? persistence::default_state_directory()
                                                          : impl_->config.session_state_directory;
        const auto loaded = impl_->core->session_state_store->load();
        if (loaded.status == persistence::LoadStatus::Loaded) {
            const auto decoded = persistence::decode_session_state(loaded.body);
            if (!decoded.ok) {
                std::cerr << "mirage-service: session state file '"
                          << (session_directory / "session-state.json").string()
                          << "' is invalid: " << decoded.error << "\n";
            } else {
                for (const auto &session : decoded.state.sessions) {
                    const auto admitted = impl_->core->sessions.full() == false;
                    if (!admitted) {
                        break;
                    }
                    impl_->core->sessions.created_at_ms.emplace(session.id, session.created_at_ms);
                    // The pinned counterpart is gone with the previous era:
                    // the hydrated session is product state only. Its close
                    // is handled service-side (DEC-028 close face without a
                    // pinned call), and task submit has no pinned session to
                    // reach (honest limitation of hydration).
                    impl_->core->hydrated_sessions.insert(session.id);
                    for (const auto &entry : session.journal) {
                        if (entry.kind == "user") {
                            (void)impl_->core->journal->append_user_message(
                                session.id, entry.task_id, entry.text);
                        } else {
                            (void)impl_->core->journal->append_outcome(session.id, entry.task_id,
                                                                       entry.outcome, entry.steps);
                        }
                    }
                    if (!session.chat_turns.empty()) {
                        std::lock_guard lock(impl_->core->dialogs.mutex);
                        auto &log = impl_->core->dialogs.sessions[session.id];
                        std::uint64_t next_sequence = 1;
                        for (const auto &turn : session.chat_turns) {
                            detail::DialogTurnRecord record;
                            record.turn_id = turn.turn_id;
                            record.status = turn.status;
                            record.user_text = turn.user_text;
                            record.reply_text = turn.reply_text;
                            record.error = turn.error;
                            record.sequence = turn.sequence;
                            record.recorded_at_ms = turn.recorded_at_ms;
                            next_sequence = std::max(next_sequence, turn.sequence + 1);
                            log.turns.push_back(std::move(record));
                        }
                        log.next_sequence = next_sequence;
                        log.total_recorded = log.turns.size();
                    }
                }
                if (!decoded.state.sessions.empty()) {
                    std::cerr << "mirage-service: hydrated " << decoded.state.sessions.size()
                              << " session(s) from "
                              << (session_directory / "session-state.json").string() << '\n';
                }
            }
        }
    }

    const HostOutcome hosted = impl_->core->host.start(binding);
    if (!hosted.ok) {
        impl_->publish_host_status(HostStatus::Failed);
        if (impl_->core->model_layer) {
            impl_->core->model_layer->shutdown();
        }
        impl_->core->executor.shutdown(false);
        outcome.error = hosted.error;
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }
    // The workflow surface rides the Running host (DEC-023): pinned
    // WorkflowRuntime over the service executor and the primary session,
    // with the bridge as its event store. The desktop atom toolset (DEC-024)
    // is built over the same environment with the shared RULE-05 gate, so
    // ToolCall steps dispatch through the same permission face as the task
    // drivers. A failure here fails start() closed — the workflow faces are
    // core equipment, not optional.
    impl_->core->workflow_tools = mirage::integration::DesktopAtomToolset::build(
        impl_->core->environment.get(),
        [permission =
             impl_->core->permission](const std::string &capability, const std::string &resource,
                                      const mirage::integration::AtomCancelProbe &cancelled) {
            if (!permission) {
                return false; // Fail closed: no controller, no capability use.
            }
            const auto parsed = permission::capability_from_name(capability);
            if (!parsed) {
                return false; // Fail closed: outside the judged vocabulary.
            }
            permission::PermissionRequest request;
            request.capability = *parsed;
            request.resource = resource;
            return permission->authorize(request, cancelled).allowed;
        });
    const HostOutcome workflow_surface = impl_->core->host.attach_workflow_surface(
        impl_->core->executor, impl_->core->workflow_bridge, impl_->core->workflow_tools);
    if (!workflow_surface.ok) {
        impl_->core->host.shutdown();
        impl_->publish_host_status(impl_->core->host.status());
        impl_->core->executor.shutdown(false);
        outcome.error = workflow_surface.error;
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    impl_->publish_host_status(HostStatus::Running);

    // The primary session enters the registry (DEC-021); session.list
    // reports it alongside the session.open ones.
    {
        const SessionIdentity primary = impl_->core->host.primary_session();
        std::lock_guard lock(impl_->core->sessions.mutex);
        impl_->core->sessions.created_at_ms.emplace(primary.id, wall_now_ms());
    }

    std::string diagnostic;
    impl_->listener = ipc::IpcListener::bind(impl_->socket_path, diagnostic);
    if (!impl_->listener.valid()) {
        impl_->core->host.shutdown();
        impl_->publish_host_status(impl_->core->host.status());
        impl_->core->executor.shutdown(false);
        outcome.error = {"internal", std::move(diagnostic)};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    detail::ServiceLoop::Dependencies dependencies;
    dependencies.listener = &impl_->listener;
    dependencies.max_connections = impl_->config.max_connections;
    dependencies.on_frame = [raw = impl_.get()](std::uint64_t connection_id, std::string payload) {
        raw->handle_frame(connection_id, payload);
    };
    dependencies.on_exit = [raw = impl_.get()] { raw->loop_done.set_value(); };
    auto owned_loop = std::make_unique<detail::ServiceLoop>(std::move(dependencies));
    if (impl_->shutdown_fd >= 0) {
        owned_loop->register_shutdown_fd(impl_->shutdown_fd);
    }
    impl_->loop.store(owned_loop.get(), std::memory_order_release);

    executor::BlockingWorkerSpec spec;
    spec.name = "mirage-ipc-loop";
    spec.config.thread_name = "mirage-ipc-loop";
    // The executor's blocking worker owns the loop from here on; Impl keeps
    // only the observer pointer above.
    spec.worker = std::move(owned_loop);
    impl_->loop_worker = impl_->core->executor.start_worker(std::move(spec));
    if (!impl_->loop_worker.started()) {
        impl_->loop.store(nullptr, std::memory_order_release);
        impl_->listener.close();
        impl_->core->host.shutdown();
        impl_->publish_host_status(impl_->core->host.status());
        impl_->core->executor.shutdown(false);
        outcome.error = {"internal", "blocking worker start failed: " +
                                         impl_->loop_worker.start_result().message};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }
    impl_->lifecycle.store(Impl::Lifecycle::Running, std::memory_order_release);
    outcome.ok = true;
    return outcome;
}

ServiceRunReport RuntimeService::run() {
    ServiceRunReport report;
    if (impl_->lifecycle.load(std::memory_order_acquire) != Impl::Lifecycle::Running) {
        report.diagnostic = "service is not running";
        return report;
    }
    // Blocks until the loop exits (shutdown requested via IPC, signal fd,
    // request_shutdown or executor stop), then tears down on this thread.
    impl_->loop_done_future.get();
    impl_->listener.close();
    impl_->teardown();
    return impl_->run_report;
}

} // namespace mirage::runtime
