#include <mirage/runtime/mira_host.hpp>

#include <mira/core_contracts.hpp>
#include <mira/runtime.hpp>
#include <mira/tool_skill.hpp>
#include <mira/version.hpp>
#include <mira/workflow_compiler.hpp>
#include <mira/workflow_ir.hpp>
#include <mira/workflow_recovery.hpp>
#include <mira/workflow_runtime.hpp>
#include <mirage/integration/workflow_learning_memory.hpp>
#include <mirage/integration/workflow_recovery_model.hpp>

#include <algorithm>
#include <atomic>
#include <functional>
#include <kairo/comm/double_buffer.hpp>
#include <unordered_map>
#include <utility>

namespace mirage::runtime {

MiraCoreVersion mira_core_version() {
    return {mira::kVersion.major, mira::kVersion.minor, mira::kVersion.patch};
}

std::string mira_core_version_string() {
    const MiraCoreVersion version = mira_core_version();
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

bool mira_core_compatible_with(int expected_major, int expected_minor) {
    const MiraCoreVersion version = mira_core_version();
    return version.major == expected_major && version.minor == expected_minor;
}

namespace {

const char *error_code_name(mira::ErrorCode code) {
    switch (code) {
    case mira::ErrorCode::Cancelled:
        return "cancelled";
    case mira::ErrorCode::DeadlineExceeded:
        return "deadline_exceeded";
    case mira::ErrorCode::ResourceExhausted:
        return "resource_exhausted";
    case mira::ErrorCode::Unavailable:
        return "unavailable";
    case mira::ErrorCode::PermissionDenied:
        return "permission_denied";
    case mira::ErrorCode::InvalidArgument:
        return "invalid_argument";
    case mira::ErrorCode::InvalidState:
        return "invalid_state";
    case mira::ErrorCode::NotFound:
        return "not_found";
    case mira::ErrorCode::AlreadyExists:
        return "already_exists";
    case mira::ErrorCode::UnsupportedCapability:
        return "unsupported_capability";
    case mira::ErrorCode::UnsupportedVersion:
        return "unsupported_version";
    case mira::ErrorCode::InvalidObservation:
        return "invalid_observation";
    case mira::ErrorCode::StaleObservation:
        return "stale_observation";
    case mira::ErrorCode::InvalidModelOutput:
        return "invalid_model_output";
    case mira::ErrorCode::ContextOverflow:
        return "context_overflow";
    case mira::ErrorCode::SafetyRejected:
        return "safety_rejected";
    case mira::ErrorCode::ConfirmationRequired:
        return "confirmation_required";
    case mira::ErrorCode::ExecutionUncertain:
        return "execution_uncertain";
    case mira::ErrorCode::DataLoss:
        return "data_loss";
    case mira::ErrorCode::PlatformError:
        return "platform_error";
    case mira::ErrorCode::Internal:
        return "internal";
    }
    return "unknown";
}

HostError pinned_error(const mira::Error &error) {
    std::string message =
        error.safe_message.empty() ? "pinned runtime rejected the request" : error.safe_message;
    return HostError{"pinned_runtime",
                     std::string(error_code_name(error.code)) + ": " + std::move(message)};
}

HostError host_error(std::string code, std::string message) {
    return HostError{std::move(code), std::move(message)};
}

HostOutcome failed(HostError error) { return HostOutcome{false, std::move(error)}; }

TaskProgress project_task_state(mira::TaskState state) {
    using mira::TaskState;
    switch (state) {
    case TaskState::Idle:
        return TaskProgress::Idle;
    case TaskState::Observing:
    case TaskState::Reasoning:
    case TaskState::Planning:
    case TaskState::Acting:
    case TaskState::Verifying:
    case TaskState::Recovering:
        return TaskProgress::Active;
    case TaskState::Pausing:
    case TaskState::Paused:
    case TaskState::TakeoverSettling:
    case TaskState::SuspendedForTakeover:
        return TaskProgress::Paused;
    case TaskState::Cancelling:
        return TaskProgress::Cancelling;
    case TaskState::Completed:
        return TaskProgress::Completed;
    case TaskState::Failed:
        return TaskProgress::Failed;
    case TaskState::Cancelled:
        return TaskProgress::Cancelled;
    }
    return TaskProgress::Unknown;
}

/// Stable lowercase wire form of the pinned SessionState (DEC-021); never
/// translated, the product layer matches on it and the golden vectors pin
/// the set on both ends.
const char *session_state_name(mira::SessionState state) {
    switch (state) {
    case mira::SessionState::Opening:
        return "opening";
    case mira::SessionState::Autonomous:
        return "autonomous";
    case mira::SessionState::TakeoverPending:
        return "takeover_pending";
    case mira::SessionState::HumanControlled:
        return "human_controlled";
    case mira::SessionState::Resuming:
        return "resuming";
    case mira::SessionState::Closing:
        return "closing";
    case mira::SessionState::Closed:
        return "closed";
    case mira::SessionState::Failed:
        return "failed";
    }
    return "failed";
}

} // namespace

const char *host_status_name(HostStatus status) {
    switch (status) {
    case HostStatus::Stopped:
        return "stopped";
    case HostStatus::Starting:
        return "starting";
    case HostStatus::Running:
        return "running";
    case HostStatus::Stopping:
        return "stopping";
    case HostStatus::Failed:
        return "failed";
    }
    return "unknown";
}

// DEC-150: independent workflow calls resolve the latest published target at dispatch.
// Registries are immutable after publication; the shared snapshot retains an in-flight
// call's handler without racing the serial publisher or mutating Mira's registry.
struct WorkflowCallVersion {
    mira::SkillPublicationRegistry skills;
    mira::BuiltinToolHandler handler;
};
struct WorkflowCallAlias {
    using Version = std::shared_ptr<const WorkflowCallVersion>;
    kairo::comm::DoubleBuffer<Version> current{Version{}, "workflow.call.latest"};
    std::vector<std::string> callees; // serial owner only
};

struct MiraHost::Impl {
    explicit Impl(HostConfig host_config)
        : config(host_config),
          runtime(mira::RuntimeConfig{host_config.worker_threads, host_config.queue_capacity,
                                      host_config.max_in_flight}) {}

    /// Best-effort ordered teardown of an initialized pinned runtime. Never
    /// moves the host status; callers decide the observable state.
    void release_pinned_runtime() {
        if (!runtime_initialized) {
            return;
        }
        if (auto stop = runtime.request_shutdown()) {
            (void)stop.value().outcome(config.shutdown_drain);
        }
        (void)runtime.finish_shutdown();
        runtime_initialized = false;
    }

    HostConfig config;
    mira::MiraRuntime runtime;
    std::atomic<HostStatus> status{HostStatus::Stopped};
    mira::SessionId session_id;
    /// The pinned environment start() bound; further sessions (DEC-021)
    /// open against the same environment.
    std::shared_ptr<mira::IEnvironment> environment;
    bool runtime_initialized = false;
    HostShutdownReport last_shutdown_report;

    /// Workflow execution surface (M5-05, DEC-023): one pinned WorkflowRuntime
    /// over the primary session, attached by the service after start(). The
    /// event bridge is kept alive here so the runtime's event store outlives
    /// every drive; constructed empty and engaged by attach.
    // DEC-150: registry and tools outlive runtime handlers and child Runs.
    std::unordered_map<std::string, std::shared_ptr<WorkflowCallAlias>> workflow_calls;
    std::shared_ptr<mira::BuiltinToolRegistry> workflow_tools;
    std::unique_ptr<mira::WorkflowRuntime> workflow_runtime;
    std::shared_ptr<mirage::integration::WorkflowEventBridge> workflow_events;
    kairo::Executor *workflow_executor = nullptr;
    std::shared_ptr<mirage::integration::WorkflowRecoveryModel> recovery_model;
    std::unique_ptr<mira::WorkflowRecoveryOrchestrator> workflow_recovery;
    bool recovery_drained = true;
    bool workflow_learning = false;

    /// True when the surface is attached and still accepting work; a shut
    /// down surface rejects every call with the stable invalid_state shape.
    [[nodiscard]] bool workflow_surface_active() const {
        return status.load() == HostStatus::Running && workflow_runtime != nullptr &&
               !workflow_runtime->shut_down();
    }
};

MiraHost::MiraHost(HostConfig config) : impl_(std::make_unique<Impl>(config)) {}

MiraHost::~MiraHost() {
    if (impl_) {
        // The workflow surface dies before the pinned runtime it rides on
        // (pinned teardown order, DEC-023).
        (void)shutdown_workflow_surface();
        impl_->release_pinned_runtime();
    }
}

HostStatus MiraHost::status() const { return impl_->status.load(); }

HostOutcome
MiraHost::start(std::shared_ptr<mirage::integration::DesktopEnvironmentBinding> binding) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Stopped) {
        return failed(
            host_error("invalid_state", std::string("start() requires a Stopped host, got ") +
                                            host_status_name(current)));
    }
    if (!binding) {
        return failed(host_error("invalid_argument", "binding is null"));
    }

