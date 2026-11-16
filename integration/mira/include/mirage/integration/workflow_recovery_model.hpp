#pragma once

#include <functional>
#include <memory>
#include <mira/model_gateway.hpp>

namespace mirage::integration {
/// DEC-144: externally assembled model stack for explicit Workflow recovery.
/// Gateway/provider/profile/transport use the Host's Executor. Their owner
/// drains Host recovery before reconfiguring or closing the model stack.
/// No private ModelLayer gateway or additional model stack is created here.
struct WorkflowRecoveryModel {
    std::shared_ptr<mira::ModelGateway> gateway;
    mira::ModelProfileId profile_id;
    /// Same provider as the externally configured profile. Host registers a
    /// non-throwing recovery boundary before inference; no concurrent reuse.
    std::shared_ptr<mira::IModelProvider> provider;
    /// Trusted, bounded host whitelist; empty keeps Mira's value-free
    /// projection. Never populate this from model output or imported claims.
    std::function<mira::JsonValue(const mira::JsonValue &)> project_parameters;
};

/// MIRA-20261112-002: temporary single-Adapter boundary until pinned recovery
/// catches provider/future exceptions. No exception body enters diagnostics.
[[nodiscard]] mira::Result<std::shared_ptr<mira::IModelProvider>>
    guard_workflow_recovery_provider(std::shared_ptr<mira::IModelProvider>);
} // namespace mirage::integration
