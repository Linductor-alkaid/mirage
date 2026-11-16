// Opt-in paid acceptance, never a CTest: two genuine recovery requests through
// the production ModelLayer/provider/transport, controlled files, private DB.
#include "../support/workflow_recovery_fixture.hpp"
#include <mira/model_schema.hpp>
#include <mira/workflow_learning.hpp>
#include <mira/workflow_tools.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/integration/workflow_learning_store.hpp>
#include <mirage/platform/credential_store.hpp>
#include <mirage/runtime/persistence/paths.hpp>
#include <mirage/runtime/persistence/settings.hpp>

#include <fstream>

namespace {
namespace persistence = mirage::runtime::persistence;
class Telemetry final : public mira::IModelProvider {
    std::shared_ptr<mira::IModelProvider> inner_;

  public:
    unsigned calls = 0;
    int domain_code = 0;
    int local_parse_outcome = -1, patch_error_code = 0;
    bool schema_valid = false;
    mira::TransportTrace trace;
    explicit Telemetry(std::shared_ptr<mira::IModelProvider> p) : inner_(std::move(p)) {}
    const mira::ModelProfile &profile() const override { return inner_->profile(); }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &r,
                                            const mira::OperationContext &c,
                                            const mira::ProviderInferOptions &o) override {
        if (++calls > 2) {
            mira::Error e;
            e.code = mira::ErrorCode::ResourceExhausted;
            e.safe_message = "acceptance model-call budget exhausted";
            return e;
        }
        auto result = inner_->infer(r, c, o);
        if (!result)
            domain_code = result.error().domain_code;
        if (result) {
            auto local = r;
            local.output_contract.mode = mira::OutputMode::StrictJsonSchema;
            const auto parsed = mira::parse_decision(local, result.value());
            local_parse_outcome = static_cast<int>(parsed.outcome);
            schema_valid = parsed.outcome == mira::DecisionParseOutcome::Decision;
            if (schema_valid && parsed.decision) {
                const auto *entries = parsed.decision->value.find("patch_entries");
                if (entries) {
                    const auto patch = mira::parse_workflow_patch_entries(*entries);
                    if (!patch)
                        patch_error_code = patch.error().domain_code;
                    else
                        for (const auto &entry : patch.value()) {
                            const auto valid = mira::validate_workflow_patch_entry(entry);
                            if (!valid)
                                patch_error_code = valid.error().domain_code;
                        }
                }
            }
        }
        trace = inner_->last_trace();
        return result;
    }
    const mira::TransportTrace &last_trace() const override { return inner_->last_trace(); }
    const mira::SseStreamStats &last_sse_stats() const override { return inner_->last_sse_stats(); }
};
struct CloseLayer {
    Fixture &fixture;
    i::ModelLayer &layer;
    ~CloseLayer() {
        fixture.host.shutdown_workflow_surface();
        fixture.host.shutdown();
        layer.shutdown();
    }
};
int run(const std::filesystem::path &report_path, const std::filesystem::path &state) {
    std::ifstream input(persistence::default_config_directory() / "service.json", std::ios::binary);
    std::string text(persistence::kMaxSettingsFileBytes + 1, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    if (text.empty() || text.size() > persistence::kMaxSettingsFileBytes)
        throw std::runtime_error("configured model settings are unavailable or oversized");
    const auto settings = persistence::decode_settings(text);
    if (!settings.ok)
        throw std::runtime_error("configured model settings are invalid");
    std::optional<persistence::ModelSettings> selected;
    const auto supported = [](const auto &m) {
        return m.enabled &&
               (m.dialect == "openai.chat-completions.v1" || m.dialect == "openai.responses.v1" ||
                m.dialect == "anthropic.messages.v1");
    };
    if (settings.settings.model && supported(*settings.settings.model))
        selected = settings.settings.model;
    if (!selected)
        for (const auto &m : settings.settings.models)
            if (supported(m)) {
                selected = m;
                break;
            }
    if (!selected)
        throw std::runtime_error("no configured model supports the recovery schema dialect");

    i::WorkflowLearningStore *storage = nullptr;
    Fixture fixture(
        {}, false,
        [&](kairo::Executor &executor) -> std::shared_ptr<mira::IMemory> {
            std::string error;
            auto unique = i::WorkflowLearningStore::open(executor, state, 64ULL << 20, error);
            if (!unique)
                throw std::runtime_error(error);
            auto owner = std::shared_ptr<i::WorkflowLearningStore>(std::move(unique));
            storage = owner.get();
            const auto memory = owner->binding()->memory;
            return std::shared_ptr<mira::IMemory>(std::move(owner), memory.get());
        },
        false, "mirage.workflow.local-user.v1");
    i::ModelLayerConfig config;
    config.enabled = config.workflow_recovery_only = true;
    config.dialect = selected->dialect;
    config.endpoint_origin = selected->endpoint_origin;
    config.api_prefix = selected->api_prefix;
    config.model_selector = selected->model_selector;
    config.display_name = "Workflow isolated online acceptance";
    config.credential_ref = selected->credential_ref;
    config.credential_env = selected->credential_env;
    config.context_window_tokens = selected->context_window_tokens;
    config.request_deadline = 30s;
    config.max_output_tokens = 2048;
    config.credential_lookup = [](const std::string &reference) -> std::optional<std::string> {
        const auto result = mirage::platform::read_credential(reference);
        return result.ok ? std::optional{result.value} : std::nullopt;
    };
    std::vector<mira::ModelUsage> usage;
    config.usage_observer = [&](const auto &sample) { usage.push_back(sample); };
    i::ModelLayer layer(fixture.executor.value, config);
    CloseLayer close{fixture, layer};
    auto model = layer.workflow_recovery_model();
    if (!model)
        throw std::runtime_error("production Workflow model could not be assembled");
    const auto telemetry = std::make_shared<Telemetry>(model->provider);
    model->provider = telemetry;
    model->gateway->set_event_store(fixture.events, mira::RuntimeId::generate(),
                                    *mira::SessionId::parse(fixture.host.primary_session().id));
    const auto patch_path = std::make_shared<std::string>();
    model->project_parameters = [patch_path](const Json &) {
        // Explicitly whitelisted synthetic task contract. No user content or
        // the fixture's private-parameter-marker is projected to the model.
        return Json{Json::Object{
            {"step_arguments_patch_path", *patch_path},
            {"step_arguments_value",
             Json::Object{{"tool", "desktop.filesystem.read_text"}, {"path", "/fixture/good"}}},
            {"task_contract",
             "The authorized task is to read the synthetic file /fixture/good with tool "
             "desktop.filesystem.read_text. The saved ToolCall's path is wrong. Repair the "
             "current step using a step_arguments set patch with value "
             "{tool: desktop.filesystem.read_text, path: /fixture/good}, then resume. "
             "The patch path is exactly step_arguments_patch_path (the raw step ID), "
             "not a JSON Pointer. Use step_arguments_value as the value object unchanged. "
             "Cite any adopted recovery lesson using its lesson_id. Do not skip the step."}}};
    };
    r::WorkflowRecoveryOptions recovery_options;
    recovery_options.model_call_deadline = 30s;
    if (config.dialect == "anthropic.messages.v1") {
        recovery_options.decision_output_mode = "text";
        recovery_options.thinking = "none";
    }
    ok(fixture.host.attach_workflow_recovery(model, recovery_options));
    const auto started_at = std::chrono::steady_clock::now();
    Json::Array rounds;
    bool passed = true;
    Run source;
    for (unsigned n = 0; n < 2 && passed; ++n) {
        const auto run = n == 0 ? fixture.waiting() : fixture.start_same(source);
        source = run;
        *patch_path = run.step;
        fixture.state(run.id, "waiting_agent");
        const auto end = std::chrono::steady_clock::now() + 40s;
        r::HostOutcome admitted;
        do {
            admitted = fixture.host.start_workflow_recovery(run.id);
            if (!admitted.ok)
                std::this_thread::sleep_for(1ms);
        } while (!admitted.ok && std::chrono::steady_clock::now() < end);
        if (!admitted.ok)
            throw std::runtime_error("online recovery admission did not settle");
        r::WorkflowRecoveryPollResult settled;
        do {
            settled = fixture.host.poll_workflow_recovery(run.id);
            if (!settled.ok)
                throw std::runtime_error("online recovery poll failed");
            if (!settled.settled)
                std::this_thread::sleep_for(1ms);
        } while (!settled.settled && std::chrono::steady_clock::now() < end);
        passed = settled.settled && settled.attempt.outcome == "patched-and-resumed";
        bool learned = false;
        if (passed) {
            fixture.state(run.id, "completed");
            r::WorkflowRecoveryLessonResult written;
            do {
                written = fixture.host.record_workflow_recovery_lesson(run.id);
                if (!written.ok && written.error.code == "invalid_state")
                    std::this_thread::sleep_for(1ms);
            } while (!written.ok && written.error.code == "invalid_state" &&
                     std::chrono::steady_clock::now() < end);
            learned = written.ok;
            passed = learned;
        }
        std::uint32_t kept = 0;
        const auto page = take(fixture.events->read(
            {*mira::SessionId::parse(fixture.host.primary_session().id), std::nullopt, 1000}));
        for (const auto &e : page.events)
            if (e.payload.type == "WorkflowRecoveryAttempted") {
                const auto audit = take(mira::parse_workflow_recovery_attempted(e.payload));
                if (audit.run_id.to_string() == run.id)
                    kept = audit.lessons_kept;
            }
        if (n == 1)
            passed = passed && kept > 0;
        rounds.emplace_back(
            Json::Object{{"run_id", run.id},
                         {"round", static_cast<std::int64_t>(n + 1)},
                         {"outcome", settled.settled ? settled.attempt.outcome : "unsettled"},
                         {"reason_code", settled.attempt.reason_code},
                         {"lesson_recorded", learned},
                         {"lessons_in_model_context", static_cast<std::int64_t>(kept)}});
    }
    Json::Array samples;
    for (const auto &sample : usage)
        samples.emplace_back(Json::Object{
            {"input_tokens", static_cast<std::int64_t>(sample.input_tokens.value_or(0))},
            {"output_tokens", static_cast<std::int64_t>(sample.output_tokens.value_or(0))}});
    const auto capacity = take(storage->capacity_snapshot());
    Json report{Json::Object{
        {"schema", "mirage.workflow.online-acceptance.v1"},
        {"passed", passed},
        {"model", config.model_selector},
        {"dialect", config.dialect},
        {"scope", "controlled Host + production ModelLayer, no real desktop"},
        {"rounds", std::move(rounds)},
        {"provider_usage", std::move(samples)},
        {"provider_calls", static_cast<std::int64_t>(telemetry->calls)},
        {"provider_domain_code", telemetry->domain_code},
        {"local_parse_outcome", telemetry->local_parse_outcome},
        {"schema_valid", telemetry->schema_valid},
        {"patch_error_code", telemetry->patch_error_code},
        {"write_started", telemetry->trace.write_started},
        {"write_completed", telemetry->trace.write_completed},
        {"headers_received", telemetry->trace.headers_received},
        {"file_bytes", static_cast<std::int64_t>(capacity.file_bytes)},
        {"file_limit_bytes", static_cast<std::int64_t>(capacity.limit_bytes)},
        {"duration_ms",
         static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - started_at)
                                       .count())}}};
    std::ofstream output(report_path, std::ios::binary | std::ios::trunc);
    output << mira::canonical_json_string(report) << '\n';
    if (!output)
        throw std::runtime_error("acceptance report write failed");
    std::cout << (passed ? "online Workflow learning passed\n"
                         : "online Workflow learning failed\n");
    return passed ? 0 : 1;
}
int inspect(const std::filesystem::path &report_path, const std::filesystem::path &state) {
    std::ifstream input(report_path, std::ios::binary);
    std::string text(64 * 1024 + 1, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    if (text.empty() || text.size() > 64 * 1024)
        return 2;
    auto decoded = mira::parse_json(text);
    if (!decoded || !decoded.value().find("passed") ||
        decoded.value().find("passed")->as_boolean() != std::optional<bool>{true})
        return 2;
    auto report = std::move(decoded.value());
    const auto *rounds = report.find("rounds");
    if (!rounds || !rounds->as_array() || rounds->as_array()->size() != 2)
        return 2;
    ExecutorOwner executor;
    std::string error;
    auto owner = i::WorkflowLearningStore::open(executor.value, state, 64ULL << 20, error);
    if (!owner)
        return 2;
    const auto binding = owner->binding();
    const mira::MemoryScope scope{mira::MemoryScopeKind::Agent, binding->agent_subject_id,
                                  std::nullopt};
    for (const auto &round : *rounds->as_array()) {
        const auto *id = round.find("run_id");
        if (!id || !id->as_string())
            return 2;
        const auto &run_id = *id->as_string();
        const auto episode = binding->memory->get(mira::workflow_episode_memory_id(run_id), scope);
        const auto lesson = binding->memory->get(mira::recovery_lesson_memory_id(run_id), scope);
        if (!episode || !episode.value() || !lesson || !lesson.value())
            return 2;
        if (episode.value()->kind != mira::MemoryKind::Episode)
            return 2;
        const auto episode_json = mira::parse_json(episode.value()->statement);
        if (!episode_json)
            return 2;
        const auto fact = mira::workflow_episode_from_json(episode_json.value());
        if (!fact || fact.value().run_id != run_id || fact.value().outcome != "completed")
            return 2;
        const auto parsed = mira::recovery_lesson_from_record(*lesson.value());
        if (!parsed || parsed.value().recovered_run_id != run_id)
            return 2;
    }
    const auto capacity = take(owner->capacity_snapshot());
    if (!owner->close(error))
        return 2;
    report.set("reopened_in_second_process", true);
    report.set("durable_episode_count", 2);
    report.set("durable_lesson_count", 2);
    report.set("reopened_file_bytes", static_cast<std::int64_t>(capacity.file_bytes));
    std::ofstream output(report_path, std::ios::binary | std::ios::trunc);
    output << mira::canonical_json_string(report) << '\n';
    if (!output)
        return 2;
    std::cout << "second-process durable learning inspection passed\n";
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 3 && !(argc == 4 && std::string_view(argv[1]) == "--inspect")) {
        std::cerr << "usage: workflow_learning_online_acceptance REPORT PRIVATE_STATE_DIRECTORY\n";
        return 2;
    }
    try {
        const int result =
            argc == 4
                ? inspect(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]))
                : run(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
        return result != 0 ? result : (mirage::testing::failure_count() == 0 ? 0 : 1);
    } catch (const std::exception &) {
        // No backend/credential/request/response body is printed.
        std::cerr
            << "online Workflow acceptance could not complete; inspect setup and bounded report\n";
        return 2;
    }
}
