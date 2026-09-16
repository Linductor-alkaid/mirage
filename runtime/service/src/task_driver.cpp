#include "task_driver.hpp"

#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/runtime/ipc/protocol.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <utility>

namespace mirage::runtime::detail {
namespace {

TaskIdentity identity_of(const std::string& task_id) {
    TaskIdentity identity;
    identity.id = task_id;
    return identity;
}

/// Serialized host operation: every MiraHost call from the driver goes onto
/// the service's single serial context (the host's one-owner discipline).
template <typename F> auto post_host(ServiceCore& core, F&& operation)
    -> std::future<typename std::invoke_result<F>::type> {
    return core.executor.submit_on(core.serial, std::forward<F>(operation));
}

template <typename T>
bool ready_within(std::future<T>& future, std::chrono::milliseconds budget) {
    return future.valid() &&
           future.wait_for(budget) == std::future_status::ready;
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
    } catch (const std::exception&) {
    } catch (...) {
    }
}

void for_each_step(ServiceCore& core, const std::string& task_id,
                   std::size_t begin, const std::function<void(StepRecord&)>& mutate) {
    std::lock_guard lock(core.registry.mutex);
    auto entry = core.registry.tasks.find(task_id);
    if (entry == core.registry.tasks.end()) {
        return;
    }
    auto& steps = entry->second.steps;
    for (std::size_t index = begin; index < steps.size(); ++index) {
        mutate(steps[index]);
    }
}

void mark_step(ServiceCore& core, const std::string& task_id, std::size_t index,
               const char* status, const std::string& operation_id, bool ok,
               int exit_code, std::string result, bool truncated,
               std::string error) {
    std::lock_guard lock(core.registry.mutex);
    auto entry = core.registry.tasks.find(task_id);
    if (entry == core.registry.tasks.end() ||
        index >= entry->second.steps.size()) {
        return;
    }
    StepRecord& step = entry->second.steps[index];
    step.status = status;
    step.operation_id = operation_id;
    step.ok = ok;
    step.exit_code = exit_code;
    step.result = std::move(result);
    step.result_truncated = truncated;
    step.error = std::move(error);
}

void mark_driver_done(ServiceCore& core, const std::string& task_id,
                      bool has_success, bool success) {
    std::lock_guard lock(core.registry.mutex);
    auto entry = core.registry.tasks.find(task_id);
    if (entry != core.registry.tasks.end()) {
        entry->second.driver_done = true;
        entry->second.has_success = has_success;
        entry->second.success = success;
    }
}

/// Admits one desktop operation for the task; false means the pinned
/// runtime refused (unknown identity, cancelled or settled era), which ends
/// the driving loop without marking the task failed from here.
bool begin_operation(ServiceCore& core, const std::string& task_id,
                     OperationTicket& ticket) {
    auto begin = post_host(core, [&core, &task_id] {
        return core.host.begin_operation(identity_of(task_id));
    });
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
    } catch (const std::exception&) {
        return false;
    }
}

void admit_completion(ServiceCore& core, const OperationTicket& ticket) {
    // Idempotent by pinned contract (stale/late tickets settle as NoOps), so
    // a timeout or rejection here never escalates into a task failure.
    consume(post_host(core, [&core, ticket] {
                return core.host.admit_operation_completion(ticket);
            }));
}

/// Executes one step's desktop action on the caller (driver) thread; the
/// provider is synchronous and bounded by its own budget. Returns the
/// structured outcome the registry records.
void execute_action(ServiceCore& core, const ipc::TaskStep& step,
                    std::chrono::milliseconds step_budget, bool& ok,
                    int& exit_code, std::string& result, bool& truncated,
                    std::string& error) {
    auto* environment = core.environment.get();
    if (environment == nullptr) {
        error = "internal: no desktop environment bound";
        return;
    }
    if (step.kind == ipc::StepKind::FilesystemRead) {
        auto* filesystem = environment->filesystem();
        if (filesystem == nullptr) {
            error = "filesystem provider unavailable";
            return;
        }
        const auto outcome = filesystem->read_text_file(step.argument);
        if (!outcome.ok) {
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
    auto* process = environment->process();
    if (process == nullptr) {
        error = "process provider unavailable";
        return;
    }
    desktop::ProcessLimits limits;
    limits.timeout = step_budget;
    const auto outcome = process->execute(step.argument, limits);
    exit_code = outcome.exit_code;
    if (outcome.output_truncated) {
        truncated = true;
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
        error = "process reported failure (exit code " +
                std::to_string(outcome.exit_code) + ")";
        return;
    }
    ok = true;
}

} // namespace

void run_driver(executor::StopToken stop_token,
                std::shared_ptr<ServiceCore> core, std::string task_id) {
    if (!core) {
        return;
    }
    ServiceCore& service = *core;
    try {
        std::size_t index = 0;
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
            }

            if (stop_token.stop_requested()) {
                for_each_step(service, task_id, index,
                              [](StepRecord& pending) {
                                  pending.status = step_status::kSkipped;
                              });
                mark_driver_done(service, task_id, false, false);
                return;
            }

            OperationTicket ticket;
            if (!begin_operation(service, task_id, ticket)) {
                // The pinned control plane refused the operation: the task
                // is unknown, cancelled or already settled elsewhere. The
                // pinned state stays authoritative; nothing is marked
                // failed from the driver side.
                for_each_step(service, task_id, index,
                              [](StepRecord& pending) {
                                  pending.status = step_status::kSkipped;
                              });
                mark_driver_done(service, task_id, false, false);
                return;
            }
            mark_step(service, task_id, index, step_status::kRunning,
                      ticket.operation_id, false, -1, {}, false, {});

            bool ok = false;
            int exit_code = -1;
            std::string result;
            bool truncated = false;
            std::string error;
            execute_action(service, step, step_budget, ok, exit_code, result,
                           truncated, error);
            mark_step(service, task_id, index,
                      ok ? step_status::kOk : step_status::kFailed,
                      ticket.operation_id, ok, exit_code, std::move(result),
                      truncated, error);
            admit_completion(service, ticket);

            if (!ok) {
                // Fail-fast (DEC-007 item 5): settle the task failed and
                // leave the remaining steps skipped.
                for_each_step(service, task_id, index + 1,
                              [](StepRecord& pending) {
                                  pending.status = step_status::kSkipped;
                              });
                auto complete = post_host(service, [&service, &task_id, &error] {
                    return service.host.complete_task(identity_of(task_id),
                                                      false, error);
                });
                const bool settled =
                    ready_within(complete, service.command_wait) &&
                    [&] {
                        try {
                            return complete.get().ok;
                        } catch (const std::exception&) {
                            return false;
                        }
                    }();
                // has_success only when the pinned runtime accepted the
                // settlement; a concurrent cancel keeps the terminal say.
                mark_driver_done(service, task_id, settled, false);
                return;
            }
            ++index;
        }

        auto complete = post_host(service, [&service, &task_id] {
            return service.host.complete_task(identity_of(task_id), true);
        });
        bool settled = false;
        if (ready_within(complete, service.command_wait)) {
            try {
                settled = complete.get().ok;
            } catch (const std::exception&) {
                settled = false;
            }
        }
        mark_driver_done(service, task_id, settled, settled);
    } catch (const std::exception&) {
        mark_driver_done(service, task_id, false, false);
    } catch (...) {
        mark_driver_done(service, task_id, false, false);
    }
}

} // namespace mirage::runtime::detail