    // Recover the pinned environment contract from the binding. A binding
    // whose most-derived type does not implement it cannot host and fails
    // closed here instead of producing a session with a null environment.
    const auto environment = std::dynamic_pointer_cast<mira::IEnvironment>(binding);
    if (!environment) {
        return failed(host_error("invalid_argument",
                                 std::string("binding '") + binding->binding_name() +
                                     "' does not implement the pinned environment contract"));
    }

    impl_->status.store(HostStatus::Starting);

    if (const auto initialized = impl_->runtime.initialize(); !initialized) {
        impl_->status.store(HostStatus::Failed);
        return failed(pinned_error(initialized.error()));
    }
    impl_->runtime_initialized = true;

    const auto session = impl_->runtime.open_session(environment);
    if (!session) {
        impl_->release_pinned_runtime();
        impl_->status.store(HostStatus::Failed);
        return failed(pinned_error(session.error()));
    }
    impl_->session_id = session.value().id;
    impl_->environment = environment;
    const auto receipt = session.value().command.receipt(impl_->config.command_wait);
    if (!receipt || receipt.value().status != mira::ReceiptStatus::Accepted) {
        impl_->release_pinned_runtime();
        impl_->status.store(HostStatus::Failed);
        return failed(receipt ? host_error("pinned_runtime", "session open command was rejected")
                              : pinned_error(receipt.error()));
    }

    impl_->status.store(HostStatus::Running);
    return HostOutcome{true, {}};
}

TaskSubmissionResult MiraHost::submit_task(const std::string &goal) {
    return submit_task(SessionIdentity{impl_->session_id.to_string()}, goal);
}

TaskSubmissionResult MiraHost::submit_task(const SessionIdentity &session,
                                           const std::string &goal) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return TaskSubmissionResult{
            false,
            {},
            host_error("invalid_state", std::string("submit_task() requires a Running host, got ") +
                                            host_status_name(current))};
    }
    if (goal.empty()) {
        return TaskSubmissionResult{
            false, {}, host_error("invalid_argument", "goal must not be empty")};
    }
    const auto session_id = mira::SessionId::parse(session.id);
    if (!session_id || session_id->is_nil()) {
        return TaskSubmissionResult{
            false, {}, host_error("invalid_argument", "malformed session identity")};
    }

    // M28/DEC-068 §4.5: TaskSpec carries optional user_constraints; default
    // construction keeps this site stable as the pinned struct grows and
    // satisfies -Wmissing-field-initializers for every added member. Mirage
    // does not populate constraints yet — later user bounds reach the loop
    // via its input seam, not by rewriting the goal.
    mira::TaskSpec task_spec;
    task_spec.goal = goal;
    const auto submission = impl_->runtime.submit_task(session_id.value(), task_spec);
    if (!submission) {
        return TaskSubmissionResult{false, {}, pinned_error(submission.error())};
    }
    const auto outcome = submission.value().command.outcome(impl_->config.command_wait);
    if (!outcome) {
        return TaskSubmissionResult{false, {}, pinned_error(outcome.error())};
    }
    if (outcome.value().status == mira::SettlementStatus::Failed) {
        return TaskSubmissionResult{false,
                                    {},
                                    outcome.value().error
                                        ? pinned_error(*outcome.value().error)
                                        : host_error("pinned_runtime", "task submission failed")};
    }
    return TaskSubmissionResult{true, TaskIdentity{submission.value().id.to_string()}, {}};
}

SessionIdentity MiraHost::primary_session() const {
    return SessionIdentity{impl_->session_id.to_string()};
}

SessionOpenResult MiraHost::open_session() {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return SessionOpenResult{
            false,
            {},
            host_error("invalid_state", std::string("open_session() requires a Running host, "
                                                    "got ") +
                                            host_status_name(current))};
    }
    if (!impl_->environment) {
        return SessionOpenResult{
            false, {}, host_error("invalid_state", "host has no bound environment")};
    }

    const auto session = impl_->runtime.open_session(impl_->environment);
    if (!session) {
        return SessionOpenResult{false, {}, pinned_error(session.error())};
    }
    const auto receipt = session.value().command.receipt(impl_->config.command_wait);
    if (!receipt || receipt.value().status != mira::ReceiptStatus::Accepted) {
        return SessionOpenResult{
            false,
            {},
            receipt ? host_error("pinned_runtime", "session open command was rejected")
                    : pinned_error(receipt.error())};
    }
    return SessionOpenResult{true, SessionIdentity{session.value().id.to_string()}, {}};
}

