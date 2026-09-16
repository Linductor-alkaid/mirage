#pragma once

#include <executor/stop_token.hpp>

#include <memory>
#include <string>

#include "service_core.hpp"

namespace mirage::runtime::detail {

/// Body of one M1 task driver, running as a cancellable executor task
/// (DEC-007 item 5/7). Walks the task's scripted steps in order; every
/// desktop action is bracketed with the host operation surface
/// (begin_operation / admit_operation_completion posted onto the service's
/// serial context) so it enters the pinned control plane with an operation
/// id, and the structured Provider result is recorded per step. The first
/// failed step settles the task as failed (fail-fast); cooperative
/// cancellation is observed between steps. The function never throws:
/// unexpected failures are recorded on the task and settle it failed, and
/// the pinned task state stays authoritative via MiraHost.
void run_driver(executor::StopToken stop_token, std::shared_ptr<ServiceCore> core,
                std::string task_id);

/// Publishes one `task.updated` snapshot for the task (DEC-012 decision 3).
/// Serial-context only: reads the registry record and, for a task that has
/// not settled, the live pinned progress (the same projection task.inspect
/// reports), then hands the snapshot to the service's event hub.
void publish_task_updated(const std::shared_ptr<ServiceCore> &core, const std::string &task_id);

/// Driver-thread entry: posts the publish onto the serial context so event
/// publication order matches the serialized service state changes. The
/// publish is a notification, not a reliable delivery (DEC-012 decision 4):
/// a rejected, drained or late post is dropped instead of blocking the
/// driver.
void publish_task_updated_best_effort(const std::shared_ptr<ServiceCore> &core,
                                      const std::string &task_id);

} // namespace mirage::runtime::detail
