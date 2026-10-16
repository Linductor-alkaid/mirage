// M10-09 manual product probe: drives one real harness chat turn through a
// running mirage-service over the Local IPC surface and prints the settled
// turn's parts, so a configured model endpoint can exercise the four basic
// tools (exec/read/write/edit) end to end. Requires a live service with the
// basic tools enabled and a model profile; never registered with add_test.
//
// Usage: m10_basic_tools_e2e <socket-path> <task-text> [settle-budget-seconds]
//                            [cancel-after-ms] [reuse-session-id]
// With a reused session id the probe skips session.open, so a driver can
// run several turns of one session and interleave its own file effects.

#include <mirage/conversation.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/protocol.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace {

using namespace std::chrono_literals;

void settle_sleep() { std::this_thread::sleep_for(500ms); }

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <socket-path> <task-text> [settle-budget-seconds]\n",
                     argv[0]);
        return 2;
    }
    const std::string socket = argv[1];
    const std::string task = argv[2];
    const auto settle_budget = std::chrono::seconds(argc > 3 ? std::atoi(argv[3]) : 600);
    // Optional product-level cancellation: "exec-start" cancels the moment
    // the running turn's parts show a started exec tool (the strongest
    // teardown evidence); a plain number cancels after a fixed delay.
    const std::string cancel_mode = argc > 4 ? argv[4] : "";

    mirage::runtime::ipc::IpcClient client(socket);
    std::string session;
    if (argc > 5 && *argv[5] != '\0') {
        session = argv[5]; // multi-turn driver: keep one session's observation state
    } else {
        const auto opened = client.call(mirage::runtime::ipc::OpenSessionRequest{}, 10s);
        if (!opened.ok) {
            std::fprintf(stderr, "session.open failed: %s\n", opened.error.message.c_str());
            return 1;
        }
        session = std::get<mirage::runtime::ipc::SessionOpened>(opened.payload).session_id;
    }
    std::printf("session: %s\n", session.c_str());

    const auto accepted =
        client.call(mirage::runtime::ipc::SessionChatRequest{session, task, true}, 30s);
    if (!accepted.ok) {
        std::fprintf(stderr, "session.chat failed: %s\n", accepted.error.message.c_str());
        return 1;
    }
    bool cancelled = cancel_mode.empty();
    const auto deadline = std::chrono::steady_clock::now() + settle_budget;
    for (;;) {
        const auto history =
            client.call(mirage::runtime::ipc::ChatHistoryRequest{session, {}}, 10s);
        if (history.ok) {
            const auto *view = std::get_if<mirage::runtime::ipc::DialogHistory>(&history.payload);
            if (!cancelled && view != nullptr && !view->turns.empty()) {
                for (const auto &part : view->turns.back().parts) {
                    if (part.kind == "tool" && part.name == "exec" && part.status != "complete") {
                        const auto ack =
                            client.call(mirage::runtime::ipc::CancelChatRequest{session}, 10s);
                        std::printf("cancel on exec-start accepted: %s\n", ack.ok ? "yes" : "no");
                        cancelled = true;
                        break;
                    }
                }
            }
            if (view != nullptr && !view->turns.empty() && view->turns.back().status != "pending") {
                const auto &turn = view->turns.back();
                std::printf("turn: %s\n", turn.status.c_str());
                if (turn.status != "ok") {
                    std::printf("error: %s\n", turn.error.c_str());
                }
                if (!turn.reply_text.empty()) {
                    std::printf("reply: %s\n", turn.reply_text.c_str());
                }
                for (const auto &part : turn.parts) {
                    if (part.kind != "tool") {
                        continue;
                    }
                    std::printf("tool: %s %s\n", part.name.c_str(), part.status.c_str());
                    std::printf("  input: %s\n", part.input.c_str());
                    std::printf("  output: %s\n", part.output.c_str());
                }
                return turn.status == "ok" ? 0 : 1;
            }
        }
        if (std::chrono::steady_clock::now() > deadline) {
            std::fprintf(stderr, "turn did not settle within the budget\n");
            return 1;
        }
        settle_sleep();
    }
}
