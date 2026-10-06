#include "../support/ipc_io.hpp"
#include "runtime_bridge.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <executor/comm.hpp>
#include <executor/executor.hpp>
#include <fstream>
#include <map>
#include <mira/json.hpp>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/persistence/session_state.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/persistence/store.hpp>
#include <mirage/runtime/runtime_service.hpp>
#include <mutex>
#include <poll.h>

namespace ipc = mirage::runtime::ipc;
namespace integration = mirage::integration;
namespace persistence = mirage::runtime::persistence;
using namespace std::chrono_literals;
class HarnessEnvironment final : public mirage::desktop::DesktopEnvironment {
  public:
    mirage::desktop::EnvironmentInfo info() const override { return {"harness", "test"}; }
};
class Provider final : public mira::IModelProvider {
  public:
    explicit Provider(const mira::ModelProfile &profile) : profile_(profile) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &context,
                                            const mira::ProviderInferOptions &options) override {
        std::string input_text;
        for (const auto &item : request.input)
            for (const auto &part : item.content)
                if (const auto *text = std::get_if<mira::TextPart>(&part))
                    input_text += text->text + "\n";
        wire.publish(std::move(input_text));
        const int number = ++calls;
        last_tools.store(static_cast<int>(request.tools.size()));
        saw_high.store(request.generation.reasoning_effort == mira::ReasoningEffort::High);
        bool image = false;
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                if (const auto *screen = std::get_if<mira::ImagePart>(&part)) {
                    image = true;
                    (void)screen;
                }
            }
        saw_image.store(image);
        if (options.stream && options.preview_sink) {
            options.preview_sink(request.request_id, {});
            options.preview_sink(request.request_id, {"Mira 正在", 0, false});
            options.preview_sink(request.request_id, {"Mira 正在增量回复", 0, false});
        }
        while (hold.load() && !context.cancelled_or_expired(mira::Timestamp::now()))
            (void)::poll(nullptr, 0, 1);
        mira::ModelResponse reply;
        reply.contract_version = {1, 0};
        reply.request_id = request.request_id;
        reply.operation_id = request.operation_id;
        reply.profile_id = profile_.id;
        reply.requested_model = profile_.model_selector;
        if (report_usage.load()) {
            reply.usage.quality = mira::UsageQuality::ProviderReported;
            reply.usage.input_tokens = static_cast<std::uint64_t>(number * 1234);
            reply.usage.output_tokens = 100;
        }
        reply.status = context.cancelled_or_expired(mira::Timestamp::now())
                           ? mira::ModelCompletionStatus::Cancelled
                           : mira::ModelCompletionStatus::Completed;
        if (throw_now.load())
            throw std::runtime_error("scripted failure");
        if (reply.status == mira::ModelCompletionStatus::Cancelled)
            return reply;
        if (bad_tool.load()) {
            mira::ToolCallOutput bad;
            bad.provider_call_id.value = "bad";
            bad.provider_name = "not_exposed";
            bad.arguments = mira::parse_json("{}").value();
            bad.arguments_digest = mira::canonical_json_digest(bad.arguments);
            reply.output.emplace_back(std::move(bad));
            return reply;
        }
        if ((number == 1 && tool_first.load()) || always_tool.load()) {
            for (const auto &tool : request.tools)
                if (tool.wire_name == "wait") {
                    mira::ToolCallOutput call;
                    call.provider_call_id.value = "call-1";
                    call.tool_id = tool.tool_id;
                    call.provider_name = tool.wire_name;
                    call.arguments = mira::parse_json(R"({"duration_ms":1})").value();
                    call.arguments_digest = mira::canonical_json_digest(call.arguments);
                    reply.output.emplace_back(std::move(call));
                    return reply;
                }
        }
        if (number > 1)
            for (const auto &item : request.input)
                if (std::any_of(item.content.begin(), item.content.end(), [](const auto &part) {
                        return std::holds_alternative<mira::ToolResultPart>(part);
                    }))
                    saw_tool_result.store(true);
        mira::MessageOutput message;
        mira::OutputTextPart text;
        text.text = old_reply.load() ? "旧末轮专属回复" : "Mira 已完成通用工具调用。";
        message.content.emplace_back(std::move(text));
        reply.output.emplace_back(std::move(message));
        return reply;
    }
    executor::comm::LatestMailbox<std::string> wire{"test-model-wire"};
    mira::ModelProfile profile_;
    std::atomic_int calls{0}, last_tools{0};
    std::atomic_bool saw_high{false}, old_reply{false};
    std::atomic_bool report_usage{true};
    std::atomic_bool hold{false}, tool_first{true}, saw_image{false}, saw_tool_result{false},
        throw_now{false}, always_tool{false}, bad_tool{false};
};
std::optional<ipc::DialogTurnEntry> wait_turn(ipc::IpcClient &client, const std::string &session) {
    const auto end = std::chrono::steady_clock::now() + 8s;
    while (std::chrono::steady_clock::now() < end) {
        const auto response = client.call(ipc::ChatHistoryRequest{session, 40}, 2s);
        if (response.ok)
            if (const auto *history = std::get_if<ipc::DialogHistory>(&response.payload))
                if (!history->turns.empty() && history->turns.back().status != "pending")
                    return history->turns.back();
        (void)::poll(nullptr, 0, 1);
    }
    return {};
}
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--probe-live") {
        auto read = [](const char *name) {
            const auto *v = std::getenv(name);
            return v ? std::string(v) : std::string{};
        };
        integration::ModelLayerConfig live;
        live.enabled = true;
        live.endpoint_origin = read("MIRAGE_PROBE_ORIGIN");
        live.api_prefix = read("MIRAGE_PROBE_PREFIX");
        live.model_selector = read("MIRAGE_PROBE_MODEL");
        live.dialect = read("MIRAGE_PROBE_DIALECT");
        live.credential_env = "MIRAGE_PROBE_KEY";
        live.request_deadline = 60s;
        executor::Executor owner;
        executor::ExecutorConfig limits;
        limits.min_threads = limits.max_threads = 4;
        limits.queue_capacity = 16;
        if (!owner.initialize_ex(limits))
            return 1;
        integration::DialogCompletion result;
        {
            integration::ModelLayer model(owner, live);
            mira::OperationContext context;
            context.operation = mira::OperationId::generate();
            context.started_at = mira::Timestamp::now();
            context.deadline = std::chrono::steady_clock::now() + 60s;
            std::size_t updates = 0, largest = 0;
            bool before_terminal = false;
            result = model.complete_harness_turn({}, read("MIRAGE_PROBE_PROMPT"), context, "", true,
                                                 [&](const auto &, const auto &text, bool) {
                                                     if (!text.empty()) {
                                                         ++updates;
                                                         largest = std::max(largest, text.size());
                                                         before_terminal = true;
                                                     }
                                                 });
            std::printf("stream_updates=%zu largest_snapshot=%zu delivered_before_return=%d\n",
                        updates, largest, before_terminal);
            model.shutdown();
        }
        owner.shutdown(true);
        std::printf("ok=%d steps=%u tools=%u\n", result.ok, result.model_steps, result.tool_calls);
        std::printf("%s\n", result.ok ? result.reply_text.c_str() : result.error.c_str());
        return result.ok ? 0 : 1;
    }
    // M6-22: Messages is a real pinned dialect; unsupported thinking fails at
    // configuration validation, before admitting a model request.
    {
        integration::ModelLayerConfig config;
        config.enabled = true;
        config.model_selector = "fixture";
        config.endpoint_origin = "https://api.example.test";
        config.dialect = "anthropic.messages.v1";
        std::string error;
        MIRAGE_CHECK(config.valid(error));
        config.supports_reasoning = true;
        MIRAGE_CHECK(!config.valid(error) && error.find("thinking") != std::string::npos);
        config.supports_reasoning = false;
        config.dialect = "unknown.messages";
        MIRAGE_CHECK(!config.valid(error));
    }
    // Exercise the production transport admission path, without any network
    // request or credentials. Provider fixtures do not start blocking workers.
    {
        executor::Executor owner;
        executor::ExecutorConfig limits;
        limits.min_threads = limits.max_threads = 2;
        MIRAGE_CHECK(owner.initialize_ex(limits));
        integration::ModelLayerConfig live;
        live.enabled = true;
        live.endpoint_origin = "http://example.com";
        live.model_selector = "transport-lifecycle-fixture";
        {
            integration::ModelLayer active(owner, live);
            MIRAGE_CHECK(active.running());
            for (int i = 0; i < 3; ++i) {
                integration::ModelLayer replacement(owner, live);
                MIRAGE_CHECK(replacement.running());
                replacement.shutdown();
                MIRAGE_CHECK(!replacement.running() && active.running());
            }
            active.shutdown();
            MIRAGE_CHECK(!active.running());
        }
        owner.shutdown(true);
    }
    mirage::testing::TempDir dir;
    auto environment = std::make_shared<HarnessEnvironment>();
    auto binding = std::make_shared<integration::MiraEnvironmentBinding>(environment);
    std::shared_ptr<Provider> provider;
    mirage::runtime::ServiceConfig config;
    config.socket_path = (dir.root() / "service.sock").string();
    config.settings_directory = dir.root() / "settings";
    config.model.request_deadline = 300ms;
    config.model.max_output_tokens = 512;
    config.persist_recovery_state = false;
    config.persist_session_state = true;
    config.session_state_directory = dir.root() / "session-state";
    std::mutex key_mutex;
    std::map<std::string, std::string> keys;
    std::atomic_bool key_write_failure{false}, key_remove_failure{false};
    config.credential_write = [&](const std::string &reference, const std::string &value) {
        std::lock_guard lock(key_mutex);
        if ((key_write_failure.load() && !value.empty()) ||
            (key_remove_failure.load() && value.empty()))
            return mirage::runtime::CredentialWriteResult{false, "fixture keyring unavailable"};
        if (value.empty())
            keys.erase(reference);
        else
            keys[reference] = value;
        return mirage::runtime::CredentialWriteResult{true, {}};
    };
    config.model.credential_lookup =
        [&](const std::string &reference) -> std::optional<std::string> {
        std::lock_guard lock(key_mutex);
        const auto found = keys.find(reference);
        return found == keys.end() ? std::optional<std::string>{}
                                   : std::optional<std::string>{found->second};
    };

    persistence::LocalSettings initial;
    initial.read_roots = {"/example/read-only"};
    MIRAGE_CHECK(persistence::LocalStateStore(config.settings_directory, "service.json", 65536)
                     .save(persistence::encode_settings(initial))
                     .ok);
    config.model_provider_override = std::make_shared<integration::ModelProviderOverride>(
        [&provider](const mira::ModelProfile &profile) {
            provider = std::make_shared<Provider>(profile);
            return provider;
        });
    // Explicit manual GUI fixture; production binaries never expose this provider.
    if (argc == 2 && std::string(argv[1]) == "--serve-fixture") {
        config.socket_path = ipc::default_socket_path();
        config.model.enabled = true;
        config.model.model_selector = "UI-test-fixture";
        config.model.endpoint_origin = "https://fixture.example";
        config.model.request_deadline = 5s;
        mirage::runtime::RuntimeService fixture_service(config);
        if (!fixture_service.start(binding).ok)
            return 1;
        return fixture_service.run().clean ? 0 : 1;
    }
    mirage::runtime::RuntimeService service(config);
    MIRAGE_CHECK(service.start(binding).ok);
    ipc::IpcClient client(config.socket_path);
    const auto first_sessions = client.call(ipc::ListSessionsRequest{}, 2s);
    const auto primary_session =
        std::get<ipc::SessionList>(first_sessions.payload).sessions.front().id;
    persistence::LocalSettings settings;
    settings.model = persistence::ModelSettings{
        true,  "openai.responses.v1", "Agent fixture",  "https://fixture.example",
        "/v1", "test-model",          "MIRAGE_API_KEY", 32768};
    const auto serialized = persistence::encode_settings(settings);
    MIRAGE_CHECK(persistence::decode_settings(serialized).settings.model->context_window_tokens ==
                 32768);
    MIRAGE_CHECK(
        !persistence::decode_settings(R"({"schema":1,"model":{"context_window_tokens":-1}})").ok);
    MIRAGE_CHECK(
        !persistence::decode_settings(R"({"schema":1,"model":{"context_window_tokens":2000001}})")
             .ok);
    MIRAGE_CHECK(
        !persistence::decode_settings(R"({"schema":1,"model":{"context_window_tokens":"32768"}})")
             .ok);
    const auto roundtrip = ipc::decode_request(
        ipc::encode_request(9, ipc::SessionChatRequest{"session", "goal", true}));
    MIRAGE_CHECK(roundtrip.ok && std::get<ipc::SessionChatRequest>(roundtrip.body).agent);
    auto set = client.call(ipc::SetModelRequest{serialized}, 2s);
    MIRAGE_CHECK(set.ok && std::holds_alternative<ipc::ModelConfiguration>(set.payload));
    initial.model = settings.model;
    initial.model->api_key_configured = true;
    const auto expected_saved = persistence::encode_settings(initial);
    const auto loaded =
        persistence::LocalStateStore(config.settings_directory, "service.json", 65536).load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::Loaded && loaded.body == expected_saved);
    auto invalid = settings;
    invalid.model->dialect = "invalid";
    MIRAGE_CHECK(!client.call(ipc::SetModelRequest{persistence::encode_settings(invalid)}, 2s).ok);
    invalid = settings;
    invalid.model->credential_env = "secret with spaces";
    MIRAGE_CHECK(!client.call(ipc::SetModelRequest{persistence::encode_settings(invalid)}, 2s).ok);
    invalid = settings;
    invalid.model->endpoint_origin = "https://fixture.example/v1";
    MIRAGE_CHECK(!client.call(ipc::SetModelRequest{persistence::encode_settings(invalid)}, 2s).ok);
    MIRAGE_CHECK(persistence::LocalStateStore(config.settings_directory, "service.json", 65536)
                     .load()
                     .body == expected_saved);
    auto opened = client.call(ipc::OpenSessionRequest{}, 2s);
    MIRAGE_CHECK(opened.ok);
    const std::string session = std::get<ipc::SessionOpened>(opened.payload).session_id;
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "解释一个概念", true}, 2s).ok);
    auto turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "ok");
    if (turn)
        MIRAGE_CHECK(turn->reply_text == "Mira 已完成通用工具调用。");
    MIRAGE_CHECK(provider->calls.load() == 2 && !provider->saw_image.load() &&
                 provider->saw_tool_result.load());
    MIRAGE_CHECK(provider->profile_.capabilities.limits.max_context_tokens == 32768);
    MIRAGE_CHECK(turn && turn->context_usage && turn->context_usage->input_tokens == 2468);
    MIRAGE_CHECK(turn && turn->context_usage && turn->context_usage->window_tokens == 32768);
    if (turn) {
        ipc::ChatTurnUpdatedEvent update;
        update.session_id = session;
        update.turn_id = turn->turn_id;
        update.status = "ok";
        update.user_text = turn->user_text;
        update.reply_text = turn->reply_text;
        update.has_reply = true;
        update.sequence = turn->sequence;
        update.context_usage = turn->context_usage;
        ipc::Event envelope;
        envelope.seq = 1;
        envelope.payload = update;
        const auto json = ipc::encode_event(envelope);
        const auto decoded = ipc::decode_event(json);
        MIRAGE_CHECK(decoded.ok && std::get<ipc::ChatTurnUpdatedEvent>(decoded.event.payload)
                                           .context_usage->input_tokens == 2468);
        auto corrupt = json;
        const auto at = corrupt.find("\"input_tokens\":2468");
        MIRAGE_CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            corrupt.replace(at, std::string("\"input_tokens\":2468").size(), "\"input_tokens\":-1");
            MIRAGE_CHECK(!ipc::decode_event(corrupt).ok);
        }
        update.context_usage.reset();
        envelope.payload = update;
        MIRAGE_CHECK(ipc::decode_event(ipc::encode_event(envelope)).ok);
    }
    provider->old_reply.store(true);
    provider->report_usage.store(false);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "缺失Token用量", true}, 2s).ok);
    const auto missing_usage = wait_turn(client, session);
    MIRAGE_CHECK(missing_usage && missing_usage->status == "ok" && !missing_usage->context_usage);
    // DEC-039: replace the last settled pair, preserving earlier context.
    provider->old_reply.store(false);
    const auto obsolete = missing_usage->turn_id;
    const auto invalid_replacement = client.call(
        ipc::SessionChatRequest{session, "不会入库", true, "read_only", "", "not-the-latest"}, 2s);
    MIRAGE_CHECK(!invalid_replacement.ok && invalid_replacement.error.code == "invalid_state");
    const auto unchanged = client.call(ipc::ChatHistoryRequest{session, 40}, 2s);
    MIRAGE_CHECK(std::get<ipc::DialogHistory>(unchanged.payload).turns.back().turn_id == obsolete);
    const auto replacement = client.call(
        ipc::SessionChatRequest{session, "修改后的输入", true, "read_only", "", obsolete}, 2s);
    MIRAGE_CHECK(replacement.ok &&
                 std::get<ipc::DialogTurnAccepted>(replacement.payload).replaces_turn_id ==
                     obsolete);
    auto replaced = wait_turn(client, session);
    MIRAGE_CHECK(replaced && replaced->status == "ok" && replaced->turn_id != obsolete);
    std::string wire;
    MIRAGE_CHECK(provider->wire.try_load(wire));
    MIRAGE_CHECK(wire.find("修改后的输入") != std::string::npos &&
                 wire.find("缺失Token用量") == std::string::npos &&
                 wire.find("旧末轮专属回复") == std::string::npos &&
                 wire.find("解释一个概念") != std::string::npos);
    const auto replacement_history = client.call(ipc::ChatHistoryRequest{session, 40}, 2s);
    MIRAGE_CHECK(std::get<ipc::DialogHistory>(replacement_history.payload).turns.size() == 2);
    MIRAGE_CHECK(
        !client
             .call(ipc::SessionChatRequest{session, "过期编辑", true, "default", "", obsolete}, 2s)
             .ok);
    // Fail persistence before acknowledgement: old context remains intact.
    const auto state_directory = config.session_state_directory;
    const auto saved_directory = state_directory.string() + "-saved";
    std::filesystem::rename(state_directory, saved_directory);
    {
        std::ofstream blocker(state_directory);
        blocker << "fixture blocker";
    }
    const auto persist_refused = client.call(
        ipc::SessionChatRequest{session, "不应替换", true, "read_only", "", replaced->turn_id}, 2s);
    MIRAGE_CHECK(!persist_refused.ok && persist_refused.error.code == "unavailable");
    std::filesystem::remove(state_directory);
    std::filesystem::rename(saved_directory, state_directory);
    const auto after_failure = client.call(ipc::ChatHistoryRequest{session, 40}, 2s);
    MIRAGE_CHECK(std::get<ipc::DialogHistory>(after_failure.payload).turns.back().turn_id ==
                 replaced->turn_id);
    // Replacing a turn cannot restore an active or superseded task.
    provider->hold.store(true);
    const auto active_revision = client.call(
        ipc::SessionChatRequest{session, "活动修改", true, "read_only", "", replaced->turn_id}, 2s);
    MIRAGE_CHECK(active_revision.ok);
    MIRAGE_CHECK(!client
                      .call(ipc::SessionChatRequest{session, "并发修改", true, "read_only", "",
                                                    replaced->turn_id},
                            2s)
                      .ok);
    MIRAGE_CHECK(client.call(ipc::CancelChatRequest{session}, 2s).ok);
    MIRAGE_CHECK(wait_turn(client, session)->status == "failed");
    provider->hold.store(false);
    provider->report_usage.store(true);
    provider->bad_tool.store(true);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "非法提案", true}, 2s).ok);
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "failed");
    provider->bad_tool.store(false);
    provider->hold.store(true);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "等待停止", true}, 2s).ok);
    const auto busy = client.call(ipc::SetModelRequest{serialized}, 2s);
    MIRAGE_CHECK(!busy.ok && busy.error.code == "invalid_state");
    const auto active_delete = client.call(ipc::DeleteSessionRequest{session}, 2s);
    MIRAGE_CHECK(!active_delete.ok && active_delete.error.code == "invalid_state");
    MIRAGE_CHECK(client.call(ipc::CancelChatRequest{session}, 2s).ok);
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "failed");
    provider->hold.store(false);
    provider->throw_now.store(true);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "错误路径", true}, 2s).ok);
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "failed");
    provider->throw_now.store(false);
    provider->always_tool.store(true);
    const auto before_budget = provider->calls.load();
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "预算路径", true}, 2s).ok);
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "failed" && turn->error.find("turn") != std::string::npos);
    MIRAGE_CHECK(provider->calls.load() - before_budget == 16);
    provider->always_tool.store(false);
    provider->hold.store(true);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "超时路径", true}, 2s).ok);
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "failed" &&
                 turn->error.find("cancelled") != std::string::npos);
    provider->hold.store(false);
    MIRAGE_CHECK(
        !client.call(ipc::SessionChatRequest{session, "unsupported", true, "default", "high"}, 2s)
             .ok);
    MIRAGE_CHECK(
        !ipc::decode_request(
             R"({"v":1,"id":1,"op":"session.chat","session_id":"a","text":"b","access":"full"})")
             .ok);
    MIRAGE_CHECK(
        !ipc::decode_request(
             R"({"v":1,"id":1,"op":"session.chat","session_id":"a","text":"b","reasoning":"ultra"})")
             .ok);
    settings.model->supports_reasoning = true;
    settings.models = {*settings.model, *settings.model};
    settings.models.back().display_name = "Other fixture";
    settings.models.back().model_selector = "second-model";
    MIRAGE_CHECK(client.call(ipc::SetModelRequest{persistence::encode_settings(settings)}, 2s).ok);
    const auto catalog_response = client.call(ipc::GetModelRequest{}, 2s);
    const auto catalog = persistence::decode_settings(
        std::get<ipc::ModelConfiguration>(catalog_response.payload).settings_json);
    MIRAGE_CHECK(catalog.ok && catalog.settings.models.size() == 2 &&
                 catalog.settings.model->supports_reasoning);
    const auto saved_catalog = persistence::decode_settings(
        persistence::LocalStateStore(config.settings_directory, "service.json", 65536).load().body);
    MIRAGE_CHECK(saved_catalog.ok && saved_catalog.settings.models.size() == 2 &&
                 saved_catalog.settings.permission_rules == initial.permission_rules);
    provider->tool_first.store(false);
    MIRAGE_CHECK(client
                     .call(ipc::SessionChatRequest{session, "text attachment: deliberate content",
                                                   true, "read_only", "high"},
                           2s)
                     .ok);
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "ok" && provider->last_tools.load() == 0 &&
                 provider->saw_high.load());
    auto duplicates = settings;
    duplicates.models.back().display_name = duplicates.models.front().display_name;
    MIRAGE_CHECK(!persistence::decode_settings(persistence::encode_settings(duplicates)).ok);
    MIRAGE_CHECK(
        !client.call(ipc::SetModelRequest{persistence::encode_settings(duplicates)}, 2s).ok);
    auto many = settings;
    many.models.resize(13);
    MIRAGE_CHECK(!persistence::decode_settings(persistence::encode_settings(many)).ok);
    // Real subscription + UI bridge responses, independent of EUI/Chromium.
    std::atomic_int wakes{0};
    mirage::native_ui::RuntimeBridge bridge([&wakes] { ++wakes; }, config.socket_path);
    auto deadline = std::chrono::steady_clock::now() + 4s;
    mirage::native_ui::RuntimeMessage message;
    bool connected = false;
    while (std::chrono::steady_clock::now() < deadline && !connected) {
        if (bridge.receive(message))
            connected = message.kind == mirage::native_ui::RuntimeMessage::Kind::Connected;
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(connected);
    MIRAGE_CHECK(bridge.call(ipc::SubscribeEventsRequest{true}, "subscribe"));
    bool subscribed = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && !subscribed) {
        while (bridge.receive(message))
            if (message.tag == "subscribe" && message.response.ok)
                subscribed = true;
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(subscribed);
    // A pre-preview subscriber receives canonical events without unknown preview frames.
    mirage::native_ui::RuntimeBridge legacy([] {}, config.socket_path);
    bool legacy_ready = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && !legacy_ready) {
        while (legacy.receive(message))
            legacy_ready |= message.kind == mirage::native_ui::RuntimeMessage::Kind::Connected;
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(legacy_ready);
    MIRAGE_CHECK(legacy.call(ipc::SubscribeEventsRequest{}, "legacy.subscribe"));
    legacy_ready = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && !legacy_ready) {
        while (legacy.receive(message))
            legacy_ready |= message.tag == "legacy.subscribe" && message.response.ok;
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(legacy_ready);
    const auto attachment_path = (dir.root() / "attachment.txt").string();
    {
        std::ofstream file(attachment_path);
        file << "真实文本附件";
    }
    MIRAGE_CHECK(bridge.load_attachment(attachment_path, 7));
    bool attachment_loaded = false;
    deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline && !attachment_loaded) {
        while (bridge.receive(message))
            if (message.kind == mirage::native_ui::RuntimeMessage::Kind::Attachment)
                attachment_loaded = message.local_id == 7 && message.attachment.attachment &&
                                    message.attachment.attachment->text == "真实文本附件";
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(attachment_loaded);

    // Frontend startup submits these together; the persistent client is single-outstanding.
    MIRAGE_CHECK(bridge.call(ipc::ListSessionsRequest{}, "burst.sessions"));
    MIRAGE_CHECK(bridge.call(ipc::GetModelRequest{}, "burst.model"));
    int burst_ok = 0;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && burst_ok < 2) {
        while (bridge.receive(message))
            if (message.tag.starts_with("burst.") && message.response.ok)
                ++burst_ok;
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(burst_ok == 2 && bridge.connected());
    const auto wake_before_activity = wakes.load();
    bridge.set_activity(true);
    deadline = std::chrono::steady_clock::now() + 500ms;
    while (std::chrono::steady_clock::now() < deadline && wakes.load() <= wake_before_activity)
        (void)::poll(nullptr, 0, 1);
    MIRAGE_CHECK(wakes.load() > wake_before_activity);
    bridge.set_activity(false);
    provider->hold.store(true);
    MIRAGE_CHECK(bridge.call(ipc::SessionChatRequest{session, "真实事件", true}, "send", 7));
    bool got_event = false, got_ack = false, got_preview = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && (!got_preview || !got_ack)) {
        while (bridge.receive(message)) {
            if (message.event &&
                std::holds_alternative<ipc::ChatTurnUpdatedEvent>(message.event->payload))
                got_event = true;
            if (message.event)
                if (const auto *preview =
                        std::get_if<ipc::ChatPreviewEvent>(&message.event->payload))
                    got_preview = got_preview || preview->text == "Mira 正在增量回复";
            if (message.tag == "send" && message.response.ok)
                got_ack = true;
        }
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(got_preview && got_ack);
    const auto pending_history = client.call(ipc::ChatHistoryRequest{session, 40}, 2s);
    MIRAGE_CHECK(pending_history.ok);
    if (pending_history.ok) {
        const auto &pending = std::get<ipc::DialogHistory>(pending_history.payload).turns.back();
        MIRAGE_CHECK(pending.status == "pending" && pending.reply_text.empty() &&
                     !pending.context_usage);
    }
    provider->hold.store(false);
    got_event = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && !got_event) {
        while (bridge.receive(message))
            if (message.event)
                if (const auto *turn_event =
                        std::get_if<ipc::ChatTurnUpdatedEvent>(&message.event->payload))
                    got_event = turn_event->status == "ok";
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(got_event && wakes.load() > 0);
    bool legacy_terminal = false;
    bool legacy_preview = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && !legacy_terminal) {
        while (legacy.receive(message)) {
            if (message.event) {
                legacy_preview |=
                    std::holds_alternative<ipc::ChatPreviewEvent>(message.event->payload);
                if (const auto *legacy_turn =
                        std::get_if<ipc::ChatTurnUpdatedEvent>(&message.event->payload))
                    legacy_terminal |= legacy_turn->status == "ok";
            }
        }
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(legacy_terminal && !legacy_preview);
    // Intentionally stop draining the UI inbox: overflow must be observable and recoverable.
    bool admitted = true;
    for (int batch = 0; batch < 12; ++batch) {
        for (int item = 0; item < 14; ++item)
            admitted = bridge.call(ipc::GetModelRequest{}, "overflow") && admitted;
        (void)::poll(nullptr, 0, 300);
    }
    MIRAGE_CHECK(admitted && bridge.take_gap());
    while (bridge.receive(message)) {
    }
    MIRAGE_CHECK(bridge.call(ipc::GetModelRequest{}, "recover"));
    bool recovered = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && !recovered) {
        while (bridge.receive(message))
            if (message.tag == "recover" && message.response.ok)
                recovered = true;
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(recovered && bridge.connected());
    bridge.shutdown();
    MIRAGE_CHECK(!bridge.load_attachment(attachment_path, 7));
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "ok");
    // Write-only keys: keys live in the injected OS store, never settings/get.
    const auto key_packet = ipc::SetModelRequest{serialized, "mirage-synthetic-api-key"};
    const auto key_roundtrip = ipc::decode_request(ipc::encode_request(18, key_packet));
    MIRAGE_CHECK(key_roundtrip.ok &&
                 std::get<ipc::SetModelRequest>(key_roundtrip.body).api_key == key_packet.api_key);
    MIRAGE_CHECK(!ipc::decode_request(
                      R"({"v":1,"id":1,"op":"model.set","settings":"{}","api_key":"bad key"})")
                      .ok);
    key_write_failure.store(true);
    MIRAGE_CHECK(!client.call(key_packet, 2s).ok);
    key_write_failure.store(false);
    auto key_response = client.call(key_packet, 2s);
    MIRAGE_CHECK(key_response.ok);
    auto key_document = std::get<ipc::ModelConfiguration>(key_response.payload).settings_json;
    auto keyed = persistence::decode_settings(key_document);
    MIRAGE_CHECK(keyed.ok && keyed.settings.model->credential_env.empty() &&
                 keyed.settings.model->credential_ref.size() == 32 &&
                 keyed.settings.model->api_key_configured);
    MIRAGE_CHECK(key_document.find("mirage-synthetic-api-key") == std::string::npos);
    persistence::LocalStateStore key_disk(config.settings_directory, "service.json", 65536);
    MIRAGE_CHECK(key_disk.load().body.find("mirage-synthetic-api-key") == std::string::npos);
    {
        std::lock_guard lock(key_mutex);
        MIRAGE_CHECK(keys.size() == 1 && keys.begin()->second == "mirage-synthetic-api-key");
    }
    const auto settings_barrier =
        config.settings_directory /
        ("service.json.tmp." + std::to_string(static_cast<long>(::getpid())));
    std::ofstream(settings_barrier) << "occupied";
    MIRAGE_CHECK(!client.call(ipc::SetModelRequest{key_document, "another-synthetic-key"}, 2s).ok);
    {
        std::lock_guard lock(key_mutex);
        MIRAGE_CHECK(keys.size() == 1 && keys.begin()->second == "mirage-synthetic-api-key");
    }
    std::filesystem::remove(settings_barrier);
    // Clearing requires an explicit empty write; absent keeps the reference.
    MIRAGE_CHECK(client.call(ipc::SetModelRequest{key_document}, 2s).ok);
    auto cleared = client.call(ipc::SetModelRequest{key_document, ""}, 2s);
    MIRAGE_CHECK(cleared.ok);
    {
        std::lock_guard lock(key_mutex);
        MIRAGE_CHECK(keys.empty());
    }
    MIRAGE_CHECK(!persistence::decode_settings(
                      std::get<ipc::ModelConfiguration>(cleared.payload).settings_json)
                      .settings.model->api_key_configured);
    // Cleanup failure is visible even though the new redacted configuration
    // has already been committed; the failed orphan is not silently claimed removed.
    auto cleanup_key = client.call(key_packet, 2s);
    MIRAGE_CHECK(cleanup_key.ok);
    const auto cleanup_document =
        std::get<ipc::ModelConfiguration>(cleanup_key.payload).settings_json;
    key_remove_failure.store(true);
    const auto cleanup_warning = client.call(ipc::SetModelRequest{cleanup_document, ""}, 2s);
    MIRAGE_CHECK(cleanup_warning.ok);
    const auto &cleanup_config = std::get<ipc::ModelConfiguration>(cleanup_warning.payload);
    MIRAGE_CHECK(!cleanup_config.warning.empty());
    MIRAGE_CHECK(!persistence::decode_settings(cleanup_config.settings_json)
                      .settings.model->api_key_configured);
    key_remove_failure.store(false);
    {
        std::lock_guard lock(key_mutex);
        MIRAGE_CHECK(keys.size() == 1);
        keys.clear(); // remove only the synthetic fixture's reported orphan
    }
    MIRAGE_CHECK(client.call(ipc::SetModelRequest{serialized}, 2s).ok);

    // DEC-042: a provider has multiple model identities but one connection/key.
    auto provider_settings = settings;
    provider_settings.models_present = true;
    provider_settings.model->provider_id = "provider-test";
    provider_settings.model->provider_name = "测试服务";
    provider_settings.models = {*provider_settings.model, *provider_settings.model};
    provider_settings.models[1].display_name = "provider-second";
    provider_settings.models[1].model_selector = "provider-second-model";
    const auto grouped =
        client.call(ipc::SetModelRequest{persistence::encode_settings(provider_settings),
                                         "provider-synthetic-key"},
                    2s);
    MIRAGE_CHECK(grouped.ok);
    auto grouped_settings = persistence::decode_settings(
        std::get<ipc::ModelConfiguration>(grouped.payload).settings_json);
    MIRAGE_CHECK(grouped_settings.ok && grouped_settings.settings.models.size() == 2 &&
                 grouped_settings.settings.model->provider_name == "测试服务");
    MIRAGE_CHECK(grouped_settings.settings.models[0].credential_ref ==
                     grouped_settings.settings.models[1].credential_ref &&
                 !grouped_settings.settings.models[1].credential_ref.empty());
    auto failed_group = grouped_settings.settings;
    failed_group.model->provider_name = "未提交服务名";
    std::ofstream(settings_barrier) << "occupied";
    MIRAGE_CHECK(!client
                      .call(ipc::SetModelRequest{persistence::encode_settings(failed_group),
                                                 "rejected-group-key"},
                            2s)
                      .ok);
    const auto retained_group = client.call(ipc::GetModelRequest{}, 2s);
    const auto retained_settings = persistence::decode_settings(
        std::get<ipc::ModelConfiguration>(retained_group.payload).settings_json);
    MIRAGE_CHECK(retained_settings.settings.model->provider_name == "测试服务" &&
                 retained_settings.settings.models[1].credential_ref ==
                     grouped_settings.settings.models[1].credential_ref);
    {
        std::lock_guard lock(key_mutex);
        MIRAGE_CHECK(keys.size() == 1);
    }
    std::filesystem::remove(settings_barrier);
    auto selected_group = grouped_settings.settings;
    selected_group.model = selected_group.models[1];
    MIRAGE_CHECK(
        client.call(ipc::SetModelRequest{persistence::encode_settings(selected_group)}, 2s).ok);
    selected_group.model->provider_name = "新服务名";
    selected_group.model->endpoint_origin = "https://renamed.test";
    auto renamed_group =
        client.call(ipc::SetModelRequest{persistence::encode_settings(selected_group)}, 2s);
    MIRAGE_CHECK(renamed_group.ok);
    const auto renamed_settings = persistence::decode_settings(
        std::get<ipc::ModelConfiguration>(renamed_group.payload).settings_json);
    MIRAGE_CHECK(renamed_settings.settings.models[0].provider_name == "新服务名" &&
                 renamed_settings.settings.models[0].endpoint_origin == "https://renamed.test");
    persistence::LocalSettings clear_catalog;
    clear_catalog.models_present = true;
    clear_catalog.model = persistence::ModelSettings{};
    const auto cleared_catalog =
        client.call(ipc::SetModelRequest{persistence::encode_settings(clear_catalog)}, 2s);
    MIRAGE_CHECK(cleared_catalog.ok);
    const auto cleared_settings = persistence::decode_settings(
        std::get<ipc::ModelConfiguration>(cleared_catalog.payload).settings_json);
    MIRAGE_CHECK(cleared_settings.ok && cleared_settings.settings.models_present &&
                 cleared_settings.settings.models.empty() &&
                 !cleared_settings.settings.model->enabled);
    {
        std::lock_guard lock(key_mutex);
        MIRAGE_CHECK(keys.empty());
    }
    const auto disk_cleared = persistence::decode_settings(key_disk.load().body);
    MIRAGE_CHECK(disk_cleared.ok && disk_cleared.settings.models_present &&
                 disk_cleared.settings.models.empty());
    MIRAGE_CHECK(client.call(ipc::SetModelRequest{serialized}, 2s).ok);

    // Delete is durable, active-safe and can clear the legacy primary chat.
    auto disposable_open = client.call(ipc::OpenSessionRequest{}, 2s);
    const auto disposable = std::get<ipc::SessionOpened>(disposable_open.payload).session_id;
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{disposable, "可删除的历史", true}, 2s).ok);
    MIRAGE_CHECK(wait_turn(client, disposable).has_value());
    const auto state_backup = dir.root() / "state-backup";
    std::filesystem::rename(config.session_state_directory, state_backup);
    std::ofstream(config.session_state_directory) << "directory blocked";
    MIRAGE_CHECK(!client.call(ipc::DeleteSessionRequest{disposable}, 2s).ok);
    MIRAGE_CHECK(client.call(ipc::ChatHistoryRequest{disposable, 40}, 2s).ok);
    std::filesystem::remove(config.session_state_directory);
    std::filesystem::remove(
        state_backup / ("session-state.json.tmp." + std::to_string(static_cast<long>(::getpid()))));
    std::filesystem::rename(state_backup, config.session_state_directory);
    const auto removed = client.call(ipc::DeleteSessionRequest{disposable}, 2s);
    MIRAGE_CHECK(removed.ok && std::holds_alternative<ipc::SessionDeleted>(removed.payload));
    MIRAGE_CHECK(ipc::decode_response(ipc::encode_response(removed)).ok);
    MIRAGE_CHECK(!client.call(ipc::ChatHistoryRequest{disposable, 40}, 2s).ok);
    MIRAGE_CHECK(!client.call(ipc::DeleteSessionRequest{disposable}, 2s).ok);
    MIRAGE_CHECK(
        client.call(ipc::SessionChatRequest{primary_session, "主会话旧历史", true}, 2s).ok);
    MIRAGE_CHECK(wait_turn(client, primary_session).has_value());
    MIRAGE_CHECK(client.call(ipc::DeleteSessionRequest{primary_session}, 2s).ok);
    const auto primary_history = client.call(ipc::ChatHistoryRequest{primary_session, 40}, 2s);
    MIRAGE_CHECK(primary_history.ok &&
                 std::get<ipc::DialogHistory>(primary_history.payload).turns.empty());
    const auto persisted_sessions = persistence::decode_session_state(
        persistence::LocalStateStore(config.session_state_directory, "session-state.json",
                                     1024 * 1024)
            .load()
            .body);
    MIRAGE_CHECK(persisted_sessions.ok &&
                 std::none_of(persisted_sessions.state.sessions.begin(),
                              persisted_sessions.state.sessions.end(),
                              [&](const auto &item) { return item.id == disposable; }));
    const auto active_provider = provider;
    const auto before_corrupt = client.call(ipc::GetModelRequest{}, 2s);
    persistence::LocalStateStore disk(config.settings_directory, "service.json", 65536);
    MIRAGE_CHECK(disk.save("{").ok);
    MIRAGE_CHECK(!client.call(ipc::SetModelRequest{serialized}, 2s).ok);
    const auto after_corrupt = client.call(ipc::GetModelRequest{}, 2s);
    MIRAGE_CHECK(after_corrupt.ok &&
                 std::get<ipc::ModelConfiguration>(before_corrupt.payload).settings_json ==
                     std::get<ipc::ModelConfiguration>(after_corrupt.payload).settings_json);
    MIRAGE_CHECK(disk.load().body == "{" && disk.save(expected_saved).ok);
    provider = active_provider;
    provider->hold.store(true);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "关闭路径", true}, 2s).ok);
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
    {
        mirage::runtime::RuntimeService restarted(config);
        MIRAGE_CHECK(restarted.start(binding).ok);
        ipc::IpcClient restored_client(config.socket_path);
        const auto restored_list = restored_client.call(ipc::ListSessionsRequest{}, 2s);
        const auto &restored_sessions = std::get<ipc::SessionList>(restored_list.payload).sessions;
        MIRAGE_CHECK(std::none_of(restored_sessions.begin(), restored_sessions.end(),
                                  [&](const auto &item) { return item.id == disposable; }));
        const auto restored_history =
            restored_client.call(ipc::ChatHistoryRequest{session, 40}, 2s);
        const auto &restored_turns = std::get<ipc::DialogHistory>(restored_history.payload).turns;
        MIRAGE_CHECK(
            !restored_turns.empty() &&
            std::none_of(restored_turns.begin(), restored_turns.end(),
                         [&obsolete](const auto &item) { return item.turn_id == obsolete; }));
        const auto restored_last = restored_turns.back().turn_id;
        MIRAGE_CHECK(restored_client.call(ipc::SetModelRequest{serialized}, 2s).ok);
        provider->hold.store(false);
        MIRAGE_CHECK(restored_client
                         .call(ipc::SessionChatRequest{session, "重启后编辑", true, "read_only", "",
                                                       restored_last},
                               2s)
                         .ok);
        const auto resumed = wait_turn(restored_client, session);
        MIRAGE_CHECK(resumed && resumed->status == "ok" && resumed->user_text == "重启后编辑");
        const auto rebound_list = restored_client.call(ipc::ListSessionsRequest{}, 2s);
        MIRAGE_CHECK(rebound_list.ok);
        const auto &rebound_sessions = std::get<ipc::SessionList>(rebound_list.payload).sessions;
        MIRAGE_CHECK(
            std::any_of(rebound_sessions.begin(), rebound_sessions.end(), [&](const auto &item) {
                return item.id == session && item.state != "failed";
            }));
        MIRAGE_CHECK(restored_client
                         .call(ipc::SessionChatRequest{session, "继续会话", true, "read_only"}, 2s)
                         .ok);
        const auto continued = wait_turn(restored_client, session);
        MIRAGE_CHECK(continued && continued->status == "ok");
        MIRAGE_CHECK(restored_client.call(ipc::DeleteSessionRequest{session}, 2s).ok);
        MIRAGE_CHECK(!restored_client.call(ipc::ChatHistoryRequest{session, 40}, 2s).ok);
        restarted.request_shutdown();
        MIRAGE_CHECK(restarted.run().clean);
    }
    mirage::native_ui::RuntimeBridge unavailable([] {}, (dir.root() / "absent.sock").string());
    deadline = std::chrono::steady_clock::now() + 2s;
    bool lost = false;
    while (std::chrono::steady_clock::now() < deadline && !lost) {
        if (unavailable.receive(message))
            lost = message.kind == mirage::native_ui::RuntimeMessage::Kind::Lost;
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(lost && !unavailable.call(ipc::HelloRequest{}, "refused"));
    unavailable.shutdown();
    // Force the finite model driver admission path to refuse before model work starts.
    executor::Executor limited;
    executor::ExecutorConfig small;
    small.min_threads = small.max_threads = 1;
    small.queue_capacity = 1;
    small.max_in_flight_tasks = 1;
    MIRAGE_CHECK(static_cast<bool>(limited.initialize_ex(small)));
    std::atomic_bool blocker_started{false};
    auto blocked = limited.submit_cancellable([&blocker_started](executor::StopToken stop) {
        blocker_started.store(true);
        while (!stop.stop_requested())
            (void)::poll(nullptr, 0, 1);
    });
    deadline = std::chrono::steady_clock::now() + 2s;
    while (!blocker_started.load() && std::chrono::steady_clock::now() < deadline)
        (void)::poll(nullptr, 0, 1);
    MIRAGE_CHECK(blocker_started.load());
    integration::ModelProviderOverride fixture(
        [](const mira::ModelProfile &profile) { return std::make_shared<Provider>(profile); });
    auto model_config = config.model;
    model_config.enabled = true;
    model_config.model_selector = "reject-fixture";
    bool rejected = false;
    {
        integration::ModelLayer layer(limited, model_config, &fixture);
        mira::OperationContext context;
        context.deadline = std::chrono::steady_clock::now() + 300ms;
        auto refused = limited.submit_cancellable([&layer, context](executor::StopToken) {
            return layer.complete_harness_turn({}, "admission", context);
        });
        try {
            (void)refused.future.get();
        } catch (const std::exception &) {
            rejected = true;
        }
        layer.shutdown();
    }
    MIRAGE_CHECK(rejected);
    MIRAGE_CHECK(limited.get_failure_status().capacity_exhausted_count == 1);
    MIRAGE_CHECK(limited.request_task_cancel(blocked.handle).accepted());
    // Consume either running completion or queued cancellation even if the start check fails.
    try {
        blocked.future.get();
    } catch (const executor::TaskCancelled &) {
    }
    limited.shutdown(true);
    return mirage::testing::finish("native_agent_integration_test");
}