HostOutcome MiraHost::close_session(const SessionIdentity &session) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return failed(
            host_error("invalid_state", std::string("close_session() requires a Running host, "
                                                    "got ") +
                                            host_status_name(current)));
    }
    const auto session_id = mira::SessionId::parse(session.id);
    if (!session_id || session_id->is_nil()) {
        return failed(host_error("invalid_argument", "malformed session identity"));
    }

    const auto closed = impl_->runtime.close_session(session_id.value());
    if (!closed) {
        return failed(pinned_error(closed.error()));
    }
    const auto outcome = closed.value().outcome(impl_->config.command_wait);
    if (!outcome) {
        return failed(pinned_error(outcome.error()));
    }
    if (outcome.value().status == mira::SettlementStatus::Failed) {
        return failed(outcome.value().error ? pinned_error(*outcome.value().error)
                                            : host_error("pinned_runtime", "session close failed"));
    }
    // Applied and NoOp both mean the session is Closed (the pinned close is
    // idempotent over an already Closed session).
    return HostOutcome{true, {}};
}

SessionViewResult MiraHost::session_view(const SessionIdentity &session) const {
    const auto session_id = mira::SessionId::parse(session.id);
    if (!session_id || session_id->is_nil()) {
        return SessionViewResult{false, SessionView{},
                                 host_error("invalid_argument", "malformed session identity")};
    }
    const auto snapshot = impl_->runtime.session_snapshot(session_id.value());
    if (!snapshot) {
        return SessionViewResult{false, SessionView{}, pinned_error(snapshot.error())};
    }
    SessionView view;
    view.id = session.id;
    view.state = session_state_name(snapshot.value().state);
    view.environment_epoch = snapshot.value().environment_epoch;
    return SessionViewResult{true, std::move(view), {}};
}

HostOutcome MiraHost::cancel_task(const TaskIdentity &task) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return failed(
            host_error("invalid_state", std::string("cancel_task() requires a Running host, got ") +
                                            host_status_name(current)));
    }
    const auto task_id = mira::TaskId::parse(task.id);
    if (!task_id) {
        return failed(host_error("invalid_argument", "malformed task identity"));
    }

    const auto cancelled = impl_->runtime.cancel_task(task_id.value());
    if (!cancelled) {
        return failed(pinned_error(cancelled.error()));
    }
    const auto outcome = cancelled.value().outcome(impl_->config.command_wait);
    if (!outcome) {
        return failed(pinned_error(outcome.error()));
    }
    if (outcome.value().status == mira::SettlementStatus::Failed) {
        return failed(outcome.value().error
                          ? pinned_error(*outcome.value().error)
                          : host_error("pinned_runtime", "task cancellation failed"));
    }
    return HostOutcome{true, {}};
}

HostOutcome MiraHost::pause_family_task(const TaskIdentity &task, bool pause) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return failed(host_error("invalid_state",
                                 std::string(pause ? "pause_task()" : "resume_task()") +
                                     " requires a Running host, got " + host_status_name(current)));
    }
    const auto parsed = mira::TaskId::parse(task.id);
    if (!parsed) {
        return failed(host_error("invalid_argument", "malformed task identity"));
    }

    const auto commanded = pause ? impl_->runtime.pause_task(parsed.value())
                                 : impl_->runtime.resume_task(parsed.value());
    if (!commanded) {
        return failed(pinned_error(commanded.error()));
    }
    const auto outcome = commanded.value().outcome(impl_->config.command_wait);
    if (!outcome) {
        return failed(pinned_error(outcome.error()));
    }
    if (outcome.value().status == mira::SettlementStatus::Failed) {
        return failed(
            outcome.value().error
                ? pinned_error(*outcome.value().error)
                : host_error("pinned_runtime", pause ? "task pause failed" : "task resume failed"));
    }
    return HostOutcome{true, {}};
}

HostOutcome MiraHost::pause_task(const TaskIdentity &task) {
    return pause_family_task(task, /*pause=*/true);
}

HostOutcome MiraHost::resume_task(const TaskIdentity &task) {
    return pause_family_task(task, /*pause=*/false);
}

HostOutcome MiraHost::complete_task(const TaskIdentity &task, bool success,
                                    const std::string &safe_error) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return failed(host_error("invalid_state",
                                 std::string("complete_task() requires a Running host, got ") +
                                     host_status_name(current)));
    }
    const auto task_id = mira::TaskId::parse(task.id);
    if (!task_id) {
        return failed(host_error("invalid_argument", "malformed task identity"));
    }

    mira::TaskOutcome outcome_spec;
    outcome_spec.terminal_state = success ? mira::TaskState::Completed : mira::TaskState::Failed;
    if (!success && !safe_error.empty()) {
        mira::Error error;
        error.code = mira::ErrorCode::Internal;
        error.safe_message = safe_error;
        outcome_spec.error = std::move(error);
    }
    const auto completed = impl_->runtime.complete_task(task_id.value(), outcome_spec);
    if (!completed) {
        return failed(pinned_error(completed.error()));
    }
    const auto outcome = completed.value().outcome(impl_->config.command_wait);
    if (!outcome) {
        return failed(pinned_error(outcome.error()));
    }
    if (outcome.value().status == mira::SettlementStatus::Failed) {
        return failed(outcome.value().error
                          ? pinned_error(*outcome.value().error)
                          : host_error("pinned_runtime", "task completion was rejected"));
    }
    return HostOutcome{true, {}};
}

TaskViewResult MiraHost::task_view(const TaskIdentity &task) const {
    const auto task_id = mira::TaskId::parse(task.id);
    if (!task_id) {
        return TaskViewResult{false, TaskView{},
                              host_error("invalid_argument", "malformed task identity")};
    }
    const auto snapshot = impl_->runtime.task_snapshot(task_id.value());
    if (!snapshot) {
        return TaskViewResult{false, TaskView{}, pinned_error(snapshot.error())};
    }
    TaskView view;
    view.progress = project_task_state(snapshot.value().state);
    if (snapshot.value().terminal_outcome) {
        view.success =
            snapshot.value().terminal_outcome->terminal_state == mira::TaskState::Completed;
    }
    return TaskViewResult{true, view, {}};
}

OperationBeginResult MiraHost::begin_operation(const TaskIdentity &task) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return OperationBeginResult{
            false,
            {},
            host_error("invalid_state",
                       std::string("begin_operation() requires a Running host, got ") +
                           host_status_name(current))};
    }
    const auto task_id = mira::TaskId::parse(task.id);
    if (!task_id) {
        return OperationBeginResult{
            false, {}, host_error("invalid_argument", "malformed task identity")};
    }

    const auto admitted = impl_->runtime.begin_operation(task_id.value(), mira::StepId::generate());
    if (!admitted) {
        return OperationBeginResult{false, {}, pinned_error(admitted.error())};
    }
    const mira::OperationKey &key = admitted.value();
    OperationTicket ticket{task.id, key.task_epoch, key.step_id.to_string(),
                           key.operation_id.to_string()};
    return OperationBeginResult{true, std::move(ticket), {}};
}

