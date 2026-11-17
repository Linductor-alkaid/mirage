#include "../support/workflow_recovery_fixture.hpp"
#include <mira/workflow_learning.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/integration/workflow_learning_store.hpp>
#include <mirage/runtime/persistence/settings.hpp>

#include <filesystem>

namespace {
class Directory {
  public:
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("mirage-persistent-learning-" + mira::Id128::generate().to_string());
    Directory() { std::filesystem::create_directory(path); }
    ~Directory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
struct CloseModel {
    Fixture &f;
    i::ModelLayer &model;
    ~CloseModel() {
        f.host.shutdown_workflow_surface();
        f.host.shutdown();
        model.shutdown();
    }
};
void persistent_owner_and_closed_facade() {
    ExecutorOwner executor;
    Directory directory;
    std::string error;
    MIRAGE_CHECK(!i::WorkflowLearningStore::open(executor.value, {}, 1ULL << 20, error));
    MIRAGE_CHECK(!error.empty());
    auto owner = i::WorkflowLearningStore::open(executor.value, directory.path, 1ULL << 20, error);
    MIRAGE_CHECK(owner && error.empty());
    if (!owner)
        throw std::runtime_error("persistent owner fixture failed");
    const auto binding = owner->binding();
    MIRAGE_CHECK(binding->agent_subject_id == "mirage.workflow.local-user.v1");
    const mira::MemoryScope scope{mira::MemoryScopeKind::Agent, binding->agent_subject_id,
                                  std::nullopt};
    const auto run_id = mira::WorkflowRunId::generate().to_string();
    mira::WorkflowEpisodeRecord episode;
    episode.run_id = run_id;
    episode.workflow_id = mira::WorkflowId::generate().to_string();
    episode.ir_digest = std::string(64, '1');
    episode.policy = "strict";
    episode.outcome = "completed";
    episode.recorded_at_ms = 1;
    mira::MemoryMutation mutation;
    mutation.id = mira::workflow_episode_mutation_id(run_id);
    mutation.type = mira::MemoryMutationType::Add;
    mutation.scope = scope;
    mutation.evidence = {mira::EventId::generate()};
    mutation.proposed = mira::episode_to_memory_record(episode, scope, mutation.evidence,
                                                       std::chrono::system_clock::now());
    MIRAGE_CHECK(binding->memory->apply(mutation));
    const auto snapshot = take(owner->capacity_snapshot());
    MIRAGE_CHECK(snapshot.file_bytes > 0 && snapshot.file_bytes <= snapshot.limit_bytes);
    MIRAGE_CHECK(owner->close(error));
    MIRAGE_CHECK(!binding->memory->get(mutation.proposed.id, scope));
    MIRAGE_CHECK(!owner->capacity_snapshot());
    MIRAGE_CHECK(owner->close(error));
    owner = i::WorkflowLearningStore::open(executor.value, directory.path, 1ULL << 20, error);
    MIRAGE_CHECK(owner);
    const auto reopened = owner->binding();
    MIRAGE_CHECK(take(reopened->memory->get(mutation.proposed.id, scope)));
    const mira::MemoryScope other{mira::MemoryScopeKind::Agent, "other-principal", std::nullopt};
    MIRAGE_CHECK(!take(reopened->memory->get(mutation.proposed.id, other)));
    MIRAGE_CHECK(owner->close(error));
}
void production_stack_exclusive_mode() {
    Fixture f({}, false, {}, false);
    i::ModelLayerConfig config;
    config.enabled = config.workflow_recovery_only = true;
    config.model_selector = "controlled-production-stack";
    config.max_output_tokens = 2048;
    const auto provider = std::make_shared<Provider>();
    i::ModelProviderOverride override([&](const auto &profile) {
        provider->configured = profile;
        return provider;
    });
    i::ModelLayer layer(f.executor.value, config, &override);
    CloseModel close{f, layer};
    auto model = layer.workflow_recovery_model();
    MIRAGE_CHECK(model);
    if (!model)
        throw std::runtime_error("dedicated recovery stack missing");
    const auto blocked =
        layer.complete_dialog_turn("", "must not infer", mira::make_control_context());
    MIRAGE_CHECK(blocked.failed && provider->calls == 0);
    model->project_parameters = [](const Json &) {
        return Json::Object{{"safe_contract", "fixture"}};
    };
    ok(f.host.attach_workflow_recovery(model));
    const auto run = f.waiting();
    provider->decision = patch(run.step);
    f.start_recovery(run.id);
    MIRAGE_CHECK(f.settled(run.id).outcome == "patched-and-resumed");
    f.state(run.id, "completed");
    MIRAGE_CHECK(provider->last_text.find("safe_contract") != std::string::npos);
    MIRAGE_CHECK(provider->last_text.find("private-parameter-marker") == std::string::npos);
    MIRAGE_CHECK(provider->calls == 1 && f.files.successes == 1);
}
void settings_switch() {
    namespace p = mirage::runtime::persistence;
    auto absent = p::decode_settings(R"({"schema":1,"runtime":{}})");
    MIRAGE_CHECK(absent.ok && absent.settings.runtime &&
                 absent.settings.runtime->workflow_learning_enabled);
    auto disabled =
        p::decode_settings(R"({"schema":1,"runtime":{"workflow_learning_enabled":false}})");
    MIRAGE_CHECK(disabled.ok && disabled.settings.runtime &&
                 !disabled.settings.runtime->workflow_learning_enabled);
    const auto roundtrip = p::decode_settings(p::encode_settings(disabled.settings));
    MIRAGE_CHECK(roundtrip.ok && roundtrip.settings.runtime &&
                 !roundtrip.settings.runtime->workflow_learning_enabled);
    MIRAGE_CHECK(
        !p::decode_settings(R"({"schema":1,"runtime":{"workflow_learning_enabled":"false"}})").ok);
}
} // namespace
int main() {
    try {
        persistent_owner_and_closed_facade();
        production_stack_exclusive_mode();
        settings_switch();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return mirage::testing::finish("workflow_learning_store_test");
}
