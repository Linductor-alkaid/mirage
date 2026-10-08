#include "../support/ipc_io.hpp"
#include <atomic>
#include <kairo/comm/channel.hpp>
#include <memory>
#include <mira/model_provider.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/runtime_service.hpp>
#include <poll.h>
#include <stdexcept>

namespace ipc = mirage::runtime::ipc;
namespace desktop = mirage::desktop;
using namespace std::chrono_literals;
namespace {
class Environment final : public desktop::DesktopEnvironment {
  public:
    desktop::EnvironmentInfo info() const override { return {"tray-test", "private"}; }
};
class Carrier final : public desktop::TrayCarrier {
  public:
    bool refuse = false;
    std::atomic_bool break_now{false};
    kairo::comm::MpscChannel<desktop::TrayAction> commands{{.capacity = 8, .name = "fixture-tray"}};
    RunReport run(const desktop::TrayCarrierContext &context,
                  const std::function<bool()> &stop) override {
        if (refuse)
            return {false, "fixture registration refused"};
        if (context.on_ready)
            context.on_ready();
        while (!stop() && !break_now.load()) {
            (void)context.load_state();
            desktop::TrayAction action{};
            if (commands.receive_for(action, 10ms))
                context.on_action(action);
        }
        return {!break_now.load(), break_now.load() ? "fixture host lost" : ""};
    }
    void wakeup() override {}
};
class Frontend final : public desktop::FrontendProcess {
  public:
    bool fail_open = false, throw_open = false, fail_stop = false;
    int opened = 0, stopped = 0;
    std::int64_t current = 0;
    bool open(const std::string &, std::string &diagnostic) override {
        if (throw_open)
            throw std::runtime_error("fixture launch exception");
        if (fail_open) {
            diagnostic = "fixture launch refused";
            return false;
        }
        if (!current) {
            current = 777;
            ++opened;
        }
        return true;
    }
    std::int64_t pid() override { return current; }
    bool stop(std::string &diagnostic) override {
        ++stopped;
        current = 0;
        if (fail_stop)
            diagnostic = "fixture child did not exit";
        return !fail_stop;
    }
};
class HeldProvider final : public mira::IModelProvider {
  public:
    explicit HeldProvider(mira::ModelProfile profile) : profile_(std::move(profile)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &context,
                                            const mira::ProviderInferOptions &) override {
        while (!context.cancelled_or_expired(mira::Timestamp::now()))
            (void)::poll(nullptr, 0, 2);
        mira::ModelResponse reply;
        reply.contract_version = {1, 0};
        reply.request_id = request.request_id;
        reply.operation_id = request.operation_id;
        reply.profile_id = profile_.id;
        reply.requested_model = profile_.model_selector;
        reply.status = mira::ModelCompletionStatus::Cancelled;
        return reply;
    }

