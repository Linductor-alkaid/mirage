#include <mirage/runtime/mira_host.hpp>

#include <mira/core_contracts.hpp>
#include <mira/runtime.hpp>
#include <mira/version.hpp>
#include <mira/workflow_ir.hpp>
#include <mira/workflow_runtime.hpp>

#include <atomic>
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
    std::unique_ptr<mira::WorkflowRuntime> workflow_runtime;
    std::shared_ptr<mirage::integration::WorkflowEventBridge> workflow_events;

    /// True when the surface is attached and still accepting work; a shut
    /// down surface rejects every call with the stable invalid_state shape.
    [[nodiscard]] bool workflow_surface_active() const {
        return workflow_runtime != nullptr && !workflow_runtime->shut_down();
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

    const auto submission = impl_->runtime.submit_task(session_id.value(), mira::TaskSpec{goal});
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
    executor::Executor &executor,
    std::shared_ptr<mirage::integration::WorkflowEventBridge> event_bridge,
    std::shared_ptr<mirage::integration::DesktopAtomToolset> atom_toolset) {
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
    runtime->set_tool_registry(atom_toolset->registry());
    impl_->workflow_runtime = std::move(runtime);
    impl_->workflow_events = std::move(event_bridge);
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

WorkflowDefinitionResult MiraHost::publish_workflow_definition(const std::string &definition_json,
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
    // The pinned gate validates, dry-runs on the calling context and appends
    // a DryRunPassed record with content-derived evidence; gate failures
    // leave the library untouched and surface as pinned rejections.
    const auto outcome =
        impl_->workflow_runtime->publish_validated(parsed.value(), "mirage-service", reason);
    if (!outcome) {
        return WorkflowDefinitionResult{
            false, {}, {}, {}, {}, false, pinned_error(outcome.error())};
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
    // Idempotent per the pinned contract; terminal runs never revive.
    const auto cancelled = impl_->workflow_runtime->cancel_run(run_id.value());
    if (!cancelled) {
        return WorkflowRunCancelResult{false, {}, {}, pinned_error(cancelled.error())};
    }
    return WorkflowRunCancelResult{
        true, run_id_hex, mira::workflow_run_state_name(cancelled.value().state), {}};
}

HostOutcome MiraHost::shutdown_workflow_surface() {
    if (impl_->workflow_runtime == nullptr) {
        return HostOutcome{true, {}};
    }
    if (impl_->workflow_runtime->shut_down()) {
        return HostOutcome{true, {}};
    }
    // Pinned order for this call: stop producers, cancel active runs through
    // the control plane, drain drive futures within the bounded budget. Runs
    // on the service's owning (non-worker) thread.
    const mira::WorkflowShutdownReport report = impl_->workflow_runtime->shutdown();
    if (!report.clean) {
        return failed(host_error("pinned_runtime",
                                 "workflow surface shutdown was not clean: " + report.diagnostic));
    }
    return HostOutcome{true, {}};
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
    (void)shutdown_workflow_surface();
    if (current == HostStatus::Failed) {
        // Failed is terminal and never revived; release pinned resources.
        impl_->release_pinned_runtime();
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
    report.clean = pinned.clean;
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