HostOutcome MiraHost::admit_operation_completion(const OperationTicket &ticket) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return failed(host_error("invalid_state",
                                 std::string("admit_operation_completion() requires a Running "
                                             "host, got ") +
                                     host_status_name(current)));
    }
    const auto task_id = mira::TaskId::parse(ticket.task_id);
    const auto step_id = mira::StepId::parse(ticket.step_id);
    const auto operation_id = mira::OperationId::parse(ticket.operation_id);
    if (!task_id || !step_id || !operation_id) {
        return failed(host_error("invalid_argument", "malformed operation ticket"));
    }

    const mira::OperationKey key{task_id.value(), ticket.task_epoch, step_id.value(),
                                 operation_id.value()};
    const auto settled = impl_->runtime.admit_operation_completion(key);
    if (!settled) {
        return failed(pinned_error(settled.error()));
    }
    const auto outcome = settled.value().outcome(impl_->config.command_wait);
    if (!outcome) {
        return failed(pinned_error(outcome.error()));
    }
    if (outcome.value().status == mira::SettlementStatus::Failed) {
        return failed(outcome.value().error
                          ? pinned_error(*outcome.value().error)
                          : host_error("pinned_runtime", "operation completion was rejected"));
    }
    // Applied settles the operation; NoOp re-states an already settled
    // ticket, which keeps late completion reports idempotent.
    return HostOutcome{true, {}};
}

HostOutcome MiraHost::attach_workflow_surface(
    kairo::Executor &executor,
    std::shared_ptr<mirage::integration::WorkflowEventBridge> event_bridge,
    std::shared_ptr<mirage::integration::DesktopAtomToolset> atom_toolset,
    std::shared_ptr<mirage::integration::WorkflowLearningMemory> learning) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return failed(host_error("invalid_state",
                                 std::string("attach_workflow_surface() requires a Running host, "
                                             "got ") +
                                     host_status_name(current)));
    }
    if (impl_->workflow_runtime != nullptr) {
        return failed(host_error("invalid_state", "workflow surface is already attached"));
    }
    if (!event_bridge) {
        return failed(host_error("invalid_argument", "event bridge is null"));
    }
    if (!atom_toolset) {
        return failed(host_error("invalid_argument", "atom toolset is null"));
    }
    if (learning && !learning->memory)
        return failed(host_error("invalid_argument", "workflow learning memory is null"));
    if (learning && (learning->agent_subject_id.size() > 128 ||
                     learning->agent_subject_id.find_first_not_of(
                         "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:-") !=
                         std::string::npos))
        return failed(host_error("invalid_argument", "invalid workflow learning subject"));
    if (!impl_->environment) {
        return failed(host_error("invalid_state", "host has no bound environment"));
    }

    // The pinned runtime's event store is the authoritative workflow record
    // (RULE-07); the bridge feeds the service-side broadcast from the same
    // stream, so there is exactly one event source (DEC-023).
    auto runtime = std::make_unique<mira::WorkflowRuntime>(executor, impl_->runtime,
                                                           impl_->session_id, impl_->environment);
    runtime->set_event_store(event_bridge);
    // ToolCall steps dispatch through the desktop atom registry (DEC-024);
    // Strict definitions carrying ToolCall steps are rejected at admission
    // while it is absent, so the registry installs before any run starts.
    impl_->workflow_tools = atom_toolset->registry();
    runtime->set_tool_registry(impl_->workflow_tools);
    if (learning) {
        const mira::MemoryScope scope{
            learning->agent_subject_id.empty() ? mira::MemoryScopeKind::Session
                                               : mira::MemoryScopeKind::Agent,
            learning->agent_subject_id.empty() ? impl_->session_id.to_string()
                                               : learning->agent_subject_id,
            std::nullopt};
        const auto installed = runtime->set_learning_context(learning->memory, scope);
        if (!installed)
            return failed(host_error("pinned_runtime", "workflow learning context rejected"));
    }
    impl_->workflow_runtime = std::move(runtime);
    impl_->workflow_learning = learning != nullptr;
    impl_->workflow_events = std::move(event_bridge);
    impl_->workflow_executor = &executor;
    return HostOutcome{true, {}};
}

WorkflowDefinitionResult MiraHost::save_workflow_definition(const std::string &definition_json,
                                                            const std::string &reason) {
    if (!impl_->workflow_surface_active()) {
        return WorkflowDefinitionResult{
            false,
            {},
            {},
            {},
            {},
            false,
            host_error("invalid_state", "workflow surface is not attached")};
    }
    const auto parsed = mira::parse_workflow_definition(definition_json);
    if (!parsed) {
        return WorkflowDefinitionResult{false, {}, {}, {}, {}, false, pinned_error(parsed.error())};
    }
    const auto appended =
        impl_->workflow_runtime->publish_workflow(parsed.value(), "mirage-service", reason);
    if (!appended) {
        return WorkflowDefinitionResult{
            false, {}, {}, {}, {}, false, pinned_error(appended.error())};
    }
    WorkflowDefinitionResult result;
    result.ok = true;
    result.workflow_id = parsed.value().workflow_id.to_string();
    result.digest = appended.value().to_string();
    result.name = parsed.value().name;
    return result;
}

WorkflowDefinitionResult MiraHost::import_workflow_experience(const std::string &definition_json,
                                                              const std::string &workflow_id,
                                                              const std::string &expected_digest) {
    if (!impl_->workflow_surface_active()) {
        return WorkflowDefinitionResult{
            false,
            {},
            {},
            {},
            {},
            false,
            host_error("invalid_state", "workflow surface is not attached")};
    }
    const auto parsed = mira::parse_workflow_definition(definition_json);
    if (!parsed) {
        return WorkflowDefinitionResult{
            false,
            {},
            {},
            {},
            {},
            false,
            host_error("invalid_argument", "invalid workflow experience definition")};
    }
    if (parsed.value().workflow_id.to_string() != workflow_id ||
        mira::workflow_definition_digest(parsed.value()).to_string() != expected_digest) {
        return WorkflowDefinitionResult{
            false,
            {},
            {},
            {},
            {},
            false,
            host_error("invalid_argument", "workflow experience content identity mismatch")};
    }
    return save_workflow_definition(definition_json, "workflow experience imported draft");
}

