#include "../../integration/mira/src/pet_exclusion.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <mirage/integration/desktop_atom_toolset.hpp>
#include <stdexcept>

namespace {
using namespace mirage;
struct State {
    int acquired = 0, released = 0, verifies = 0, calls = 0;
    bool hidden = false, fail_acquire = false, fail_verify = false, fail_restore = false;
    bool passive = false;
};
class Lease final : public desktop::PresentationLease {
  public:
    explicit Lease(State &state) : state_(state), excluded_(!state.passive) {
        state_.hidden = excluded_;
    }
    bool excluded() const override { return excluded_; }
    ~Lease() override { (void)release(); }
    desktop::ProviderError verify(const desktop::CancelToken &cancel) override {
        ++state_.verifies;
        if (cancel.cancelled())
            return {"cancelled", "cancelled fixture"};
        return state_.fail_verify ? desktop::ProviderError{"pet_exclusion_failed", "fixture"}
                                  : desktop::ProviderError{};
    }
    desktop::ProviderError release() override {
        if (released_)
            return {};
        released_ = true;
        ++state_.released;
        state_.hidden = false;
        return state_.fail_restore ? desktop::ProviderError{"pet_restore_failed", "fixture"}
                                   : desktop::ProviderError{};
    }

  private:
    State &state_;
    bool excluded_;
    bool released_ = false;
};
integration::detail::PetExclusion acquire(State &state) {
    return [&state](const desktop::CancelToken &) -> desktop::PresentationExclusion {
        ++state.acquired;
        if (state.fail_acquire)
            return {{}, {"pet_exclusion_unavailable", "fixture"}};
        return {std::make_unique<Lease>(state), {}};
    };
}
mira::Result<mira::ToolExecutionRecord> execute(mira::BuiltinToolRegistry &registry,
                                                const std::string &name) {
    for (const auto &spec : registry.exposed_tools()) {
        if (spec.wire_name != name)
            continue;
        mira::ToolProposal proposal;
        proposal.provider_call_id = mira::ProviderToolCallId{"fixture"};
        proposal.tool_id = spec.tool_id;
        proposal.wire_name = spec.wire_name;
        proposal.tool_version = spec.version;
        proposal.arguments = mira::JsonValue{mira::JsonValue::Object{}};
        proposal.arguments_digest = mira::canonical_json_digest(proposal.arguments);
        proposal.operation_id = mira::OperationId::generate();
        proposal.has_side_effects = spec.has_side_effects;
        return registry.execute(proposal, mira::make_control_context());
    }
    MIRAGE_CHECK(false);
    return mira::Error{};
}
} // namespace
int main() {
    using namespace mirage;
    using integration::detail::exclude_pet_during;
    const auto args = mira::JsonValue{mira::JsonValue::Object{}};
    const auto context = mira::make_control_context();
    State state;
    mira::BuiltinToolHandler handler = [&](const auto &,
                                           const auto &) -> mira::Result<mira::JsonValue> {
        ++state.calls;
        MIRAGE_CHECK(state.hidden);
        return args;
    };
    auto tool = exclude_pet_during("desktop.input.click", handler, acquire(state));
    MIRAGE_CHECK(tool(args, context));
    MIRAGE_CHECK(state.acquired == 1 && state.released == 1 && state.verifies == 2 &&
                 !state.hidden);
    state.fail_acquire = true;
    auto result = tool(args, context);
    MIRAGE_CHECK(!result && result.error().safe_message.starts_with("pet_exclusion_unavailable"));
    MIRAGE_CHECK(state.calls == 1 && !state.hidden);
    state.fail_acquire = false;
    state.fail_verify = true;
    result = tool(args, context);
    MIRAGE_CHECK(!result && state.calls == 1 && state.released == 2 && !state.hidden);
    state.fail_verify = false;
    state.fail_restore = true;
    result = tool(args, context);
    MIRAGE_CHECK(!result && result.error().code == mira::ErrorCode::ExecutionUncertain);
    MIRAGE_CHECK(result.error().safe_message.starts_with("pet_restore_failed") && !state.hidden);
    state.fail_restore = false;
    tool = exclude_pet_during(
        "desktop.input.click",
        [&](const auto &, const auto &) -> mira::Result<mira::JsonValue> {
            MIRAGE_CHECK(state.hidden);
            throw std::runtime_error("injected provider exception");
        },
        acquire(state));
    result = tool(args, context);
    MIRAGE_CHECK(!result && !state.hidden && state.released == 4);
    // Running cancellation still restores before reporting the handler result.
    desktop::CancelToken cancel;
    tool = exclude_pet_during(
        "desktop.input.type_text",
        [&](const auto &, const auto &) -> mira::Result<mira::JsonValue> {
            MIRAGE_CHECK(state.hidden);
            cancel.request_cancel();
            mira::Error error;
            error.code = mira::ErrorCode::Cancelled;
            return error;
        },
        acquire(state), cancel);
    result = tool(args, context);
    MIRAGE_CHECK(!result && result.error().code == mira::ErrorCode::Cancelled && !state.hidden);
    const auto admitted = state.acquired;
    MIRAGE_CHECK(!tool(args, context));
    MIRAGE_CHECK(state.acquired == admitted); // queued/pre-dispatch cancellation
    auto expired = context;
    expired.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds{1};
    tool = exclude_pet_during("desktop.input.click", handler, acquire(state));
    MIRAGE_CHECK(!tool(args, expired) && state.acquired == admitted);
    // Non-desktop work and no-pet embeddings do not acquire a surface lease.
    auto plain = [&](const auto &, const auto &) -> mira::Result<mira::JsonValue> {
        MIRAGE_CHECK(!state.hidden);
        return args;
    };
    MIRAGE_CHECK(exclude_pet_during("exec", plain, acquire(state))(args, context));
    MIRAGE_CHECK(exclude_pet_during("desktop.window.front", plain, {})(args, context));
    MIRAGE_CHECK(state.acquired == admitted);
    // Admission exceptions (e.g. Executor shutting down) refuse before handler.
    auto rejected = [](const desktop::CancelToken &) -> desktop::PresentationExclusion {
        throw std::runtime_error("Executor admission rejected");
    };
    const auto before_calls = state.calls;
    result = exclude_pet_during("desktop.input.click", handler, rejected)(args, context);
    MIRAGE_CHECK(!result && result.error().code == mira::ErrorCode::PlatformError);
    MIRAGE_CHECK(state.calls == before_calls);
    // Read-only tools never withdraw the pet, even if exclusion is unavailable.
    integration::DesktopToolContext desktop;
    desktop.environment = std::make_shared<testing::FakeDesktopEnvironment>();
    desktop.exclude_pet = acquire(state);
    auto registry = integration::DesktopAtomToolset::build(desktop)->registry();
    state.fail_acquire = true;
    const auto reads_before = state.acquired;
    auto record = execute(*registry, "desktop.window.list");
    MIRAGE_CHECK(record && !record.value().failed && state.acquired == reads_before);
    for (const auto *name : {"desktop.screen.capture", "desktop.vision.ocr", "desktop.window.list",
                             "desktop.accessibility.snapshot"})
        MIRAGE_CHECK(exclude_pet_during(name, plain, acquire(state))(args, context));
    MIRAGE_CHECK(state.acquired == reads_before);
    state.fail_acquire = false;
    // The pixel boundary restores before analysis and carries fallback evidence.
    for (bool passive : {false, true}) {
        state.passive = passive;
        auto frame = desktop::capture_with_presentation(acquire(state), {}, [&] {
            MIRAGE_CHECK(state.hidden == !passive);
            desktop::CaptureOutcome out;
            out.ok = true;
            return out;
        });
        MIRAGE_CHECK(frame.ok && frame.presentation_excluded == !passive && !state.hidden);
    }
    state.passive = false;
    auto frame =
        desktop::capture_with_presentation(acquire(state), {}, [&]() -> desktop::CaptureOutcome {
            MIRAGE_CHECK(state.hidden);
            throw std::runtime_error("capture exception");
        });
    MIRAGE_CHECK(!frame.ok && !state.hidden);
    state.fail_restore = true;
    frame = desktop::capture_with_presentation(acquire(state), {}, [] {
        desktop::CaptureOutcome out;
        out.ok = true;
        return out;
    });
    MIRAGE_CHECK(!frame.ok && frame.error.code == "pet_restore_failed" && !state.hidden);
    state.fail_restore = false;
    state.fail_verify = true;
    bool captured = false;
    frame = desktop::capture_with_presentation(acquire(state), {}, [&] {
        captured = true;
        return desktop::CaptureOutcome{};
    });
    MIRAGE_CHECK(!frame.ok && !captured && !state.hidden);
    return testing::finish("pet_exclusion_tools_test");
}
