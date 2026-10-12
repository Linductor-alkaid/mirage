#include "../support/test.hpp"
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/pet/status.hpp>
#include <vector>

namespace pet = mirage::runtime::pet;
namespace ipc = mirage::runtime::ipc;
using namespace std::chrono_literals;

int main() {
    const auto now = pet::Projection::Clock::time_point{100s};
    MIRAGE_CHECK(pet::running_tool_phase("desktop.screen.capture") == "looking");
    MIRAGE_CHECK(pet::running_tool_phase("desktop.vision.click") == "mouse");
    MIRAGE_CHECK(pet::running_tool_phase("desktop.input.type_text") == "typing");
    MIRAGE_CHECK(pet::running_tool_phase("desktop.process.execute") == "unknown");
    pet::Projection p("owner-A");
    auto status = p.update("starting", {}, false, false, now);
    MIRAGE_CHECK(status.display_state == "starting" && pet::valid_status(status));
    status = p.update("running", {}, false, false, now + 1s);
    MIRAGE_CHECK(status.display_state == "idle" && status.revision == 2);
    const auto repeated = p.update("running", {}, false, false, now + 2s);
    MIRAGE_CHECK(repeated.revision == status.revision && repeated.elapsed_ms == 1000);

    std::vector<pet::Work> work{{"turn", "a", "session", "queued", "typing"}};
    status = p.update("running", work, false, false, now + 3s);
    MIRAGE_CHECK(status.display_state == "unknown" && status.reason_code == "queued");
    work.front().state = "active";
    work.front().phase = "thinking";
    status = p.update("running", work, false, false, now + 4s);
    MIRAGE_CHECK(status.display_state == "thinking" && status.active_count == 1);
    work.push_back({"turn", "b", "other", "active", "looking"});
    status = p.update("running", work, false, false, now + 5s);
    MIRAGE_CHECK(status.source_id == "a" && status.active_count == 2 && status.elapsed_ms == 1000);
    work.back().input_owner = true;
    work.back().phase = "mouse";
    status = p.update("running", work, false, false, now + 6s);
    MIRAGE_CHECK(status.source_id == "b" && status.display_state == "mouse");
    status = p.update("running", work, false, true, now + 7s);
    MIRAGE_CHECK(status.display_state == "waiting" && status.source_id.empty());
    status = p.update("running", work, true, true, now + 8s);
    MIRAGE_CHECK(status.display_state == "takeover");
    status = p.update("stopping", work, true, true, now + 9s);
    MIRAGE_CHECK(status.display_state == "exiting");
    status = p.update("failed", work, false, false, now + 10s);
    MIRAGE_CHECK(status.display_state == "task_failure" && status.reason_code == "host_failed");
    work = {{"turn", "a", "session", "active", "unknown"}};
    status = p.update("running", work, false, false, now + 11s);
    MIRAGE_CHECK(status.display_state == "unknown");
    work.front().state = "completed";
    status = p.update("running", work, false, false, now + 12s);
    MIRAGE_CHECK(status.display_state == "task_success");
    const auto terminal_revision = status.revision;
    status = p.update("running", work, false, false, now + 16s);
    MIRAGE_CHECK(status.display_state == "task_success" && status.revision == terminal_revision);
    status = p.update("running", work, false, false, now + 17s);
    MIRAGE_CHECK(status.display_state == "idle" && status.source_id.empty());
    status = p.update("running", work, false, false, now + 18s);
    MIRAGE_CHECK(status.display_state == "idle"); // no historical completion replay
    work.front().source_id = "new";
    work.front().state = "active";
    status = p.update("running", work, false, false, now + 19s);
    work.front().state = "failed";
    status = p.update("running", work, false, false, now + 20s);
    MIRAGE_CHECK(status.display_state == "task_failure");
    status = p.update("running", work, false, false, now + 100s);
    MIRAGE_CHECK(status.display_state == "task_failure");
    work = {{"turn", "next", "session", "cancelling", "typing"}};
    status = p.update("running", work, false, false, now + 101s);
    MIRAGE_CHECK(status.display_state == "cancelled" && status.summary == "正在取消");
    work.front().state = "cancelled";
    status = p.update("running", work, false, false, now + 102s);
    MIRAGE_CHECK(status.display_state == "cancelled" && status.summary == "已取消");
    work = {{"task", "paused", {}, "paused", "mouse"}};
    status = p.update("running", work, false, false, now + 103s);
    MIRAGE_CHECK(status.display_state == "paused");
    work.front().source_id.assign(129, 'x');
    status = p.update("running", work, false, false, now + 104s);
    MIRAGE_CHECK(status.display_state == "unknown" && status.reason_code == "invalid_source");
    work.assign(pet::max_work_items + 1, {"turn", "id", {}, "active"});
    status = p.update("running", work, false, false, now + 105s);
    MIRAGE_CHECK(status.reason_code == "projection_capacity" && pet::valid_status(status));

    const auto request = ipc::decode_request(ipc::encode_request(7, ipc::PetStatusRequest{}));
    MIRAGE_CHECK(request.ok && std::holds_alternative<ipc::PetStatusRequest>(request.body));
    const auto subscribe =
        ipc::decode_request(ipc::encode_request(8, ipc::SubscribeEventsRequest{false, true}));
    MIRAGE_CHECK(subscribe.ok && std::get<ipc::SubscribeEventsRequest>(subscribe.body).pet_status);
    const auto legacy = ipc::decode_request(R"({"v":1,"id":8,"op":"events.subscribe"})");
    MIRAGE_CHECK(legacy.ok && !std::get<ipc::SubscribeEventsRequest>(legacy.body).pet_status);
    MIRAGE_CHECK(
        !ipc::decode_request(R"({"v":1,"id":8,"op":"events.subscribe","pet_status":1})").ok);
    ipc::Response response;
    response.id = 9;
    response.ok = true;
    response.payload = status;
    auto decoded = ipc::decode_response(ipc::encode_response(response));
    MIRAGE_CHECK(decoded.ok && std::get<pet::Status>(decoded.response.payload) == status);
    const auto event = ipc::decode_event(ipc::encode_event({11, ipc::PetUpdatedEvent{status}}));
    MIRAGE_CHECK(event.ok && std::get<ipc::PetUpdatedEvent>(event.event.payload).status == status);
    auto bad = status;
    for (const auto &field :
         {"revision", "schema", "state", "source", "summary", "count", "elapsed"}) {
        bad = status;
        const std::string name{field};
        if (name == "revision")
            bad.revision = 0;
        if (name == "schema")
            bad.schema_version = 2;
        if (name == "state")
            bad.display_state = "invented";
        if (name == "source") {
            bad.source_kind = "turn";
            bad.source_id.clear();
        }
        if (name == "summary")
            bad.summary.assign(257, 'x');
        if (name == "count")
            bad.active_count = pet::max_work_items + 1;
        if (name == "elapsed")
            bad.elapsed_ms = UINT64_MAX;
        response.payload = bad;
        MIRAGE_CHECK(!ipc::decode_response(ipc::encode_response(response)).ok);
        MIRAGE_CHECK(!ipc::decode_event(ipc::encode_event({11, ipc::PetUpdatedEvent{bad}})).ok);
    }
    ipc::ServiceIdentity hello;
    hello.name = "mirage-service";
    hello.pet_status = true;
    response.payload = hello;
    decoded = ipc::decode_response(ipc::encode_response(response));
    MIRAGE_CHECK(
        decoded.ok &&
        std::get<ipc::ServiceIdentity>(decoded.response.payload).pet_status.value_or(false));
    pet::Projection restarted("owner-B");
    MIRAGE_CHECK(restarted.update("running", {}, false, false, now).owner_epoch !=
                 status.owner_epoch);
    std::vector<pet::Work> fast{{"turn", "fast", "session", "completed", "unknown", false, 1}};
    status = restarted.update("running", fast, false, false, now + 1s);
    MIRAGE_CHECK(status.display_state == "task_success" && status.source_id == "fast");
    MIRAGE_CHECK(restarted.completed_seen() == 1);
    fast.push_back({"turn", "fast-failure", "session", "failed", "unknown", false, 2});
    status = restarted.update("running", fast, false, false, now + 2s);
    MIRAGE_CHECK(status.display_state == "task_failure" && status.source_id == "fast-failure");
    fast.front().completion_order = 0;
    MIRAGE_CHECK(restarted.update("running", fast, false, false, now + 3s).source_id ==
                 "fast-failure");
    return mirage::testing::finish("pet_status_test");
}
