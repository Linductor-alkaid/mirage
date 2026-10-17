#include "../support/ipc_io.hpp"
#include <atomic>
#include <cstdio>
#include <memory>
#include <mira/model_provider.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/runtime_service.hpp>
#include <stdexcept>

namespace ipc = mirage::runtime::ipc;
namespace pet = mirage::runtime::pet;
namespace test = mirage::testing;
using namespace std::chrono_literals;

namespace {
class Environment final : public mirage::desktop::DesktopEnvironment {
  public:
    mirage::desktop::EnvironmentInfo info() const override { return {"pet-test", "private"}; }
};
class Provider final : public mira::IModelProvider {
  public:
    explicit Provider(mira::ModelProfile profile) : profile_(std::move(profile)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    std::atomic_bool hold{true}, fail{false}, show_answer{false}, show_long_thought{false};
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &context,
                                            const mira::ProviderInferOptions &options) override {
        // Simulated transport callbacks remain on the Runtime Executor's task.
        if (options.preview_sink)
            options.preview_sink(request.request_id, {});
        if (options.thinking_preview_sink) {
            options.thinking_preview_sink(request.request_id, {});
            options.thinking_preview_sink(request.request_id, {"正在检查当前配置", 0, false});
        }
        bool answer_sent = false, long_thought_sent = false;
        while (hold.load() && !context.cancelled_or_expired(mira::Timestamp::now())) {
            if (show_long_thought.load() && !long_thought_sent) {
                long_thought_sent = true;
                if (options.thinking_preview_sink)
                    options.thinking_preview_sink(
                        request.request_id,
                        {std::string(128, 'p') + " " + std::string(2047, 'x'), 0, false});
            }
            if (show_answer.load() && !answer_sent) {
                answer_sent = true;
                if (options.preview_sink)
                    options.preview_sink(request.request_id,
                                         {"配置尚未验证，需要继续检查。", 0, false});
                if (options.thinking_preview_sink)
                    options.thinking_preview_sink(
                        request.request_id, {"late thinking must not replace answer", 0, false});
            }
            (void)::poll(nullptr, 0, 1);
        }
        if (context.expired(mira::Timestamp::now()) && !context.cancelled())
            return mira::make_model_error(mira::ModelDomainCode::ModelDeadlineExceeded,
                                          "fixture model timeout", false, context.operation);
        if (fail.load())
            throw std::runtime_error("fixture provider failure");
        mira::ModelResponse result;
        result.contract_version = {1, 0};
        result.request_id = request.request_id;
        result.operation_id = context.operation;
        result.profile_id = profile_.id;
        result.requested_model = profile_.model_selector;
        result.status = context.cancelled_or_expired(mira::Timestamp::now())
                            ? mira::ModelCompletionStatus::Cancelled
                            : mira::ModelCompletionStatus::Completed;
        mira::MessageOutput message;
        mira::OutputTextPart text;
        text.text = "fixture complete";
        message.content.emplace_back(std::move(text));
        result.output.emplace_back(std::move(message));
        return result;
    }

  private:
    mira::ModelProfile profile_;
};
pet::Status snapshot(ipc::IpcClient &client) {
    const auto response = client.call(ipc::PetStatusRequest{}, 2s);
    MIRAGE_CHECK(response.ok && std::holds_alternative<pet::Status>(response.payload));
    return std::get_if<pet::Status>(&response.payload) ? std::get<pet::Status>(response.payload)
                                                       : pet::Status{};
}
pet::Status wait_state(ipc::IpcClient &client, std::string_view expected) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    pet::Status status;
    do {
        status = snapshot(client);
        if (status.display_state == expected)
            break;
        (void)::poll(nullptr, 0, 2);
    } while (std::chrono::steady_clock::now() < deadline);
    MIRAGE_CHECK(status.display_state == expected);
    return status;
}
ipc::IpcStream subscribe(const std::string &endpoint, bool pet_events) {
    std::string error;
    auto stream = ipc::connect_stream(endpoint, 2s, error);
    MIRAGE_CHECK(stream.valid());
    MIRAGE_CHECK(test::write_all(
        stream,
        ipc::make_frame(ipc::encode_request(1, ipc::SubscribeEventsRequest{false, pet_events})),
        2s));
    std::string buffer;
    const auto ack = test::read_frame(stream, buffer, 2s);
    const auto seed = test::read_frame(stream, buffer, 2s);
    MIRAGE_CHECK(ack.status == test::FrameRead::Status::Message &&
                 ipc::decode_response(ack.message).ok);
    const auto decoded = ipc::decode_event(seed.message);
    MIRAGE_CHECK(decoded.ok && std::holds_alternative<ipc::HostStatusEvent>(decoded.event.payload));
    return stream;
}
} // namespace

