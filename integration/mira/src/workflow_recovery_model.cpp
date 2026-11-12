#include <mirage/integration/workflow_recovery_model.hpp>

namespace mirage::integration {
namespace {
mira::Error failure() {
    mira::Error e;
    e.code = mira::ErrorCode::Internal;
    e.domain = "mirage.workflow.recovery-provider";
    e.safe_message = "workflow recovery provider failed";
    return e;
}
class Guard final : public mira::IModelProvider {
    std::shared_ptr<mira::IModelProvider> provider_;
    mira::ModelProfile profile_;
    mira::TransportTrace empty_trace_;
    mira::SseStreamStats empty_sse_;

  public:
    explicit Guard(std::shared_ptr<mira::IModelProvider> p)
        : provider_(std::move(p)), profile_(provider_->profile()) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &r,
                                            const mira::OperationContext &c,
                                            const mira::ProviderInferOptions &o) override {
        try {
            auto result = provider_->infer(r, c, o);
            if (!result) {
                auto clean = result.error();
                clean.safe_message = "workflow recovery provider failed";
                return clean;
            }
            return result;
        } catch (...) {
            return failure();
        }
    }
    const mira::TransportTrace &last_trace() const override {
        try {
            return provider_->last_trace();
        } catch (...) {
            return empty_trace_;
        }
    }
    const mira::SseStreamStats &last_sse_stats() const override {
        try {
            return provider_->last_sse_stats();
        } catch (...) {
            return empty_sse_;
        }
    }
    std::optional<std::chrono::milliseconds> last_retry_after_hint() const override {
        try {
            return provider_->last_retry_after_hint();
        } catch (...) {
            return std::nullopt;
        }
    }
};
} // namespace
mira::Result<std::shared_ptr<mira::IModelProvider>>
guard_workflow_recovery_provider(std::shared_ptr<mira::IModelProvider> provider) {
    if (!provider) {
        auto e = failure();
        e.code = mira::ErrorCode::InvalidArgument;
        return e;
    }
    try {
        return std::shared_ptr<mira::IModelProvider>{std::make_shared<Guard>(std::move(provider))};
    } catch (...) {
        return failure();
    }
}
} // namespace mirage::integration
