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

} // namespace mirage::runtime::detail