WorkflowDefinitionResult MiraHost::publish_workflow_definition(const std::string &definition_json,
                                                               const std::string &reason,
                                                               bool reusable) {
    if (!impl_->workflow_surface_active()) {
        return WorkflowDefinitionResult{
            false,
            {},
            {},
            {},
            {},
            false,
            host_error("invalid_state", "workflow surface is not attached")};
    }
    const auto parsed = mira::parse_workflow_definition(definition_json);
    if (!parsed) {
        return WorkflowDefinitionResult{false, {}, {}, {}, {}, false, pinned_error(parsed.error())};
    }
    const auto workflow_id = parsed.value().workflow_id.to_string();
    std::vector<std::string> callees;
    for (const auto &step : parsed.value().steps) {
        const auto *tool = step.arguments.find("tool");
        if (!tool || !tool->as_string())
            continue;
        const auto &wire = *tool->as_string();
        if (wire.starts_with("workflow.") && wire.size() == 41)
            callees.push_back(wire.substr(9));
    }
    // Check the entire candidate graph before changing any published target.
    std::unordered_map<std::string, unsigned> colors;
    std::function<bool(const std::string &)> cycle = [&](const std::string &id) {
        if (colors[id] == 1)
            return true;
        if (colors[id] == 2)
            return false;
        colors[id] = 1;
        const auto found = impl_->workflow_calls.find(id);
        if (id == workflow_id) {
            for (const auto &next : callees)
                if (cycle(next))
                    return true;
        } else if (found != impl_->workflow_calls.end()) {
            for (const auto &next : found->second->callees)
                if (cycle(next))
                    return true;
        }
        colors[id] = 2;
        return false;
    };
    if (cycle(workflow_id))
        return {false,
                {},
                {},
                {},
                {},
                false,
                host_error("workflow_cycle", "workflow call graph contains a cycle")};
    if (!impl_->workflow_calls.contains(workflow_id) && impl_->workflow_calls.size() >= 128)
        return {false,
                {},
                {},
                {},
                {},
                false,
                host_error("resource_exhausted", "workflow call graph capacity exhausted")};
    // The pinned gate validates, dry-runs on the calling context and appends
    // a DryRunPassed record with content-derived evidence; gate failures
    // leave the library untouched and surface as pinned rejections.
    const auto outcome =
        impl_->workflow_runtime->publish_validated(parsed.value(), "mirage-service", reason);
    if (!outcome) {
        return WorkflowDefinitionResult{
            false, {}, {}, {}, {}, false, pinned_error(outcome.error())};
    }
    // All published versions update an existing call target, even when a CLI
    // caller did not request first-time exposure as a reusable workflow.
    if (reusable || impl_->workflow_calls.contains(workflow_id)) {
        const auto refs = impl_->workflow_runtime->workflow_tool_refs(parsed.value().workflow_id,
                                                                      outcome.value().ir_digest);
        if (!refs)
            return {false, {}, {}, {}, {}, false, pinned_error(refs.error())};
        const auto view = impl_->workflow_tools->exposed_tools();
        const auto internal_name =
            "mirage.call." + workflow_id + "." + outcome.value().ir_digest.to_string();
        const auto descriptor = mira::make_skill_descriptor(parsed.value(), refs.value(), view,
                                                            internal_name, {1, 0, 0});
        if (!descriptor)
            return {false, {}, {}, {}, {}, false, pinned_error(descriptor.error())};
        auto version = std::make_shared<WorkflowCallVersion>();
        mira::WorkflowVersionHistory history{parsed.value().workflow_id, {}};
        mira::WorkflowVersionRecord record;
        record.actor = "mirage-service";
        record.reason = reason;
        record.content_digest = outcome.value().ir_digest;
        record.validation = mira::WorkflowValidationResult::DryRunPassed;
        record.validation_evidence = outcome.value().evidence;
        const auto appended = mira::append_workflow_version(history, record);
        if (!appended)
            return {false, {}, {}, {}, {}, false, pinned_error(appended.error())};
        const auto published = version->skills.publish_skill(descriptor.value(), parsed.value(),
                                                             refs.value(), view, history);
        if (!published)
            return {false, {}, {}, {}, {}, false, pinned_error(published.error())};
        version->skills.seal();
        auto registrations = impl_->workflow_runtime->skill_tool_registrations(version->skills);
        if (registrations.size() != 1)
            return {false,
                    {},
                    {},
                    {},
                    {},
                    false,
                    host_error("invalid_state", "workflow call handler is missing")};
        version->handler = std::move(registrations.front().handler);
        auto found = impl_->workflow_calls.find(workflow_id);
        if (found == impl_->workflow_calls.end()) {
            auto alias = std::make_shared<WorkflowCallAlias>();
            mira::BuiltinToolSpec spec;
            spec.wire_name = "workflow." + workflow_id;
            spec.description = "Call the latest published workflow " + workflow_id;
            // Stable, conservative call surface. Mira binds the current target's
            // exact parameter schema and checks its authority on every call.
            spec.parameters_schema =
                mira::JsonSchema{mira::JsonValue::Object{{"type", "object"},
                                                         {"properties", mira::JsonValue::Object{}},
                                                         {"additionalProperties", true}}};
            spec.has_side_effects = true;
            const auto registered = impl_->workflow_tools->register_tool(
                std::move(spec),
                [alias](const mira::JsonValue &arguments,
                        const mira::OperationContext &context) -> mira::Result<mira::JsonValue> {
                    kairo::comm::Snapshot<WorkflowCallAlias::Version> snapshot;
                    if (!alias->current.try_load(snapshot) || !snapshot.value) {
                        mira::Error error;
                        error.code = mira::ErrorCode::Unavailable;
                        error.safe_message =
                            "workflow call target is unavailable; retry after publication";
                        return error;
                    }
                    const auto completed = snapshot.value->handler(arguments, context);
                    if (!completed)
                        return completed.error();
                    // A stable scalar receipt: true means the current target really
                    // completed. Failure/cancellation never becomes success.
                    return mira::JsonValue{true};
                });
            if (!registered)
                return {false, {}, {}, {}, {}, false, pinned_error(registered.error())};
            found = impl_->workflow_calls.emplace(workflow_id, std::move(alias)).first;
        }
        if (!found->second->current.try_publish(std::move(version)))
            return {
                false,
                {},
                {},
                {},
                {},
                false,
                host_error("unavailable", "workflow call publication is busy; retry explicitly")};
        found->second->callees = std::move(callees);
    }
    WorkflowDefinitionResult result;
    result.ok = true;
    result.workflow_id = parsed.value().workflow_id.to_string();
    result.digest = outcome.value().ir_digest.to_string();
    result.name = parsed.value().name;
    result.dry_run_id = outcome.value().dry_run_id.to_string();
    result.idempotent = outcome.value().idempotent;
    return result;
}

WorkflowExperienceDraftResult
MiraHost::compile_workflow_experience(const std::string &source_run_id,
                                      const std::string &target_workflow_id,
                                      const std::string &name, const std::string &summary) const {
    return compile_workflow_experience_impl({source_run_id}, target_workflow_id, name, summary,
                                            false);
}

WorkflowExperienceDraftResult
MiraHost::induce_workflow_experience(const std::vector<std::string> &source_run_ids,
                                     const std::string &target_workflow_id, const std::string &name,
                                     const std::string &summary) const {
    return compile_workflow_experience_impl(source_run_ids, target_workflow_id, name, summary,
                                            true);
}

