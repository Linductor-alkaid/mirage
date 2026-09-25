#include "desktop_client.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include <executor/executor.hpp>

#include "include/cef_app.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"

#include "shell_session.hpp"

namespace mirage::desktop_shell {
namespace {

/// Renderer-facing process message names. The message router owns its own
/// internal name; these are the shell's event and connection-loss push
/// channel (browser → renderer only).
constexpr const char *kEventMessage = "mirage:desktop-event";
constexpr const char *kLostMessage = "mirage:desktop-connection-lost";

/// Refcounted task posted between threads (executor worker → CEF UI thread,
/// session worker → UI thread fan-out). Using CefTask keeps the cross-thread
/// boundary free of base::Bind type gymnastics.
class UiTask final : public CefTask {
  public:
    explicit UiTask(std::function<void()> work) : work_(std::move(work)) {}
    void Execute() override { work_(); }
    IMPLEMENT_REFCOUNTING(UiTask);

  private:
    std::function<void()> work_;
};

} // namespace

/// The browser-side message router handler: the renderer's query IS one
/// protocol-v1 request envelope. Third-party-thread discipline (AGENTS.md
/// rule 11): the CEF UI thread only validates and hands off, the bridge
/// exchange runs on the Executor, and the answer hops back to the UI thread.
class DesktopClient::BridgeHandler final : public CefMessageRouterBrowserSide::Handler {
  public:
    BridgeHandler(executor::Executor &executor, BridgeCore &bridge)
        : executor_(executor), bridge_(bridge) {}

    bool OnQuery(CefRefPtr<CefBrowser> /*browser*/, CefRefPtr<CefFrame> /*frame*/,
                 std::int64_t /*query_id*/, const CefString &request, bool /*persistent*/,
                 CefRefPtr<Callback> callback) override {
        const std::string request_json = request.ToString();
        const std::string query_log = "[mirage-desktop] query: " + request_json.substr(0, 120);
        std::fprintf(stderr, "%s\n", query_log.c_str());
        std::fflush(stderr);
        CefRefPtr<Callback> cb = callback;
        std::future<void> task_future = executor_.submit_auto([this, cb, request_json] {
            std::string response_json;
            try {
                bridge_.handle_request(request_json, [&response_json](const std::string &json) {
                    response_json = json;
                });
            } catch (const std::exception &error) {
                // A pool-task failure must not wedge the query: answer with
                // the stable wire-shaped internal error instead.
                ipc::Response failed;
                failed.ok = false;
                failed.error = {"internal", std::string("bridge task failed: ") + error.what()};
                response_json = ipc::encode_response(failed);
            }
            CefPostTask(TID_UI, new UiTask([cb, response_json] { cb->Success(response_json); }));
        });
        // Admission visibility (AGENTS.md rule 10): a rejected submission
        // never runs the task, so nothing would answer the query. The
        // rejection surfaces as the future's exception; an already-finished
        // accepted task has already answered (Success is single-shot at the
        // router, so no double answer is possible here).
        if (task_future.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
            try {
                task_future.get();
            } catch (const std::exception &error) {
                ipc::Response failed;
                failed.ok = false;
                failed.error = {"internal",
                                std::string("bridge submission rejected: ") + error.what()};
                const std::string json = ipc::encode_response(failed);
                CefPostTask(TID_UI, new UiTask([cb, json] { cb->Success(json); }));
            }
        }
        return true; // answered asynchronously
    }

  private:
    executor::Executor &executor_;
    BridgeCore &bridge_;
};

DesktopClient::DesktopClient(executor::Executor &executor, BridgeCore &bridge)
    : executor_(executor), bridge_(bridge) {}

void DesktopClient::init_router() {
    CEF_REQUIRE_UI_THREAD();
    CefMessageRouterConfig config;
    config.js_query_function = "mirageQuery";
    config.js_cancel_function = "mirageQueryCancel";
    router_ = CefMessageRouterBrowserSide::Create(config);
    bridge_handler_ = std::make_unique<BridgeHandler>(executor_, bridge_);
    router_->AddHandler(bridge_handler_.get(), /*first=*/false);
}

DesktopClient::~DesktopClient() { router_->RemoveHandler(bridge_handler_.get()); }

bool DesktopClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame, CefProcessId source_process,
                                             CefRefPtr<CefProcessMessage> message) {
    return router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

void DesktopClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    std::fprintf(stderr, "[mirage-desktop] browser created\n");
    std::fflush(stderr);
    browsers_.push_back(browser);
}

void DesktopClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    std::fprintf(stderr, "[mirage-desktop] browser closing\n");
    std::fflush(stderr);
    const auto it = std::find(browsers_.begin(), browsers_.end(), browser);
    if (it != browsers_.end()) {
        browsers_.erase(it);
    }
    if (browsers_.empty()) {
        // Last window closed: the shell product form exits (the CLI/service
        // process forms keep the M1 discipline of explicit shutdowns; the
        // shell window is the product surface itself).
        CefQuitMessageLoop();
    }
}

void DesktopClient::forward_event(const std::string &event_json) {
    if (!CefCurrentlyOn(TID_UI)) {
        CefRefPtr<DesktopClient> self = this;
        CefPostTask(TID_UI, new UiTask([self, event_json] { self->forward_event(event_json); }));
        return;
    }
    for (const CefRefPtr<CefBrowser> &browser : browsers_) {
        CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kEventMessage);
        message->GetArgumentList()->SetString(0, event_json);
        browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
    }
}

void DesktopClient::forward_connection_lost(const std::string &diagnostic) {
    if (!CefCurrentlyOn(TID_UI)) {
        CefRefPtr<DesktopClient> self = this;
        CefPostTask(TID_UI,
                    new UiTask([self, diagnostic] { self->forward_connection_lost(diagnostic); }));
        return;
    }
    for (const CefRefPtr<CefBrowser> &browser : browsers_) {
        CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kLostMessage);
        message->GetArgumentList()->SetString(0, diagnostic);
        browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
    }
}

} // namespace mirage::desktop_shell