int main() {
    test::TempDir dir;
    mirage::runtime::ServiceConfig config;
    config.socket_path = (dir.root() / "pet.sock").string();
    config.persist_settings = config.persist_session_state = config.persist_recovery_state = false;
    config.model.enabled = true;
    config.model.endpoint_origin = "https://fixture.invalid";
    config.model.model_selector = "pet-test";
    config.model.request_deadline = 3s;
    std::shared_ptr<Provider> provider;
    config.model_provider_override = std::make_shared<mirage::integration::ModelProviderOverride>(
        [&provider](const auto &profile) {
            provider = std::make_shared<Provider>(profile);
            return provider;
        });
    const auto binding = std::make_shared<mirage::integration::MiraEnvironmentBinding>(
        std::make_shared<Environment>());
    mirage::runtime::RuntimeService service(config);
    MIRAGE_CHECK(service.start(binding).ok);
    ipc::IpcClient client(config.socket_path);
    const auto hello = client.call(ipc::HelloRequest{}, 2s);
    MIRAGE_CHECK(hello.ok &&
                 std::get<ipc::ServiceIdentity>(hello.payload).pet_status.value_or(false) &&
                 std::get<ipc::ServiceIdentity>(hello.payload).pet_bubbles.value_or(false));
    auto opted = subscribe(config.socket_path, true);
    auto legacy = subscribe(config.socket_path, false);
    auto idle = snapshot(client);
    MIRAGE_CHECK(idle.display_state == "idle" && idle.active_count == 0);
    std::string buffer;
    const auto notification = test::read_frame(opted, buffer, 2s);
    const auto event = ipc::decode_event(notification.message);
    MIRAGE_CHECK(event.ok && std::get_if<ipc::PetUpdatedEvent>(&event.event.payload));
    std::string legacy_buffer;
    MIRAGE_CHECK(test::read_frame(legacy, legacy_buffer, 50ms).status ==
                 test::FrameRead::Status::Timeout);
    const auto sessions = client.call(ipc::ListSessionsRequest{}, 2s);
    MIRAGE_CHECK(sessions.ok);
    const auto session = std::get<ipc::SessionList>(sessions.payload).sessions.front().id;
    auto accepted = client.call(ipc::SessionChatRequest{session, "do work", false}, 2s);
    MIRAGE_CHECK(accepted.ok);
    // Headless has no tray maintenance timer: the model's real in-flight
    // transition must publish without a pet.status poll prompting it.
    bool pushed_thinking = false;
    const auto push_deadline = std::chrono::steady_clock::now() + 2s;
    while (!pushed_thinking && std::chrono::steady_clock::now() < push_deadline) {
        const auto frame = test::read_frame(opted, buffer, 200ms);
        if (frame.status != test::FrameRead::Status::Message)
            continue;
        const auto update = ipc::decode_event(frame.message);
        if (update.ok)
            if (const auto *pet_update = std::get_if<ipc::PetUpdatedEvent>(&update.event.payload))
                pushed_thinking = pet_update->status.display_state == "thinking";
    }
    MIRAGE_CHECK(pushed_thinking);
    legacy.close();
    const auto active = wait_state(client, "thinking");
    MIRAGE_CHECK(active.active_count == 1 && active.source_kind == "turn" &&
                 active.session_id == session);
    MIRAGE_CHECK(active.bubble && active.bubble->kind == "thinking" &&
                 active.bubble->text == "正在检查当前配置");
    // The real projection and event broadcast must deliver a decodable long
    // thinking preview, then keep serving status and answer updates.
    provider->show_long_thought.store(true);
    bool pushed_long_thought = false, valid_events = true;
    const auto long_deadline = std::chrono::steady_clock::now() + 1s;
    while (!pushed_long_thought && std::chrono::steady_clock::now() < long_deadline) {
        const auto frame = test::read_frame(opted, buffer, 100ms);
        if (frame.status != test::FrameRead::Status::Message)
            continue;
        const auto update = ipc::decode_event(frame.message);
        valid_events = valid_events && update.ok;
        if (update.ok)
            if (const auto *pet_update = std::get_if<ipc::PetUpdatedEvent>(&update.event.payload))
                pushed_long_thought = pet_update->status.bubble &&
                                      pet_update->status.bubble->truncated &&
                                      pet_update->status.bubble->text == std::string(2047, 'x');
    }
    MIRAGE_CHECK(valid_events && pushed_long_thought);
    opted.close();
    const auto long_status = snapshot(client);
    MIRAGE_CHECK(long_status.bubble && long_status.bubble->text == std::string(2047, 'x'));
    provider->show_long_thought.store(false);
    provider->show_answer.store(true);
    auto answer_status = snapshot(client);
    const auto answer_deadline = std::chrono::steady_clock::now() + 2s;
    while ((!answer_status.bubble || answer_status.bubble->kind != "dialog") &&
           std::chrono::steady_clock::now() < answer_deadline) {
        (void)::poll(nullptr, 0, 2);
        answer_status = snapshot(client);
    }
    MIRAGE_CHECK(answer_status.bubble && answer_status.bubble->kind == "dialog" &&
                 answer_status.bubble->content_origin == "answer_summary" &&
                 answer_status.bubble->text.find("未验证") != std::string::npos);
    MIRAGE_CHECK(!client.call(ipc::SessionChatRequest{session, "duplicate", false}, 2s).ok);
    MIRAGE_CHECK(snapshot(client).source_id == active.source_id);
    provider->hold.store(false);
    const auto done = wait_state(client, "task_success");
    MIRAGE_CHECK(done.active_count == 0 && done.source_id == active.source_id);
    MIRAGE_CHECK(done.bubble && done.bubble->final && done.bubble->text == "fixture complete");
    MIRAGE_CHECK(snapshot(client).revision == done.revision); // idempotent terminal
    provider->show_answer.store(false);

    provider->hold.store(true);
    accepted = client.call(ipc::SessionChatRequest{session, "cancel work", false}, 2s);
    MIRAGE_CHECK(accepted.ok);
    (void)wait_state(client, "thinking");
    MIRAGE_CHECK(client.call(ipc::CancelChatRequest{session}, 2s).ok);
    const auto cancelled = wait_state(client, "cancelled");
    MIRAGE_CHECK(cancelled.display_state != "task_success" && cancelled.display_state != "typing");
    const auto settle_deadline = std::chrono::steady_clock::now() + 3s;
    while (snapshot(client).active_count && std::chrono::steady_clock::now() < settle_deadline)
        (void)::poll(nullptr, 0, 2);
    MIRAGE_CHECK(snapshot(client).active_count == 0);
    MIRAGE_CHECK(snapshot(client).display_state == "cancelled");
    MIRAGE_CHECK(!snapshot(client).bubble);

    provider->hold.store(true);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "deadline work", false}, 2s).ok);
    (void)wait_state(client, "thinking");
    MIRAGE_CHECK(wait_state(client, "task_failure").active_count == 0);
    MIRAGE_CHECK(!snapshot(client).bubble);
    provider->fail.store(true);
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "fail work", false}, 2s).ok);
    (void)wait_state(client, "thinking");
    provider->hold.store(false);
    MIRAGE_CHECK(wait_state(client, "task_failure").active_count == 0);
    MIRAGE_CHECK(!snapshot(client).bubble);

    service.request_desktop_takeover();
    MIRAGE_CHECK(snapshot(client).display_state == "takeover");
    MIRAGE_CHECK(!snapshot(client).bubble);
    MIRAGE_CHECK(service.resume_desktop());
    MIRAGE_CHECK(snapshot(client).display_state != "takeover");
    MIRAGE_CHECK(client.call(ipc::ShutdownRequest{}, 2s).ok);
    MIRAGE_CHECK(service.run().clean);
    MIRAGE_CHECK(!std::filesystem::exists(config.socket_path));

    mirage::runtime::RuntimeService restarted(config);
    MIRAGE_CHECK(restarted.start(binding).ok);
    const auto fresh = snapshot(client);
    MIRAGE_CHECK(fresh.owner_epoch != idle.owner_epoch && fresh.display_state == "idle" &&
                 !fresh.bubble);
    const auto restored_sessions = client.call(ipc::ListSessionsRequest{}, 2s);
    const auto restored_session =
        std::get<ipc::SessionList>(restored_sessions.payload).sessions.front().id;
    MIRAGE_CHECK(
        client.call(ipc::SessionChatRequest{restored_session, "shutdown work", false}, 2s).ok);
    (void)wait_state(client, "thinking");
    MIRAGE_CHECK(client.call(ipc::ShutdownRequest{}, 2s).ok);
    MIRAGE_CHECK(restarted.run().clean);
    return test::finish("pet_status_integration_test");
}
