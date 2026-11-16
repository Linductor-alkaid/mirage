#pragma once

#include <mirage/runtime/ipc/protocol.hpp>

#include "service_core.hpp"

namespace mirage::runtime::detail {
// Serial-context only. This is a model-resource reservation, not a second
// attempt registry: all futures, outcomes and budgets remain in Mira.
HostOutcome refresh_workflow_recovery(ServiceCore &);
ipc::Response control_workflow_recovery(ServiceCore &, const ipc::WorkflowRecoveryRequest &);
} // namespace mirage::runtime::detail