  private:
    mira::ModelProfile profile_;
};
auto binding() {
    return std::make_shared<mirage::integration::MiraEnvironmentBinding>(
        std::make_shared<Environment>());
}
mirage::runtime::ServiceConfig config(const mirage::testing::TempDir &dir,
                                      const std::shared_ptr<Carrier> &carrier,
                                      const std::shared_ptr<Frontend> &frontend) {
    mirage::runtime::ServiceConfig result;
    result.socket_path = (dir.root() / "tray.sock").string();
    result.persist_recovery_state = result.persist_session_state = result.persist_settings = false;
    result.tray_carrier = carrier;
    result.frontend = frontend;
    return result;
}
ipc::ProductState state(ipc::IpcClient &client) {
    const auto reply = client.call(ipc::ProductControlRequest{}, 2s);
    MIRAGE_CHECK(reply.ok);
    const auto *snapshot = std::get_if<ipc::ProductState>(&reply.payload);
    MIRAGE_CHECK(snapshot);
    return snapshot ? *snapshot : ipc::ProductState{};
}
void lifecycle() {
    mirage::testing::TempDir dir;
    auto carrier = std::make_shared<Carrier>();
    auto frontend = std::make_shared<Frontend>();
    auto options = config(dir, carrier, frontend);
    mirage::runtime::RuntimeService service(options);
    MIRAGE_CHECK(service.start(binding()).ok);
    ipc::IpcClient client(options.socket_path);
    const auto hello = client.call(ipc::HelloRequest{}, 2s);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(hello.ok && identity && identity->tray.value_or(false));
    auto before = state(client);
    MIRAGE_CHECK(before.frontend_pid == 777 && before.window_epoch == 1 && !before.frontend_ready);
    MIRAGE_CHECK(!client.call(ipc::ProductControlRequest{"frontend_ready", 0, 888}, 2s).ok);
    MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"frontend_ready", 0, 777}, 2s).ok);
    MIRAGE_CHECK(state(client).frontend_ready);
    MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"open", 0}, 2s).ok);
    auto after = state(client);
    MIRAGE_CHECK(after.frontend_pid == before.frontend_pid && after.window_epoch == 2);
    MIRAGE_CHECK(!client.call(ipc::ProductControlRequest{"confirm_quit", 1}, 2s).ok);
    MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"quit", 0}, 2s).ok);
    MIRAGE_CHECK(service.run().clean);
    MIRAGE_CHECK(frontend->opened == 1 && frontend->stopped == 1 && frontend->current == 0);
    MIRAGE_CHECK(!std::filesystem::exists(options.socket_path));
}
void active_exit() {
    mirage::testing::TempDir dir;
    auto carrier = std::make_shared<Carrier>();
    auto frontend = std::make_shared<Frontend>();
    auto options = config(dir, carrier, frontend);
    options.model.enabled = true;
    options.model.endpoint_origin = "https://fixture.example";
    options.model.model_selector = "held-agent";
    options.model.request_deadline = 30s;
    options.model_provider_override = std::make_shared<mirage::integration::ModelProviderOverride>(
        [](const mira::ModelProfile &profile) { return std::make_shared<HeldProvider>(profile); });
    mirage::runtime::RuntimeService service(options);
    MIRAGE_CHECK(service.start(binding()).ok);
    ipc::IpcClient client(options.socket_path);
    const auto opened = client.call(ipc::OpenSessionRequest{}, 2s);
    const auto *session = std::get_if<ipc::SessionOpened>(&opened.payload);
    MIRAGE_CHECK(opened.ok && session);
    if (session) {
        ipc::SessionChatRequest chat;
        chat.session_id = session->session_id;
        chat.text = "held request";
        chat.agent = true;
        MIRAGE_CHECK(client.call(chat, 2s).ok);
        MIRAGE_CHECK(state(client).active_work >= 1);
        MIRAGE_CHECK(carrier->commands.try_send(desktop::TrayAction::Quit));
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        ipc::ProductState pending;
        do {
            pending = state(client);
            (void)::poll(nullptr, 0, 5);
        } while (!pending.exit_pending && std::chrono::steady_clock::now() < deadline);
        MIRAGE_CHECK(pending.exit_pending && pending.active_work >= 1);
        MIRAGE_CHECK(
            !client.call(ipc::ProductControlRequest{"confirm_quit", pending.exit_epoch + 1}, 2s)
                 .ok);
        MIRAGE_CHECK(
            client.call(ipc::ProductControlRequest{"cancel_quit", pending.exit_epoch}, 2s).ok);
        MIRAGE_CHECK(!state(client).exit_pending && state(client).active_work >= 1);
        MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"quit", 0}, 2s).ok);
        const auto again = state(client);
        MIRAGE_CHECK(again.exit_pending && again.exit_epoch > pending.exit_epoch);
        MIRAGE_CHECK(
            !client.call(ipc::ProductControlRequest{"confirm_quit", pending.exit_epoch}, 2s).ok);
        MIRAGE_CHECK(
            client.call(ipc::ProductControlRequest{"confirm_quit", again.exit_epoch}, 2s).ok);
    } else
        service.request_shutdown();
    const auto started = std::chrono::steady_clock::now();
    MIRAGE_CHECK(service.run().clean);
    MIRAGE_CHECK(std::chrono::steady_clock::now() - started < 5s);
    MIRAGE_CHECK(frontend->stopped == 1);
}
void workflow_exit() {
    mirage::testing::TempDir dir;
    auto carrier = std::make_shared<Carrier>();
    auto frontend = std::make_shared<Frontend>();
    auto options = config(dir, carrier, frontend);
    mirage::runtime::RuntimeService service(options);
    MIRAGE_CHECK(service.start(binding()).ok);
    ipc::IpcClient client(options.socket_path);
    const std::string definition = R"({"schema_version":{"major":1,"minor":0},
        "workflow_id":"11111111111111111111111111111111","name":"exit-only fixture","parameters":[],
        "steps":[{"step_id":"22222222222222222222222222222222","kind":"verify",
        "verification":{"signal":"run_parameter:x","op":"eq","value":"y"}}],
        "default_policy":"strict","allowed_policies":["strict","interactive","dry_run"]})";
    MIRAGE_CHECK(client.call(ipc::WorkflowPublishRequest{definition}, 2s).ok);
    ipc::WorkflowRunRequest request;
    request.workflow_id = "11111111111111111111111111111111";
    request.policy = "interactive";
    MIRAGE_CHECK(client.call(request, 2s).ok);
    bool waiting = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!waiting && std::chrono::steady_clock::now() < deadline) {
        const auto runs = client.call(ipc::WorkflowRunsRequest{}, 2s);
        if (const auto *list = std::get_if<ipc::WorkflowRunList>(&runs.payload))
            for (const auto &run : list->runs)
                waiting |= run.state == "waiting_user";
        (void)::poll(nullptr, 0, 5);
    }
    MIRAGE_CHECK(waiting);
    MIRAGE_CHECK(state(client).active_work >= 1);
    MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"quit", 0}, 2s).ok);
    const auto pending = state(client);
    MIRAGE_CHECK(pending.exit_pending && pending.active_work >= 1);
    MIRAGE_CHECK(
        client.call(ipc::ProductControlRequest{"confirm_quit", pending.exit_epoch}, 2s).ok);
    MIRAGE_CHECK(service.run().clean);
}

