#include "../../platform/linux/src/input_method_backend.hpp"
#include "../support/test.hpp"
#include <cstdlib>
#include <iostream>
#include <kairo/executor.hpp>
#include <memory>
#include <mira/json.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>

namespace {
class Environment final : public mirage::desktop::DesktopEnvironment {
  public:
    std::unique_ptr<mirage::desktop::InputMethodProvider> provider;
    mirage::desktop::EnvironmentInfo info() const override { return {"ime-fixture", "linux"}; }
    mirage::desktop::InputMethodProvider *input_method() override { return provider.get(); }
};
class ContractProvider final : public mirage::desktop::InputMethodProvider {
  public:
    mirage::desktop::InputMethodOutcome result;
    mirage::desktop::InputMethodOutcome observe(const mirage::desktop::InputMethodLimits &,
                                                const mirage::desktop::CancelToken &) override {
        return result;
    }
};
void contract() {
    using namespace mirage;
    Environment env;
    auto provider = std::make_unique<ContractProvider>();
    auto *raw = provider.get();
    env.provider = std::move(provider);
    auto tools = integration::DesktopAtomToolset::build(&env, {});
    const auto spec = tools->registry()->exposed_tools().front();
    const auto invoke = [&] {
        mira::ToolProposal proposal;
        proposal.provider_call_id = mira::ProviderToolCallId{"contract"};
        proposal.tool_id = spec.tool_id;
        proposal.wire_name = spec.wire_name;
        proposal.tool_version = spec.version;
        proposal.arguments = mira::JsonValue{mira::JsonValue::Object{}};
        proposal.arguments_digest = mira::canonical_json_digest(proposal.arguments);
        proposal.operation_id = mira::OperationId::generate();
        return tools->registry()->execute(proposal, mira::make_control_context());
    };
    for (int mode = 0; mode < 8; ++mode) {
        desktop::InputMethodState state;
        state.backend = "fixture";
        state.engine = {"pinyin", "fixture.engine", 123, {}};
        if (mode == 1)
            state.backend.resize(33, 'x');
        if (mode == 2)
            state.engine.value = std::string(257, 'x');
        if (mode == 3)
            state.engine.source.clear();
        if (mode == 4)
            state.engine.captured_at_ms = 0;
        raw->result = {true, false, {state}, {}};
        if (mode == 5)
            raw->result.input_methods.resize(3);
        if (mode == 6)
            raw->result = {false, true, {}, {"cancelled", "fixture"}};
        if (mode == 7)
            raw->result = {false, false, {}, {"timeout", "fixture"}};
        const auto result = invoke();
        if (mode == 6) {
            MIRAGE_CHECK(!result && result.error().code == mira::ErrorCode::Cancelled);
            continue;
        }
        if (!result || result.value().failed != (mode != 0))
            std::cerr << "contract mode=" << mode << " result=" << bool(result) << " failure="
                      << (result ? result.value().safe_error_summary : result.error().safe_message)
                      << '\n';
        MIRAGE_CHECK(result && result.value().failed == (mode != 0));
        if (mode == 0 && result && !result.value().failed) {
            const auto &entry = result.value().result.find("input_methods")->as_array()->front();
            MIRAGE_CHECK(*entry.find("conversion_mode")->find("status")->as_string() == "unknown");
            MIRAGE_CHECK(entry.find("conversion_mode")->find("value")->is_null());
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    using namespace mirage;
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    MIRAGE_CHECK(executor.initialize(config));
    auto work = executor.submit_auto([&] {
        contract();
        auto env = std::make_shared<Environment>();
        env->provider = platform::linux_backend::InputMethodBackend::open();
        MIRAGE_CHECK(bool(env->provider));
        if (!env->provider)
            return;
        if (argc == 2 && std::string(argv[1]) == "--timeout") {
            const auto begin = std::chrono::steady_clock::now();
            auto result = env->provider->observe({std::chrono::milliseconds(30)}, {});
            MIRAGE_CHECK(!result.ok && result.error.code == "timeout" &&
                         result.input_methods.empty());
            MIRAGE_CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(2));
            return;
        }
        if (argc == 2 && std::string(argv[1]) == "--cancel") {
            const auto begin = std::chrono::steady_clock::now();
            desktop::CancelToken cancel([&] {
                return std::chrono::steady_clock::now() - begin > std::chrono::milliseconds(30);
            });
            auto result = env->provider->observe({}, cancel);
            MIRAGE_CHECK(!result.ok && result.cancelled && result.input_methods.empty());
            MIRAGE_CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(2));
            return;
        }
        for (const int timeout : {0, -1, 2001}) {
            auto result = env->provider->observe({std::chrono::milliseconds(timeout)}, {});
            MIRAGE_CHECK(!result.ok && result.error.code == "invalid_argument");
        }
        desktop::CancelToken cancelled;
        cancelled.request_cancel();
        MIRAGE_CHECK(env->provider->observe({}, cancelled).cancelled);
        integration::DesktopToolContext desktop;
        desktop.environment = env;
        desktop.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        const auto epoch = desktop.actions->epoch();
        mira::BuiltinToolRegistry registry;
        integration::DesktopAtomToolset::register_harness_tools(registry, desktop, false, {}, {});
        auto tools = registry.exposed_tools();
        MIRAGE_CHECK(tools.size() == 1 && tools[0].wire_name == "desktop.input.state");
        if (tools.empty())
            return;
        MIRAGE_CHECK(!tools[0].has_side_effects && tools[0].version.major == 1 &&
                     tools[0].version.minor == 0 && tools[0].version.patch == 0);
        mira::ToolProposal proposal;
        proposal.provider_call_id = mira::ProviderToolCallId{"ime-test"};
        proposal.tool_id = tools[0].tool_id;
        proposal.wire_name = tools[0].wire_name;
        proposal.tool_version = tools[0].version;
        proposal.arguments = mira::JsonValue{mira::JsonValue::Object{}};
        proposal.arguments_digest = mira::canonical_json_digest(proposal.arguments);
        proposal.operation_id = mira::OperationId::generate();
        mira::OperationContext ctx;
        auto result = registry.execute(proposal, ctx);
        MIRAGE_CHECK(result && !result.value().failed);
        MIRAGE_CHECK(desktop.actions->epoch() == epoch);
        if (result && !result.value().failed)
            std::cout << mira::canonical_json_string(result.value().result) << '\n';
    });
    work.get();
    executor.shutdown(true);
    return testing::finish("input_method_state_test");
}
