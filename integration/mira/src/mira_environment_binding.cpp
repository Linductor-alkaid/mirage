#include <mirage/integration/mira_environment_binding.hpp>

#include <stdexcept>
#include <utility>

namespace mirage::integration {
namespace {

mira::Error pinned_error(mira::ErrorCode code, std::string message) {
    mira::Error error;
    error.code = code;
    error.safe_message = std::move(message);
    return error;
}

} // namespace

MiraEnvironmentBinding::MiraEnvironmentBinding(
    std::shared_ptr<mirage::desktop::DesktopEnvironment> environment)
    : environment_(std::move(environment)) {
    if (!environment_) {
        throw std::invalid_argument("MiraEnvironmentBinding requires a desktop environment");
    }
    name_ = "mirage.desktop." + environment_->info().platform + "-v1";
    clock_domain_ = mira::ClockDomainId::generate();
}

const char* MiraEnvironmentBinding::binding_name() const {
    return name_.c_str();
}

mira::EnvironmentCapabilities MiraEnvironmentBinding::capabilities() const {
    // The M1 desktop environment set carries no screen, structure, foreground
    // or input surface; declare exactly that instead of letting Core guess
    // from the platform name (pinned contract: adapters must not declare a
    // capability they cannot honor).
    return {};
}

mira::Result<mira::Observation>
MiraEnvironmentBinding::observe(const mira::ObservationRequest& request,
                                const mira::OperationContext& context) {
    if (const auto validated = mira::validate_observation_request(request); !validated) {
        return validated.error();
    }
    if (context.cancelled()) {
        return pinned_error(mira::ErrorCode::Cancelled,
                            "observation was cancelled before capture");
    }
    const auto unsupported = mira::unsupported_required_components(capabilities(), request);
    if (!unsupported.empty()) {
        std::string message = "observation requires unsupported components:";
        for (const auto& component : unsupported) {
            message += " " + component;
        }
        return pinned_error(mira::ErrorCode::UnsupportedCapability, std::move(message));
    }

    // Nothing required is deliverable and nothing was required: return a
    // minimal component-free observation instead of silently incomplete
    // content.
    mira::Observation observation;
    observation.id = mira::ObservationId::generate();
    observation.session_id = context.session;
    observation.environment_epoch = 0;
    observation.aggregate_span.clock_domain = clock_domain_;
    observation.aggregate_span.normalized_begin = mira::Timestamp::now();
    observation.aggregate_span.normalized_end = observation.aggregate_span.normalized_begin;
    observation.aggregate_span.sync_quality = mira::ClockSyncQuality::Synced;
    observation.atomicity = mira::ObservationAtomicity::NonAtomic;
    return observation;
}

mira::Result<mira::ExecutionReceipt>
MiraEnvironmentBinding::execute(const mira::InputSequence& input,
                                const mira::OperationContext& context) {
    (void)input;
    mira::ExecutionReceipt receipt;
    // Refuse before any side effect: no input provider is bound in the M1
    // environment set, and the pinned receipt vocabulary expresses exactly
    // that (Rejected, no possible side effect).
    receipt.status = mira::ExecutionStatus::Rejected;
    receipt.side_effect_may_have_occurred = false;
    receipt.environment_epoch = 0;
    receipt.safe_message = "input dispatch is not available in the M1 desktop environment";
    if (context.cancelled()) {
        receipt.safe_message = "input dispatch was cancelled before dispatch";
    }
    return receipt;
}

mira::Result<void> MiraEnvironmentBinding::interrupt(const mira::OperationContext& context) {
    (void)context;
    // Best-effort release: the M1 environment set has no in-flight platform
    // input or blocking capture to release, so interrupt is an idempotent
    // success.
    return {};
}

} // namespace mirage::integration