WorkflowExperienceDraftResult MiraHost::compile_workflow_experience_impl(
    const std::vector<std::string> &source_run_ids, const std::string &target_workflow_id,
    const std::string &name, const std::string &summary, bool parameterize) const {
    WorkflowExperienceDraftResult result;
    if (!impl_->workflow_surface_active()) {
        result.error = host_error("invalid_state", "workflow surface is not attached");
        return result;
    }
    if (source_run_ids.empty() || source_run_ids.size() > 16 ||
        (parameterize && source_run_ids.size() < 2)) {
        result.error = host_error("invalid_argument", "experience source count is outside limits");
        return result;
    }
    const auto target_id = mira::WorkflowId::parse(target_workflow_id);
    if (!target_id || target_id->is_nil()) {
        result.error = host_error("invalid_argument", "malformed run or target workflow identity");
        return result;
    }
    if (name.size() > mira::kDefaultWorkflowLimits.max_string_bytes ||
        summary.size() > mira::kDefaultWorkflowLimits.max_string_bytes) {
        result.error =
            host_error("invalid_argument", "experience metadata exceeds workflow limits");
        return result;
    }
    std::vector<mira::WorkflowRunId> run_ids;
    for (const auto &source : source_run_ids) {
        const auto run_id = mira::WorkflowRunId::parse(source);
        if (!run_id || run_id->is_nil() ||
            std::find(run_ids.begin(), run_ids.end(), *run_id) != run_ids.end()) {
            result.error =
                host_error("invalid_argument", "malformed or duplicate experience source");
            return result;
        }
        run_ids.push_back(*run_id);
    }
    std::vector<mira::WorkflowTrajectory> trajectories;
    for (const auto &run_id : run_ids) {
        auto captured = impl_->workflow_runtime->capture_trajectory(run_id);
        if (!captured) {
            result.error = pinned_error(captured.error());
            return result;
        }
        if (*target_id == captured.value().workflow_id) {
            result.error =
                host_error("invalid_argument", "experience target must differ from source");
            return result;
        }
        if (!trajectories.empty() &&
            (captured.value().workflow_id != trajectories.front().workflow_id ||
             captured.value().source_digest != trajectories.front().source_digest ||
             captured.value().effective_policy != trajectories.front().effective_policy)) {
            result.error = host_error(
                "invalid_argument", "experience sources must share a workflow version and policy");
            return result;
        }
        trajectories.push_back(std::move(captured.value()));
    }
    const auto &anchor = trajectories.front();
    std::vector<mira::WorkflowParameterCandidate> candidates;
    std::string review_json;
    if (parameterize) {
        auto induced = mira::induce_parameters(trajectories);
        if (!induced) {
            result.error = pinned_error(induced.error());
            return result;
        }
        candidates = std::move(induced.value());
        mira::JsonValue::Array review;
        for (const auto &candidate : candidates) {
            review.emplace_back(mira::JsonValue::Object{
                {"name", candidate.name},
                {"step_index", static_cast<std::int64_t>(candidate.step_index)},
                {"pointer", candidate.pointer},
                {"provenance", mira::workflow_candidate_provenance_name(candidate.provenance)},
                {"observed_values", mira::JsonValue{candidate.observed_values}}});
        }
        review_json = mira::canonical_json_string(mira::JsonValue{std::move(review)});
        if (review_json.size() > 64 * 1024) {
            result.error = host_error("unavailable", "experience parameter review exceeds budget");
            return result;
        }
    }
    mira::WorkflowCompileOptions options;
    options.workflow_id = *target_id;
    options.name = name;
    options.summary = summary;
    const auto compiled = parameterize ? mira::compile_workflow(anchor, options, candidates)
                                       : mira::compile_workflow(anchor, options);
    if (!compiled) {
        result.error = pinned_error(compiled.error());
        return result;
    }
    // The compiler returns a definition, not an executable library version.
    // Keep this path observational: no publish/save, DryRun, memory mutation
    // or event append. Later review and admission remain separate operations.
    result.source_run_id = run_ids.front().to_string();
    result.source_workflow_id = anchor.workflow_id.to_string();
    result.source_digest = anchor.source_digest.to_string();
    result.workflow_id = target_id->to_string();
    result.digest = mira::workflow_definition_digest(compiled.value()).to_string();
    result.definition_json =
        mira::canonical_json_string(mira::workflow_definition_to_json(compiled.value()));
    if (parameterize) {
        for (std::size_t index = 0; index < trajectories.size(); ++index) {
            result.sources.push_back({run_ids[index].to_string(),
                                      trajectories[index].workflow_id.to_string(),
                                      trajectories[index].source_digest.to_string()});
        }
        result.parameter_candidates_json = std::move(review_json);
    }
    result.ok = true;
    return result;
}

WorkflowRunStartResult MiraHost::start_workflow_run(const std::string &workflow_id_hex,
                                                    const std::string &digest_hex,
                                                    const std::string &parameters_json,
                                                    const std::string &policy_name) {
    if (!impl_->workflow_surface_active()) {
        return WorkflowRunStartResult{
            false, {}, host_error("invalid_state", "workflow surface is not attached")};
    }
    const auto workflow_id = mira::WorkflowId::parse(workflow_id_hex);
    if (!workflow_id || workflow_id->is_nil()) {
        return WorkflowRunStartResult{
            false, {}, host_error("invalid_argument", "malformed workflow identity")};
    }
    const auto digest = mira::digest_from_hex(digest_hex);
    if (!digest) {
        return WorkflowRunStartResult{
            false, {}, host_error("invalid_argument", "malformed workflow digest")};
    }
    // Empty means "no parameters"; anything else must decode to an object so
    // the pinned binder sees the caller's input verbatim.
    mira::JsonValue parameters{mira::JsonValue::Object{}};
    if (!parameters_json.empty()) {
        const auto parsed = mira::parse_json(parameters_json);
        if (!parsed || !parsed.value().is_object()) {
            return WorkflowRunStartResult{
                false, {}, host_error("invalid_argument", "malformed run parameters")};
        }
        parameters = std::move(parsed.value());
    }
    std::optional<mira::WorkflowPolicy> policy;
    if (!policy_name.empty()) {
        const auto parsed = mira::parse_workflow_policy(policy_name);
        if (!parsed) {
            return WorkflowRunStartResult{
                false, {}, host_error("invalid_argument", "malformed execution policy")};
        }
        policy = parsed.value();
    }

    // Library path (W-03/W-04): the version is pinned by digest and must be
    // runnable; admission is fail closed on the pinned side.
    const auto created =
        impl_->workflow_runtime->create_run(workflow_id.value(), *digest, parameters, policy);
    if (!created) {
        return WorkflowRunStartResult{false, {}, pinned_error(created.error())};
    }
    const mira::WorkflowRunId run_id = created.value().run_id;
    const auto started = impl_->workflow_runtime->start_run(run_id);
    if (!started) {
        // No Created orphans: a drive admission failure cancels the created
        // run best-effort before the pinned rejection surfaces.
        (void)impl_->workflow_runtime->cancel_run(run_id);
        return WorkflowRunStartResult{false, {}, pinned_error(started.error())};
    }
    return WorkflowRunStartResult{true, run_id.to_string(), {}};
}

WorkflowRunViewResult MiraHost::workflow_run_view(const std::string &run_id_hex) const {
    const auto run_id = mira::WorkflowRunId::parse(run_id_hex);
    if (!run_id || run_id->is_nil()) {
        return WorkflowRunViewResult{
            false, {}, host_error("invalid_argument", "malformed run identity")};
    }
    if (!impl_->workflow_surface_active()) {
        return WorkflowRunViewResult{
            false, {}, host_error("invalid_state", "workflow surface is not attached")};
    }
    const auto snapshot = impl_->workflow_runtime->run_snapshot(run_id.value());
    if (!snapshot) {
        return WorkflowRunViewResult{false, {}, pinned_error(snapshot.error())};
    }
    WorkflowRunView view;
    view.run_id = snapshot.value().run_id.to_string();
    view.workflow_id = snapshot.value().workflow_id.to_string();
    view.state = mira::workflow_run_state_name(snapshot.value().state);
    view.run_epoch = snapshot.value().run_epoch;
    return WorkflowRunViewResult{true, std::move(view), {}};
}

