#include "task_driver.hpp"

#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/permission/permission.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <utility>

namespace mirage::runtime::detail {

const char *progress_name(TaskProgress progress) {
    switch (progress) {
    case TaskProgress::Idle:
        return "Idle";
    case TaskProgress::Active:
        return "Active";
    case TaskProgress::Paused:
        return "Paused";
    case TaskProgress::Cancelling:
        return "Cancelling";
    case TaskProgress::Completed:
        return "Completed";
    case TaskProgress::Failed:
        return "Failed";
    case TaskProgress::Cancelled:
        return "Cancelled";
    case TaskProgress::Unknown:
        break;
    }
    return "Unknown";
}

namespace {

TaskIdentity identity_of(const std::string &task_id) {
    TaskIdentity identity;
    identity.id = task_id;
    return identity;
}

bool terminal_progress(TaskProgress progress) {
    return progress == TaskProgress::Completed || progress == TaskProgress::Failed ||
           progress == TaskProgress::Cancelled;
}

/// The permission capability a scripted step exercises (DEC-010). The M1
/// step set cannot express filesystem.write, so that capability is judged
/// by the framework and tests, not by this driver.
mirage::runtime::permission::Capability capability_of(ipc::StepKind kind) {
    switch (kind) {
    case ipc::StepKind::FilesystemRead:
        return mirage::runtime::permission::Capability::FilesystemRead;
    case ipc::StepKind::ProcessExecute:
        return mirage::runtime::permission::Capability::ProcessExecute;
    }
    return mirage::runtime::permission::Capability::FilesystemRead;
}

/// Serialized host operation: every MiraHost call from the driver goes onto
/// the service's single serial context (the host's one-owner discipline).
template <typename F>
auto post_host(ServiceCore &core, F &&operation)
    -> std::future<typename std::invoke_result<F>::type> {
    return core.executor.submit_on(core.serial, std::forward<F>(operation));
}

template <typename T> bool ready_within(std::future<T> &future, std::chrono::milliseconds budget) {
    return future.valid() && future.wait_for(budget) == std::future_status::ready;
}

/// Consumes a settled best-effort future, swallowing its exception: the
/// observable task state lives in the pinned runtime, and settlement
/// failures (for example, cancelling an already cancelled task) must not
/// kill the driver.
template <typename T> void consume(std::future<T> future) {
    try {
        if (future.valid()) {
            (void)future.get();
        }
    } catch (const std::exception &) {
    } catch (...) {
    }
}

void for_each_step(ServiceCore &core, const std::string &task_id, std::size_t begin,
                   const std::function<void(StepRecord &)> &mutate) {
    std::lock_guard lock(core.registry.mutex);
    auto entry = core.registry.tasks.find(task_id);
    if (entry == core.registry.tasks.end()) {
        return;
    }
    auto &steps = entry->second.steps;
    for (std::size_t index = begin; index < steps.size(); ++index) {
        mutate(steps[index]);
    }
}

void mark_step(ServiceCore &core, const std::string &task_id, std::size_t index, const char *status,
               const std::string &operation_id, const char *permission, bool ok, int exit_code,
               std::string result, bool truncated, std::string error) {
    std::lock_guard lock(core.registry.mutex);
    auto entry = core.registry.tasks.find(task_id);
    if (entry == core.registry.tasks.end() || index >= entry->second.steps.size()) {
        return;
    }
    StepRecord &step = entry->second.steps[index];
    step.status = status;
    step.operation_id = operation_id;
    step.permission = permission;
    step.ok = ok;
    step.exit_code = exit_code;
    step.result = std::move(result);
    step.result_truncated = truncated;
    step.error = std::move(error);
}

/// Records only the permission outcome of a step that has not been judged
/// executable yet (the deny path in run_driver); everything else stays as
/// the pending state the registry was seeded with.
void mark_step_permission(ServiceCore &core, const std::string &task_id, std::size_t index,
                          const char *permission) {
    std::lock_guard lock(core.registry.mutex);
    auto entry = core.registry.tasks.find(task_id);
    if (entry == core.registry.tasks.end() || index >= entry->second.steps.size()) {
        return;
    }
    entry->second.steps[index].permission = permission;
}

/// Records the terminal settlement of the driver (M1-07): the terminal
/// progress name feeds the recovery snapshot and the task list/inspect
/// views after a restart, and settled tasks trigger the recovery persist
/// hook. The pinned runtime's view is the authoritative terminal name when
/// reachable (a concurrent cancel can race a failure settlement);
/// `intended` covers the unreachable-pinned fallback. `has_success` stays
/// true only when the pinned runtime accepted the settlement. The terminal
/// `task.updated` event publishes from here, so every settlement path
/// (complete, failed, cancelled, exception) emits exactly one.
void mark_driver_done(const std::shared_ptr<ServiceCore> &core, const std::string &task_id,
                      const char *intended, bool has_success, bool success) {
    ServiceCore &service = *core;
    const char *progress = intended;
    // The lambdas posted here can outlive this stack frame: when
    // ready_within gives up, the closure stays queued on the serial context
    // and runs later. Every stack string is therefore captured by value.
    auto view = post_host(
        service, [&service, task_id] { return service.host.task_view(identity_of(task_id)); });
    if (ready_within(view, service.command_wait)) {
        try {
            const TaskViewResult result = view.get();
            if (result.ok) {
                switch (result.view.progress) {
                case TaskProgress::Completed:
                    progress = "Completed";
                    break;
                case TaskProgress::Failed:
                    progress = "Failed";
                    break;
                case TaskProgress::Cancelled:
                    progress = "Cancelled";
                    break;
                default:
                    break;
                }
            }
        } catch (const std::exception &) {
        }
    }
    {
        std::lock_guard lock(service.registry.mutex);
        auto entry = service.registry.tasks.find(task_id);
        if (entry != service.registry.tasks.end()) {
            entry->second.driver_done = true;
            entry->second.final_progress = progress;
            entry->second.has_success = has_success;
            entry->second.success = success;
        }
    }
    service.recovery.persist(service.registry);
    publish_task_updated_best_effort(core, task_id);
}

/// Admits one desktop operation for the task; false means the pinned
/// runtime refused (unknown identity, cancelled or settled era), which ends
/// the driving loop without marking the task failed from here.
bool begin_operation(ServiceCore &core, const std::string &task_id, OperationTicket &ticket) {
    auto begin = post_host(
        core, [&core, task_id] { return core.host.begin_operation(identity_of(task_id)); });
    if (!ready_within(begin, core.command_wait)) {
        return false;
    }
    try {
        OperationBeginResult result = begin.get();
        if (!result.ok) {
            return false;
        }
        ticket = result.ticket;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

/// Settles the task failed through the pinned runtime (fail-fast, DEC-007
/// item 5); has_success only when the pinned runtime accepted the
/// settlement, so a concurrent cancel keeps the terminal say.
void settle_failed(const std::shared_ptr<ServiceCore> &core, const std::string &task_id,
                   const std::string &error) {
    ServiceCore &service = *core;
    auto complete = post_host(service, [&service, task_id, error] {
        return service.host.complete_task(identity_of(task_id), false, error);
    });
    const bool settled = ready_within(complete, service.command_wait) && [&] {
        try {
            return complete.get().ok;
        } catch (const std::exception &) {
            return false;
        }
    }();
    mark_driver_done(core, task_id, "Failed", settled, false);
}

void admit_completion(ServiceCore &core, const OperationTicket &ticket) {
    // Idempotent by pinned contract (stale/late tickets settle as NoOps), so
    // a timeout or rejection here never escalates into a task failure.
    consume(
        post_host(core, [&core, ticket] { return core.host.admit_operation_completion(ticket); }));
}

/// Executes one step's desktop action on the caller (driver) thread; the
/// provider is synchronous and bounded by its own budget, and `cancel`
/// ends an in-flight action cooperatively (M1-05 cancellation path).
/// `cancelled` reports a cancellation (distinct from a plain step failure,
/// which settles the task failed).
void execute_action(ServiceCore &core, const ipc::TaskStep &step,
                    const mirage::desktop::CancelToken &cancel,
                    std::chrono::milliseconds step_budget, bool &ok, bool &cancelled,
                    int &exit_code, std::string &result, bool &truncated, std::string &error) {
    auto *environment = core.environment.get();
    if (environment == nullptr) {
        error = "internal: no desktop environment bound";
        return;
    }
    if (step.kind == ipc::StepKind::FilesystemRead) {
        auto *filesystem = environment->filesystem();
        if (filesystem == nullptr) {
            error = "filesystem provider unavailable";
            return;
        }
        desktop::FileReadLimits limits;
        const auto outcome = filesystem->read_text_file(step.argument, limits, cancel);
        if (!outcome.ok) {
            cancelled = outcome.error.code == "cancelled";
            error = outcome.error.code + ": " + outcome.error.message;
            return;
        }
        ok = true;
        result = outcome.content;
        if (result.size() > core.max_result_bytes) {
            result.resize(core.max_result_bytes);
            truncated = true;
        }
        return;
    }
    auto *process = environment->process();
    if (process == nullptr) {
        error = "process provider unavailable";
        return;
    }
    desktop::ProcessLimits limits;
    limits.timeout = step_budget;
    const auto outcome = process->execute(step.argument, limits, cancel);
    exit_code = outcome.exit_code;
    if (outcome.output_truncated) {
        truncated = true;
    }
    if (outcome.cancelled) {
        cancelled = true;
        error = outcome.error.code + ": " + outcome.error.message;
        return;
    }
    if (!outcome.ok) {
        error = outcome.error.code + ": " + outcome.error.message;
        return;
    }
    result = outcome.standard_output;
    if (!outcome.standard_error.empty()) {
        if (!result.empty()) {
            result += "\n";
        }
        result += "[stderr] " + outcome.standard_error;
    }
    if (result.size() > core.max_result_bytes) {
        result.resize(core.max_result_bytes);
        truncated = true;
    }
    if (!outcome.exited_normally || outcome.exit_code != 0) {
        error = "process reported failure (exit code " + std::to_string(outcome.exit_code) + ")";
        return;
    }
    ok = true;
}

} // namespace

void publish_task_updated(const std::shared_ptr<ServiceCore> &core, const std::string &task_id) {
    ServiceCore &service = *core;
    ipc::TaskUpdatedEvent event;
    event.task_id = task_id;
    bool settled = false;
    {
        std::lock_guard lock(service.registry.mutex);
        auto entry = service.registry.tasks.find(task_id);
        if (entry == service.registry.tasks.end()) {
            return; // the task record is gone (shutdown); nothing to report
        }
        event.goal = entry->second.goal;
        if (!entry->second.final_progress.empty()) {
            // Settled: the recorded terminal name and outcome are the
            // truth, exactly as task.inspect reports them.
            event.progress = entry->second.final_progress;
            event.has_success = entry->second.has_success;
            event.success = entry->second.success;
            settled = true;
        }
    }
    if (!settled) {
        // Serial context: the live pinned view is the same projection
        // task.inspect reports for in-flight tasks.
        const TaskViewResult view = service.host.task_view(TaskIdentity{task_id});
        event.progress = view.ok ? progress_name(view.view.progress) : "Unknown";
        if (view.ok && terminal_progress(view.view.progress)) {
            event.has_success = true;
            event.success = view.view.success;
        }
    }
    service.events.publish_task_update(std::move(event));
}

void publish_task_updated_best_effort(const std::shared_ptr<ServiceCore> &core,
                                      const std::string &task_id) {
    try {
        auto posted = core->executor.submit_on(
            core->serial, [core, task_id] { publish_task_updated(core, task_id); });
        if (!ready_within(posted, core->command_wait)) {
            return; // dropped: events are notifications, snapshots are truth
        }
        consume(std::move(posted));
    } catch (const std::exception &) {
        // Rejected admission (teardown in progress) ends the publish path.
    } catch (...) {
    }
}

void publish_permission_request_best_effort(const std::shared_ptr<ServiceCore> &core,
                                            ipc::PermissionRequestedEvent event) {
    try {
        auto posted = core->executor.submit_on(
            core->serial, [core, event] { core->events.publish_permission_request(event); });
        if (!ready_within(posted, core->command_wait)) {
            return; // dropped: the hub's pending set stays the truth (DEC-020)
        }
        consume(std::move(posted));
    } catch (const std::exception &) {
        // Rejected admission (teardown in progress) ends the publish path.
    } catch (...) {
    }
}

void run_driver(executor::StopToken stop_token, std::shared_ptr<ServiceCore> core,
                std::string task_id) {
    if (!core) {
        return;
    }
    ServiceCore &service = *core;
    try {
        std::size_t index = 0;
        mirage::desktop::CancelToken cancel;
        for (;;) {
            ipc::TaskStep step;
            std::chrono::milliseconds step_budget = service.step_timeout;
            {
                std::lock_guard lock(service.registry.mutex);
                auto entry = service.registry.tasks.find(task_id);
                if (entry == service.registry.tasks.end()) {
                    return; // registry vanished (shutdown); nothing to drive
                }
                if (index >= entry->second.steps.size()) {
                    break; // all steps settled successfully
                }
                step = entry->second.steps[index].spec;
                step_budget = entry->second.step_timeout;
                cancel = entry->second.cancel;
            }

            if (stop_token.stop_requested() || cancel.cancelled()) {
                for_each_step(service, task_id, index,
                              [](StepRecord &pending) { pending.status = step_status::kSkipped; });
                mark_driver_done(core, task_id, "Cancelled", false, false);
                return;
            }

            // RULE-05 gate (DEC-010 / DEC-020): the capability is judged
            // before the pinned operation is admitted and before any
            // provider side effect, so a denied action never reaches the
            // desktop. A missing controller denies too (fail closed). The
            // probe covers cancellation arriving during a Confirm rule's
            // bounded wait (DEC-020).
            mirage::runtime::permission::PermissionRequest request;
            request.capability = capability_of(step.kind);
            request.resource = step.argument;
            request.task_id = task_id;
            const auto cancelled_probe = [&cancel, &stop_token]() -> bool {
                return cancel.cancelled() || stop_token.stop_requested();
            };
            mirage::runtime::permission::PermissionVerdict verdict;
            if (service.permission != nullptr) {
                verdict = service.permission->authorize(request, cancelled_probe);
            } else {
                verdict.reason = "permission controller unavailable";
            }
            // Cancellation outranks the verdict whenever it fired (before or
            // during the confirmation wait, DEC-020): the interrupted step
            // is cancelled with an empty permission field — the gate never
            // concluded — and the pinned cancel owns the settlement.
            if (cancelled_probe()) {
                mark_step(service, task_id, index, step_status::kCancelled, {}, "", false, -1, {},
                          false, {});
                for_each_step(service, task_id, index + 1,
                              [](StepRecord &pending) { pending.status = step_status::kSkipped; });
                mark_driver_done(core, task_id, "Cancelled", false, false);
                return;
            }
            const char *step_permission =
                mirage::runtime::permission::decision_name(verdict.decision);
            mark_step_permission(service, task_id, index, step_permission);
            if (!verdict.allowed) {
                mark_step(service, task_id, index, step_status::kFailed, {}, step_permission, false,
                          -1, {}, false, "permission_denied: " + verdict.reason);
                for_each_step(service, task_id, index + 1,
                              [](StepRecord &pending) { pending.status = step_status::kSkipped; });
                settle_failed(core, task_id, "permission_denied: " + verdict.reason);
                return;
            }

            OperationTicket ticket;
            if (!begin_operation(service, task_id, ticket)) {
                // The pinned control plane refused the operation: the task
                // is unknown, cancelled or already settled elsewhere. The
                // pinned state stays authoritative; nothing is marked
                // failed from the driver side.
                for_each_step(service, task_id, index,
                              [](StepRecord &pending) { pending.status = step_status::kSkipped; });
                mark_driver_done(core, task_id, "Cancelled", false, false);
                return;
            }
            mark_step(service, task_id, index, step_status::kRunning, ticket.operation_id,
                      step_permission, false, -1, {}, false, {});
            publish_task_updated_best_effort(core, task_id);

            bool ok = false;
            bool step_cancelled = false;
            int exit_code = -1;
            std::string result;
            bool truncated = false;
            std::string error;
            execute_action(service, step, cancel, step_budget, ok, step_cancelled, exit_code,
                           result, truncated, error);
            if (step_cancelled || cancel.cancelled()) {
                // The action was interrupted by a task cancellation: the
                // interrupted step is cancelled (not failed), the pending
                // ones are skipped, and the pinned cancel — not the driver
                // — owns the terminal settlement.
                mark_step(service, task_id, index, step_status::kCancelled, ticket.operation_id,
                          step_permission, false, exit_code, std::move(result), truncated,
                          std::move(error));
                for_each_step(service, task_id, index + 1,
                              [](StepRecord &pending) { pending.status = step_status::kSkipped; });
                admit_completion(service, ticket);
                mark_driver_done(core, task_id, "Cancelled", false, false);
                return;
            }
            mark_step(service, task_id, index, ok ? step_status::kOk : step_status::kFailed,
                      ticket.operation_id, step_permission, ok, exit_code, std::move(result),
                      truncated, error);
            admit_completion(service, ticket);

            if (!ok) {
                // Fail-fast (DEC-007 item 5): settle the task failed and
                // leave the remaining steps skipped.
                for_each_step(service, task_id, index + 1,
                              [](StepRecord &pending) { pending.status = step_status::kSkipped; });
                settle_failed(core, task_id, error);
                return;
            }
            publish_task_updated_best_effort(core, task_id);
            ++index;
        }

        auto complete = post_host(service, [&service, task_id] {
            return service.host.complete_task(identity_of(task_id), true);
        });
        bool settled = false;
        if (ready_within(complete, service.command_wait)) {
            try {
                settled = complete.get().ok;
            } catch (const std::exception &) {
                settled = false;
            }
        }
        mark_driver_done(core, task_id, "Completed", settled, settled);
    } catch (const std::exception &) {
        mark_driver_done(core, task_id, "Failed", false, false);
    } catch (...) {
        mark_driver_done(core, task_id, "Failed", false, false);
    }
}

} // namespace mirage::runtime::detail
