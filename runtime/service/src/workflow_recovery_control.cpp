#include "workflow_recovery_control.hpp"

namespace mirage::runtime::detail {
namespace {
ipc::Response failure(const HostError &error) {
    ipc::Response response;
    response.error = {error.code, error.message};
    return response;
}
void release(ServiceCore &core) {
    core.model_layer->release_workflow_recovery();
    core.recovering_workflow_run.clear();
}
class PendingReservation {
  public:
    explicit PendingReservation(ServiceCore &core) : core_(core) {}
    ~PendingReservation() {
        if (!admitted_)
            release(core_);
    }
    void admitted() { admitted_ = true; }

  private:
    ServiceCore &core_;
    bool admitted_ = false;
};
} // namespace

HostOutcome refresh_workflow_recovery(ServiceCore &core) {
    if (core.recovering_workflow_run.empty())
        return {true, {}};
    const auto result = core.host.poll_workflow_recovery(core.recovering_workflow_run);
    if (!result.ok)
        return {false, result.error};
    if (result.settled)
        release(core);
    return {true, {}};
}

ipc::Response control_workflow_recovery(ServiceCore &core,
                                        const ipc::WorkflowRecoveryRequest &request) {
    {
        std::lock_guard lock(core.workflow_runs.mutex);
        if (!core.workflow_runs.runs.contains(request.run_id))
            return failure({"not_found", "unknown workflow run"});
    }
    const auto refreshed = refresh_workflow_recovery(core);
    if (!refreshed.ok)
        return failure(refreshed.error);
    ipc::WorkflowRecoveryStatus status;
    status.run_id = request.run_id;
    status.action = request.action;
    if (request.action == "start") {
        if (!core.model_layer || !core.model_layer->running())
            return failure({"unavailable", "model layer is not configured"});
        if (!core.recovering_workflow_run.empty())
            return failure({"unavailable", "workflow recovery is still settling"});
        const auto view = core.host.workflow_run_view(request.run_id);
        if (!view.ok)
            return failure(view.error);
        if (view.view.state != "waiting_agent")
            return failure({"invalid_state", "workflow run is not waiting for Agent"});
        {
            std::lock_guard lock(core.dialogs.mutex);
            for (const auto &[id, log] : core.dialogs.sessions) {
                (void)id;
                if (!log.in_flight_turn_id.empty())
                    return failure({"invalid_state", "stop active turns before workflow recovery"});
            }
        }
        if (!core.model_layer->reserve_workflow_recovery())
            return failure({"unavailable", "model inference is busy"});
        PendingReservation reservation(core);
        core.recovering_workflow_run = request.run_id; // allocate before asynchronous admission
        if (!core.workflow_recovery_bound) {
            WorkflowRecoveryOptions options;
            options.decision_output_mode = "text";
            options.thinking = "none";
            options.model_call_deadline = std::chrono::seconds{30};
            const auto attached = core.host.attach_workflow_recovery(
                core.model_layer->workflow_recovery_model(), options);
            if (!attached.ok)
                return failure(attached.error);
            core.workflow_recovery_bound = true;
        }
        const auto started = core.host.start_workflow_recovery(request.run_id);
        if (!started.ok)
            return failure(started.error);
        reservation.admitted();
    } else if (request.action == "status") {
        const auto polled = core.host.poll_workflow_recovery(request.run_id);
        if (!polled.ok)
            return failure(polled.error);
        if (polled.settled) {
            if (core.recovering_workflow_run == request.run_id)
                release(core);
            const auto &attempt = polled.attempt;
            status.settled = true;
            status.ordinal = attempt.ordinal;
            status.workflow_id = attempt.workflow_id;
            status.outcome = attempt.outcome;
            status.reason_code = attempt.reason_code;
            status.decision_digest = attempt.decision_digest;
            status.patch_id = attempt.patch_id;
            status.model_request_id = attempt.model_request_id;
        }
    } else if (request.action == "cancel") {
        const auto cancelled = core.host.cancel_workflow_recovery(request.run_id);
        if (!cancelled.ok)
            return failure(cancelled.error);
    } else if (request.action == "lesson") {
        const auto lesson = core.host.record_workflow_recovery_lesson(request.run_id);
        if (!lesson.ok)
            return failure(lesson.error);
        status.lesson_id = lesson.lesson_id;
        status.lesson_digest = lesson.digest;
    } else {
        return failure({"invalid_argument", "unknown workflow recovery action"});
    }
    ipc::Response result;
    result.ok = true;
    result.payload = std::move(status);
    return result;
}
} // namespace mirage::runtime::detail