WorkflowRunCancelResult MiraHost::cancel_workflow_run(const std::string &run_id_hex) {
    if (!impl_->workflow_surface_active()) {
        return WorkflowRunCancelResult{
            false, {}, {}, host_error("invalid_state", "workflow surface is not attached")};
    }
    const auto run_id = mira::WorkflowRunId::parse(run_id_hex);
    if (!run_id || run_id->is_nil()) {
        return WorkflowRunCancelResult{
            false, {}, {}, host_error("invalid_argument", "malformed run identity")};
    }
    HostError recovery_error;
    if (impl_->workflow_recovery && !impl_->workflow_recovery->shut_down()) {
        const auto stopped = impl_->workflow_recovery->cancel_recovery(*run_id);
        if (!stopped)
            recovery_error = pinned_error(stopped.error());
    }
    // Idempotent per the pinned contract; terminal runs never revive. Request
    // Run cancellation even if cancelling its model producer reported failure.
    const auto cancelled = impl_->workflow_runtime->cancel_run(run_id.value());
    if (!cancelled) {
        return WorkflowRunCancelResult{false, {}, {}, pinned_error(cancelled.error())};
    }
    if (!recovery_error.code.empty())
        return WorkflowRunCancelResult{false, run_id_hex,
                                       mira::workflow_run_state_name(cancelled.value().state),
                                       recovery_error};
    return WorkflowRunCancelResult{
        true, run_id_hex, mira::workflow_run_state_name(cancelled.value().state), {}};
}

HostOutcome MiraHost::attach_workflow_recovery(
    std::shared_ptr<mirage::integration::WorkflowRecoveryModel> model,
    WorkflowRecoveryOptions options) {
    if (!impl_->workflow_surface_active() || impl_->workflow_recovery)
        return failed(host_error(
            "invalid_state", "recovery requires an active, previously unbound workflow surface"));
    if (!model || !model->gateway || !model->provider || model->profile_id.is_nil() ||
        options.model_call_deadline < std::chrono::milliseconds{1} ||
        options.model_call_deadline > std::chrono::seconds{30} ||
        options.max_decision_repairs > 1 || options.max_tracked_runs == 0 ||
        options.max_tracked_runs > 32 || options.max_output_tokens < 128 ||
        options.max_output_tokens > 8192 ||
        (options.decision_output_mode != "strict_json_schema" &&
         options.decision_output_mode != "json_object" && options.decision_output_mode != "text") ||
        (!options.thinking.empty() && options.thinking != "none" && options.thinking != "adaptive"))
        return failed(host_error("invalid_argument", "invalid recovery model or limits"));
    mira::WorkflowRecoveryConfig config;
    config.profile_id = model->profile_id;
    config.max_attempts_per_run = 1;
    config.model_call_deadline = options.model_call_deadline;
    config.max_decision_repairs = options.max_decision_repairs;
    config.max_tracked_runs = options.max_tracked_runs;
    config.max_concurrent_attempts = 1;
    config.max_output_tokens = options.max_output_tokens;
    config.decision_output_mode = options.decision_output_mode == "text" ? mira::OutputMode::Text
                                  : options.decision_output_mode == "json_object"
                                      ? mira::OutputMode::JsonObject
                                      : mira::OutputMode::StrictJsonSchema;
    if (!options.thinking.empty())
        config.thinking = options.thinking == "none" ? mira::ThinkingMode::Disabled
                                                     : mira::ThinkingMode::Adaptive;
    auto retained = std::make_shared<mirage::integration::WorkflowRecoveryModel>(*model);
    const auto guarded = mirage::integration::guard_workflow_recovery_provider(model->provider);
    if (!guarded)
        return failed(pinned_error(guarded.error()));
    if (guarded.value()->profile().id != model->profile_id)
        return failed(host_error("invalid_argument", "recovery provider profile mismatch"));
    retained->gateway->register_provider(guarded.value());
    auto recovery = std::make_unique<mira::WorkflowRecoveryOrchestrator>(
        *impl_->workflow_executor, *impl_->workflow_runtime, impl_->runtime, *retained->gateway,
        impl_->session_id, config);
    recovery->set_event_store(impl_->workflow_events);
    mira::WorkflowRecoveryHooks hooks;
    hooks.project_parameters = retained->project_parameters;
    recovery->set_hooks(std::move(hooks));
    impl_->recovery_model = std::move(retained);
    impl_->workflow_recovery = std::move(recovery);
    impl_->recovery_drained = false;
    return HostOutcome{true, {}};
}

HostOutcome MiraHost::start_workflow_recovery(const std::string &run_id_hex) {
    if (!impl_->workflow_surface_active() || !impl_->workflow_recovery ||
        impl_->workflow_recovery->shut_down())
        return failed(host_error("invalid_state", "workflow recovery is not active"));
    const auto id = mira::WorkflowRunId::parse(run_id_hex);
    if (!id || id->is_nil())
        return failed(host_error("invalid_argument", "malformed run identity"));
    const auto view = impl_->workflow_runtime->run_snapshot(*id);
    if (!view)
        return failed(pinned_error(view.error()));
    if (view.value().state != mira::WorkflowRunState::WaitingAgent)
        return failed(host_error("invalid_state", "workflow run is not waiting for Agent"));
    {
        // WaitingAgent precedes carrier epoch settlement and optional lesson
        // retrieval. Consume the drive before taking a recovery snapshot.
        const auto driven = impl_->workflow_runtime->wait_run(*id, std::chrono::milliseconds{1});
        if (!driven) {
            if (driven.error().code == mira::ErrorCode::DeadlineExceeded)
                return failed(host_error("invalid_state", "workflow drive is still settling"));
            return failed(pinned_error(driven.error()));
        }
    }
    // MIRA-20261112-003: the cache can hide a later budget-rejected result.
    // Keep this preview single-attempt without parallel attempt accounting.
    const auto previous =
        impl_->workflow_recovery->wait_recovery(*id, std::chrono::milliseconds{1});
    if (previous)
        return failed(
            host_error("invalid_state", "workflow run already has a settled recovery attempt"));
    if (previous.error().code != mira::ErrorCode::NotFound &&
        previous.error().code != mira::ErrorCode::DeadlineExceeded)
        return failed(pinned_error(previous.error()));
    const auto started = impl_->workflow_recovery->start_recovery(*id);
    return started ? HostOutcome{true, {}} : failed(pinned_error(started.error()));
}

