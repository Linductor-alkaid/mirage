#include "../support/ipc_io.hpp"
#include "runtime_bridge.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <executor/executor.hpp>
#include <mira/json.hpp>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/persistence/store.hpp>
#include <mirage/runtime/runtime_service.hpp>
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
                                            const mira::ProviderInferOptions &) override {
        const int number = ++calls;
        bool image = false;
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                if (const auto *screen = std::get_if<mira::ImagePart>(&part)) {
                    image = true;
                    (void)screen;
                }
            }
        saw_image.store(image);
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
                if (item.provenance.source == "mirage.harness.tool-result.v1")
                    saw_tool_result.store(true);
        mira::MessageOutput message;
        mira::OutputTextPart text;
        text.text = "Mira 已完成通用工具调用。";
        message.content.emplace_back(std::move(text));
        reply.output.emplace_back(std::move(message));
        return reply;
    }
    mira::ModelProfile profile_;
    std::atomic_int calls{0};
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
            result = model.complete_harness_turn({}, read("MIRAGE_PROBE_PROMPT"), context);
            model.shutdown();
        }
        owner.shutdown(true);
        std::printf("ok=%d steps=%u tools=%u\n", result.ok, result.model_steps, result.tool_calls);
        std::printf("%s\n", result.ok ? result.reply_text.c_str() : result.error.c_str());
        return result.ok ? 0 : 1;
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
    config.persist_session_state = false;

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
    provider->report_usage.store(false);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "缺失Token用量", true}, 2s).ok);
    const auto missing_usage = wait_turn(client, session);
    MIRAGE_CHECK(missing_usage && missing_usage->status == "ok" && !missing_usage->context_usage);
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
    MIRAGE_CHECK(turn && turn->status == "failed" &&
                 turn->error.find("16 model steps") != std::string::npos);
    MIRAGE_CHECK(provider->calls.load() - before_budget == 16);
    provider->always_tool.store(false);
    provider->hold.store(true);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "超时路径", true}, 2s).ok);
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "failed" &&
                 turn->error.find("cancelled") != std::string::npos);
    provider->hold.store(false);
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
    MIRAGE_CHECK(bridge.call(ipc::SubscribeEventsRequest{}, "subscribe"));
    bool subscribed = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && !subscribed) {
        while (bridge.receive(message))
            if (message.tag == "subscribe" && message.response.ok)
                subscribed = true;
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(subscribed);
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
    MIRAGE_CHECK(bridge.call(ipc::SessionChatRequest{session, "真实事件", true}, "send", 7));
    bool got_event = false, got_ack = false;
    deadline = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < deadline && (!got_event || !got_ack)) {
        while (bridge.receive(message)) {
            if (message.event &&
                std::holds_alternative<ipc::ChatTurnUpdatedEvent>(message.event->payload))
                got_event = true;
            if (message.tag == "send" && message.response.ok)
                got_ack = true;
        }
        (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(got_event && got_ack && wakes.load() > 0);
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
    turn = wait_turn(client, session);
    MIRAGE_CHECK(turn && turn->status == "ok");
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
