#pragma once

// Shared hermetic mira model-provider fakes for the runtime service tests
// (REUSE-14): the dialog-face scenes of runtime_service_test.cpp and
// event_subscription_test.cpp grew four mutually duplicated providers; this
// header is their single home. The production socket provider denies
// private/loopback endpoints by design (DEC-027), so these fakes ride the
// layer's ModelProviderOverride seam instead of a local origin.
//
// Wire-value discipline (MIRA-20261013-001): Result::value() appears only on
// literals whose legality is already established; anything read off the wire
// (or out of a response object) is pattern-matched, never .value()'d.
//
// GatedModelProvider is the superset of the two old gated fakes:
// fail_subsequent() comes from runtime_service_test's GatedModelProvider;
// event_subscription_test's GatedDialogProvider never calls it (the extra
// capability is simply unused there).

#include <mira/model_contracts.hpp>
#include <mira/model_provider.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace mirage::testing {

/// A scripted pinned model provider: binds to the layer-assembled profile and
/// answers every inference with the fixed assistant text, Completed.
class ScriptedModelProvider final : public mira::IModelProvider {
  public:
    explicit ScriptedModelProvider(std::string text) : text_(std::move(text)) {}
    [[nodiscard]] const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        ++calls_;
        mira::ModelResponse response;
        response.contract_version = mira::SchemaVersion{1, 0};
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.requested_model = profile_.model_selector;
        response.status = mira::ModelCompletionStatus::Completed;
        mira::MessageOutput message;
        message.role = mira::ModelRole::Assistant;
        mira::OutputTextPart text;
        text.text = text_;
        message.content.emplace_back(std::move(text));
        response.output.emplace_back(std::move(message));
        return response;
    }
    /// Binds the layer-assembled profile (the override factory receives it).
    void bind_profile(const mira::ModelProfile &profile) { profile_ = profile; }
    [[nodiscard]] int calls() const { return calls_.load(); }

  private:
    mira::ModelProfile profile_;
    std::atomic<int> calls_{0};
    std::string text_;
};

/// A gate-blocked scripted provider: an inference holds inside infer() until
/// the test releases it — the window the in-flight latch and the pending
/// history shape live in — and later inferences return a scripted outcome
/// (the canned reply, or a Failed status after fail_subsequent()), so the
/// failure settlement path is reachable deterministically. The cancellation
/// probe is honored so teardown is never blocked.
class GatedModelProvider final : public mira::IModelProvider {
  public:
    GatedModelProvider() = default;
    [[nodiscard]] const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &context,
                                            const mira::ProviderInferOptions &) override {
        ++calls_;
        while (hold_.load() && !context.cancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        mira::ModelResponse response;
        response.contract_version = mira::SchemaVersion{1, 0};
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.requested_model = profile_.model_selector;
        response.status = context.cancelled() ? mira::ModelCompletionStatus::Cancelled
                                              : mira::ModelCompletionStatus::Completed;
        if (response.status == mira::ModelCompletionStatus::Completed) {
            if (fail_after_release_.load()) {
                response.status = mira::ModelCompletionStatus::Failed;
                return response;
            }
            mira::MessageOutput message;
            message.role = mira::ModelRole::Assistant;
            mira::OutputTextPart text;
            text.text = "门控回复正常。";
            message.content.emplace_back(std::move(text));
            response.output.emplace_back(std::move(message));
        }
        return response;
    }

    void hold_open() { hold_.store(true); }
    void release() { hold_.store(false); }
    void fail_subsequent() { fail_after_release_.store(true); }

    /// Binds the layer-assembled profile (the override factory receives it).
    void bind_profile(const mira::ModelProfile &profile) { profile_ = profile; }
    [[nodiscard]] int calls() const { return calls_.load(); }

  private:
    mira::ModelProfile profile_;
    std::atomic<int> calls_{0};
    std::atomic<bool> hold_{false};
    std::atomic<bool> fail_after_release_{false};
};

} // namespace mirage::testing
