#include <mirage/runtime/mira_host.hpp>

#include <mira/core_contracts.hpp>
#include <mira/runtime.hpp>
#include <mira/version.hpp>

#include <atomic>
#include <utility>

namespace mirage::runtime {

MiraCoreVersion mira_core_version() {
    return {mira::kVersion.major, mira::kVersion.minor, mira::kVersion.patch};
}

std::string mira_core_version_string() {
    const MiraCoreVersion version = mira_core_version();
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "."
           + std::to_string(version.patch);
}

bool mira_core_compatible_with(int expected_major, int expected_minor) {
    const MiraCoreVersion version = mira_core_version();
    return version.major == expected_major && version.minor == expected_minor;
}

namespace {

const char* error_code_name(mira::ErrorCode code) {
    switch (code) {
    case mira::ErrorCode::Cancelled: return "cancelled";
    case mira::ErrorCode::DeadlineExceeded: return "deadline_exceeded";
    case mira::ErrorCode::ResourceExhausted: return "resource_exhausted";
    case mira::ErrorCode::Unavailable: return "unavailable";
    case mira::ErrorCode::PermissionDenied: return "permission_denied";
    case mira::ErrorCode::InvalidArgument: return "invalid_argument";
    case mira::ErrorCode::InvalidState: return "invalid_state";
    case mira::ErrorCode::NotFound: return "not_found";
    case mira::ErrorCode::AlreadyExists: return "already_exists";
    case mira::ErrorCode::UnsupportedCapability: return "unsupported_capability";
    case mira::ErrorCode::UnsupportedVersion: return "unsupported_version";
    case mira::ErrorCode::InvalidObservation: return "invalid_observation";
    case mira::ErrorCode::StaleObservation: return "stale_observation";
    case mira::ErrorCode::InvalidModelOutput: return "invalid_model_output";
    case mira::ErrorCode::ContextOverflow: return "context_overflow";
    case mira::ErrorCode::SafetyRejected: return "safety_rejected";
    case mira::ErrorCode::ConfirmationRequired: return "confirmation_required";
    case mira::ErrorCode::ExecutionUncertain: return "execution_uncertain";
    case mira::ErrorCode::DataLoss: return "data_loss";
    case mira::ErrorCode::PlatformError: return "platform_error";
    case mira::ErrorCode::Internal: return "internal";
    }
    return "unknown";
}

HostError pinned_error(const mira::Error& error) {
    std::string message = error.safe_message.empty() ? "pinned runtime rejected the request"
                                                     : error.safe_message;
    return HostError{"pinned_runtime",
                     std::string(error_code_name(error.code)) + ": " + std::move(message)};
}

HostError host_error(std::string code, std::string message) {
    return HostError{std::move(code), std::move(message)};
}

HostOutcome failed(HostError error) {
    return HostOutcome{false, std::move(error)};
}

TaskProgress project_task_state(mira::TaskState state) {
    using mira::TaskState;
    switch (state) {
    case TaskState::Idle: return TaskProgress::Idle;
    case TaskState::Observing:
    case TaskState::Reasoning:
    case TaskState::Planning:
    case TaskState::Acting:
    case TaskState::Verifying:
    case TaskState::Recovering: return TaskProgress::Active;
    case TaskState::Pausing:
    case TaskState::Paused:
    case TaskState::TakeoverSettling:
    case TaskState::SuspendedForTakeover: return TaskProgress::Paused;
    case TaskState::Cancelling: return TaskProgress::Cancelling;
    case TaskState::Completed: return TaskProgress::Completed;
    case TaskState::Failed: return TaskProgress::Failed;
    case TaskState::Cancelled: return TaskProgress::Cancelled;
    }
    return TaskProgress::Unknown;
}

} // namespace

const char* host_status_name(HostStatus status) {
    switch (status) {
    case HostStatus::Stopped: return "stopped";
    case HostStatus::Starting: return "starting";
    case HostStatus::Running: return "running";
    case HostStatus::Stopping: return "stopping";
    case HostStatus::Failed: return "failed";
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
    bool runtime_initialized = false;
    HostShutdownReport last_shutdown_report;
};

MiraHost::MiraHost(HostConfig config) : impl_(std::make_unique<Impl>(config)) {}

MiraHost::~MiraHost() {
    if (impl_) {
        impl_->release_pinned_runtime();
    }
}

HostStatus MiraHost::status() const {
    return impl_->status.load();
}

HostOutcome MiraHost::start(
    std::shared_ptr<mirage::integration::DesktopEnvironmentBinding> binding) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Stopped) {
        return failed(host_error("invalid_state",
                                 std::string("start() requires a Stopped host, got ") +
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

TaskSubmissionResult MiraHost::submit_task(const std::string& goal) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return TaskSubmissionResult{
            false, {},
            host_error("invalid_state",
                       std::string("submit_task() requires a Running host, got ") +
                           host_status_name(current))};
    }
    if (goal.empty()) {
        return TaskSubmissionResult{
            false, {}, host_error("invalid_argument", "goal must not be empty")};
    }

    const auto submission = impl_->runtime.submit_task(impl_->session_id, mira::TaskSpec{goal});
    if (!submission) {
        return TaskSubmissionResult{false, {}, pinned_error(submission.error())};
    }
    const auto outcome = submission.value().command.outcome(impl_->config.command_wait);
    if (!outcome) {
        return TaskSubmissionResult{false, {}, pinned_error(outcome.error())};
    }
    if (outcome.value().status == mira::SettlementStatus::Failed) {
        return TaskSubmissionResult{
            false, {},
            outcome.value().error ? pinned_error(*outcome.value().error)
                                  : host_error("pinned_runtime", "task submission failed")};
    }
    return TaskSubmissionResult{true, TaskIdentity{submission.value().id.to_string()}, {}};
}

HostOutcome MiraHost::cancel_task(const TaskIdentity& task) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return failed(host_error("invalid_state",
                                 std::string("cancel_task() requires a Running host, got ") +
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
        return failed(outcome.value().error ? pinned_error(*outcome.value().error)
                                            : host_error("pinned_runtime",
                                                         "task cancellation failed"));
    }
    return HostOutcome{true, {}};
}

HostOutcome MiraHost::complete_task(const TaskIdentity& task, bool success,
                                    const std::string& safe_error) {
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
        return failed(outcome.value().error ? pinned_error(*outcome.value().error)
                                            : host_error("pinned_runtime",
                                                         "task completion was rejected"));
    }
    return HostOutcome{true, {}};
}

TaskViewResult MiraHost::task_view(const TaskIdentity& task) const {
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

OperationBeginResult MiraHost::begin_operation(const TaskIdentity& task) {
    const HostStatus current = impl_->status.load();
    if (current != HostStatus::Running) {
        return OperationBeginResult{
            false, {},
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
    const mira::OperationKey& key = admitted.value();
    OperationTicket ticket{task.id, key.task_epoch, key.step_id.to_string(),
                           key.operation_id.to_string()};
    return OperationBeginResult{true, std::move(ticket), {}};
}

HostOutcome MiraHost::admit_operation_completion(const OperationTicket& ticket) {
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
        return failed(outcome.value().error ? pinned_error(*outcome.value().error)
                                            : host_error("pinned_runtime",
                                                         "operation completion was rejected"));
    }
    // Applied settles the operation; NoOp re-states an already settled
    // ticket, which keeps late completion reports idempotent.
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
    if (current == HostStatus::Failed) {
        // Failed is terminal and never revived; release pinned resources.
        impl_->release_pinned_runtime();
        return ShutdownResult{true, impl_->last_shutdown_report, {}};
    }
    if (current != HostStatus::Running) {
        return ShutdownResult{
            false, {},
            host_error("invalid_state",
                       std::string("shutdown() requires a Running host, got ") +
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