WorkflowRecoveryPollResult MiraHost::poll_workflow_recovery(const std::string &run_id_hex,
                                                            std::uint32_t after_ordinal) {
    WorkflowRecoveryPollResult result;
    const auto id = mira::WorkflowRunId::parse(run_id_hex);
    if (!id || id->is_nil()) {
        result.error = host_error("invalid_argument", "malformed run identity");
        return result;
    }
    if (!impl_->workflow_recovery) {
        result.error = host_error("invalid_state", "workflow recovery is not attached");
        return result;
    }
    const auto attempt = impl_->workflow_recovery->wait_recovery(*id, std::chrono::milliseconds{1});
    if (!attempt) {
        if (attempt.error().code == mira::ErrorCode::DeadlineExceeded) {
            result.ok = true;
            return result;
        }
        result.error = pinned_error(attempt.error());
        return result;
    }
    const auto &a = attempt.value();
    if (a.ordinal <= after_ordinal) {
        result.ok = true;
        return result;
    }
    result.ok = result.settled = true;
    result.attempt = {a.run_id.to_string(),
                      a.workflow_id.to_string(),
                      mira::workflow_recovery_outcome_name(a.outcome),
                      a.reason_code,
                      a.ordinal,
                      a.decision_digest ? a.decision_digest->to_string() : "",
                      a.patch_id ? a.patch_id->to_string() : "",
                      a.model_request_id ? a.model_request_id->to_string() : ""};
    return result;
}

HostOutcome MiraHost::cancel_workflow_recovery(const std::string &run_id_hex) {
    const auto id = mira::WorkflowRunId::parse(run_id_hex);
    if (!id || id->is_nil())
        return failed(host_error("invalid_argument", "malformed run identity"));
    if (!impl_->workflow_recovery)
        return failed(host_error("invalid_state", "workflow recovery is not attached"));
    if (impl_->workflow_recovery->shut_down())
        return HostOutcome{true, {}};
    const auto cancelled = impl_->workflow_recovery->cancel_recovery(*id);
    return cancelled ? HostOutcome{true, {}} : failed(pinned_error(cancelled.error()));
}

HostOutcome MiraHost::shutdown_workflow_recovery() {
    if (!impl_->workflow_recovery)
        return HostOutcome{true, {}};
    const auto report = impl_->workflow_recovery->shutdown();
    impl_->recovery_drained = report.clean;
    if (!report.clean || report.event_emit_failures || report.hook_failures)
        return failed(host_error("pinned_runtime", "workflow recovery drain or audit failed"));
    return HostOutcome{true, {}};
}

WorkflowRecoveryLessonResult
MiraHost::record_workflow_recovery_lesson(const std::string &run_id_hex) {
    if (!impl_->workflow_surface_active())
        return {false, {}, {}, {}, host_error("invalid_state", "workflow surface is not active")};
    const auto id = mira::WorkflowRunId::parse(run_id_hex);
    if (!id || id->is_nil())
        return {false, {}, {}, {}, host_error("invalid_argument", "malformed run identity")};
    if (impl_->workflow_learning) {
        const auto driven = impl_->workflow_runtime->wait_run(*id, std::chrono::milliseconds{1});
        if (!driven && driven.error().code == mira::ErrorCode::DeadlineExceeded)
            return {
                false, {}, {}, {}, host_error("invalid_state", "workflow drive is still settling")};
        if (!driven)
            return {false,
                    {},
                    {},
                    {},
                    host_error("pinned_runtime", "workflow learning run unavailable")};
    }
    const auto lesson = impl_->workflow_runtime->record_recovery_lesson(*id);
    if (!lesson)
        return {false,
                {},
                {},
                {},
                host_error("pinned_runtime", std::string(error_code_name(lesson.error().code)) +
                                                 ": workflow learning operation failed")};
    return {true,
            run_id_hex,
            lesson.value().lesson_id,
            mira::recovery_lesson_digest(lesson.value()).to_string(),
            {}};
}

HostOutcome MiraHost::shutdown_workflow_surface() {
    const auto recovery_down = shutdown_workflow_recovery();
    if (!recovery_down.ok && !impl_->recovery_drained)
        return recovery_down;
    if (impl_->workflow_runtime == nullptr) {
        return recovery_down;
    }
    if (impl_->workflow_runtime->shut_down()) {
        return recovery_down;
    }
    // Pinned order for this call: stop producers, cancel active runs through
    // the control plane, drain drive futures within the bounded budget. Runs
    // on the service's owning (non-worker) thread.
    const mira::WorkflowShutdownReport report = impl_->workflow_runtime->shutdown();
    if (!report.clean) {
        return failed(host_error("pinned_runtime",
                                 "workflow surface shutdown was not clean: " + report.diagnostic));
    }
    return recovery_down;
}

ShutdownResult MiraHost::shutdown() {
    const HostStatus current = impl_->status.load();
    if (current == HostStatus::Stopped) {
        // Idempotent: report the recorded result. A host that never ran is
        // trivially clean.
        HostShutdownReport report = impl_->last_shutdown_report;
        if (!impl_->runtime_initialized) {
            report.clean = true;
        }
        return ShutdownResult{true, report, {}};
    }
    // The workflow surface converges before the runtime it rides on (pinned
    // order: WorkflowRuntime shutdown → MiraRuntime stop; DEC-023).
    const auto workflow_down = shutdown_workflow_surface();
    if (!workflow_down.ok && !impl_->recovery_drained) {
        impl_->status.store(HostStatus::Failed);
        return ShutdownResult{false, {}, workflow_down.error};
    }
    if (current == HostStatus::Failed) {
        // Failed is terminal and never revived; release pinned resources.
        impl_->release_pinned_runtime();
        if (!workflow_down.ok) {
            impl_->last_shutdown_report.clean = false;
            impl_->last_shutdown_report.diagnostic = workflow_down.error.message;
        }
        return ShutdownResult{true, impl_->last_shutdown_report, {}};
    }
    if (current != HostStatus::Running) {
        return ShutdownResult{
            false,
            {},
            host_error("invalid_state", std::string("shutdown() requires a Running host, got ") +
                                            host_status_name(current))};
    }

    impl_->status.store(HostStatus::Stopping);
    HostShutdownReport report;
    if (const auto stop = impl_->runtime.request_shutdown()) {
        const auto outcome = stop.value().outcome(impl_->config.shutdown_drain);
        if (outcome && outcome.value().status == mira::SettlementStatus::Failed) {
            report.diagnostic = "shutdown request did not settle";
        }
    } else {
        report.diagnostic = "shutdown request was not admitted";
    }

    const mira::ShutdownReport pinned = impl_->runtime.finish_shutdown();
    report.clean = pinned.clean && workflow_down.ok;
    if (!workflow_down.ok)
        report.diagnostic = workflow_down.error.message;
    report.pending_commands = pinned.pending_commands;
    if (report.diagnostic.empty() && !pinned.diagnostic.empty()) {
        report.diagnostic = pinned.diagnostic;
    }
    impl_->runtime_initialized = false;
    impl_->last_shutdown_report = report;
    impl_->status.store(report.clean ? HostStatus::Stopped : HostStatus::Failed);
    return ShutdownResult{true, report, {}};
}

} // namespace mirage::runtime
