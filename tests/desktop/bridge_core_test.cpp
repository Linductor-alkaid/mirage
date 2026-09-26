// M5-02 desktop shell bridge core tests: the protocol-v1 request/response
// mapping between the renderer transport and the Local IPC session, without
// CEF (the mapping is deliberately CEF-free). The fake call function stands
// in for the shell session; the envelopes are the same shapes the golden
// vectors pin.

#include "../support/test.hpp"

#include <mirage/runtime/ipc/protocol.hpp>

#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <utility>
#include <variant>

#include "bridge_core.hpp"

namespace {

namespace ipc = mirage::runtime::ipc;
using mirage::desktop_shell::BridgeCore;

std::string handle(BridgeCore &core, const std::string &request) {
    std::string response;
    core.handle_request(request, [&response](const std::string &json) { response = json; });
    MIRAGE_CHECK(!response.empty()); // exactly-once respond, synchronously here
    return response;
}

// The service's own envelopes carry the session's correlation ids (2, 3, ...);
// the bridge must restore the renderer's echo (11, 12, ...) on the way out.

void scenario_hello_round_trip_restores_the_renderer_id() {
    BridgeCore core([](const ipc::Request &body, std::chrono::milliseconds) {
        MIRAGE_CHECK(std::holds_alternative<ipc::HelloRequest>(body));
        ipc::Response response;
        response.ok = true;
        response.id = 2; // the session-side id
        ipc::ServiceIdentity identity;
        identity.name = "mirage-runtime";
        identity.mirage_version = "0.4.0-test";
        identity.mira_core_version = "1.0.0";
        identity.host_status = "running";
        identity.protocol = ipc::kProtocolVersion;
        identity.events = true;
        response.payload = identity;
        std::promise<ipc::Response> done;
        done.set_value(std::move(response));
        return done.get_future();
    });

    const std::string response = handle(core, R"({"v":1,"id":11,"op":"hello"})");
    const ipc::ResponseDecode decoded = ipc::decode_response(response);
    MIRAGE_CHECK(decoded.ok);
    if (decoded.ok) {
        MIRAGE_CHECK(decoded.response.id == 11); // the renderer's echo, restored
        MIRAGE_CHECK(decoded.response.ok);
        const auto *identity = std::get_if<ipc::ServiceIdentity>(&decoded.response.payload);
        MIRAGE_CHECK(identity != nullptr);
        if (identity != nullptr) {
            MIRAGE_CHECK(identity->mirage_version == "0.4.0-test");
        }
    }
}

void scenario_undecodable_request_answers_protocol_error() {
    BridgeCore core([](const ipc::Request &, std::chrono::milliseconds) {
        MIRAGE_CHECK(false); // must not reach the session
        std::promise<ipc::Response> done;
        return done.get_future();
    });

    const std::string response = handle(core, "this is definitely not json");
    const ipc::ResponseDecode decoded = ipc::decode_response(response);
    MIRAGE_CHECK(decoded.ok);
    if (decoded.ok) {
        MIRAGE_CHECK(!decoded.response.ok);
        MIRAGE_CHECK(decoded.response.error.code == "protocol_error");
    }
}

void scenario_dead_session_surfaces_unavailable() {
    BridgeCore core([](const ipc::Request &, std::chrono::milliseconds) {
        ipc::Response failed;
        failed.ok = false;
        failed.id = 5;
        failed.error = {"unavailable", "no live session at 'svc'"};
        std::promise<ipc::Response> done;
        done.set_value(std::move(failed));
        return done.get_future();
    });

    const std::string response = handle(core, R"({"v":1,"id":12,"op":"task.list"})");
    const ipc::ResponseDecode decoded = ipc::decode_response(response);
    MIRAGE_CHECK(decoded.ok);
    if (decoded.ok) {
        MIRAGE_CHECK(!decoded.response.ok);
        MIRAGE_CHECK(decoded.response.id == 12); // the renderer's echo, on errors too
        MIRAGE_CHECK(decoded.response.error.code == "unavailable");
    }
}

void scenario_submit_request_body_maps_to_the_session() {
    ipc::Request seen = ipc::HelloRequest{};
    BridgeCore core([&seen](const ipc::Request &body, std::chrono::milliseconds) {
        seen = body;
        ipc::Response response;
        response.ok = true;
        response.id = 7;
        response.payload = ipc::TaskSubmitted{"task-1"};
        std::promise<ipc::Response> done;
        done.set_value(std::move(response));
        return done.get_future();
    });

    const std::string response =
        handle(core, R"({"v":1,"id":13,"op":"task.submit","goal":"open the goal file",)"
                     R"("steps":[{"op":"filesystem.read","arg":"/tmp/goal.txt"}]})");
    const auto *submitted = std::get_if<ipc::SubmitTaskRequest>(&seen);
    MIRAGE_CHECK(submitted != nullptr);
    if (submitted != nullptr) {
        MIRAGE_CHECK(submitted->goal == "open the goal file");
        MIRAGE_CHECK(submitted->steps.size() == 1);
        if (submitted->steps.size() == 1) {
            MIRAGE_CHECK(submitted->steps[0].kind == ipc::StepKind::FilesystemRead);
            MIRAGE_CHECK(submitted->steps[0].argument == "/tmp/goal.txt");
        }
    }
    const ipc::ResponseDecode decoded = ipc::decode_response(response);
    MIRAGE_CHECK(decoded.ok);
    if (decoded.ok) {
        MIRAGE_CHECK(decoded.response.id == 13);
        MIRAGE_CHECK(std::holds_alternative<ipc::TaskSubmitted>(decoded.response.payload));
    }
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[bridge_core_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("hello_round_trip_restores_the_renderer_id",
                 scenario_hello_round_trip_restores_the_renderer_id);
    run_scenario("undecodable_request_answers_protocol_error",
                 scenario_undecodable_request_answers_protocol_error);
    run_scenario("dead_session_surfaces_unavailable", scenario_dead_session_surfaces_unavailable);
    run_scenario("submit_request_body_maps_to_the_session",
                 scenario_submit_request_body_maps_to_the_session);
    return mirage::testing::finish("bridge_core_test");
}