void failures() {
    for (int mode = 0; mode != 5; ++mode) {
        mirage::testing::TempDir dir;
        auto carrier = std::make_shared<Carrier>();
        auto frontend = std::make_shared<Frontend>();
        carrier->refuse = mode == 0;
        frontend->fail_open = mode == 1;
        frontend->throw_open = mode == 2;
        frontend->fail_stop = mode == 4;
        auto options = config(dir, carrier, frontend);
        mirage::runtime::RuntimeService service(options);
        const auto started = service.start(binding());
        if (mode < 3) {
            MIRAGE_CHECK(!started.ok);
            MIRAGE_CHECK(!std::filesystem::exists(options.socket_path));
            MIRAGE_CHECK(frontend->opened == 0);
        } else {
            MIRAGE_CHECK(started.ok);
            if (mode == 3)
                carrier->break_now.store(true);
            else
                service.request_shutdown();
            const auto report = service.run();
            MIRAGE_CHECK(!report.clean && !report.diagnostic.empty());
            MIRAGE_CHECK(frontend->stopped == 1);
        }
    }
}
void wire() {
    for (const auto *action : {"status", "open", "quit", "confirm_quit", "cancel_quit"}) {
        const auto decoded =
            ipc::decode_request(ipc::encode_request(1, ipc::ProductControlRequest{action, 5}));
        const auto *request = std::get_if<ipc::ProductControlRequest>(&decoded.body);
        MIRAGE_CHECK(decoded.ok && request && request->action == action &&
                     request->exit_epoch == 5);
    }
    MIRAGE_CHECK(!ipc::decode_request(
                      R"({"v":1,"id":1,"op":"product.control","action":"destroy","exit_epoch":0})")
                      .ok);
    MIRAGE_CHECK(!ipc::decode_request(
                      R"({"v":1,"id":1,"op":"product.control","action":"quit","exit_epoch":-1})")
                      .ok);
    ipc::Response response;
    response.ok = true;
    response.payload = ipc::ProductState{777, 2, 3, 1, true, true};
    const auto decoded = ipc::decode_response(ipc::encode_response(response));
    const auto *product = std::get_if<ipc::ProductState>(&decoded.response.payload);
    MIRAGE_CHECK(decoded.ok && product && product->exit_pending && product->frontend_ready &&
                 product->exit_epoch == 3);
}
} // namespace
int main() {
    wire();
    lifecycle();
    active_exit();
    workflow_exit();
    failures();
    return mirage::testing::finish("tray_runtime_test");
}
