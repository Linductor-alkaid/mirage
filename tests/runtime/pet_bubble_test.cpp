#include "../../apps/pet/bubble_presentation.hpp"
#include "../support/test.hpp"
#include <cmath>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/pet/status.hpp>

namespace pet = mirage::runtime::pet;
using namespace std::chrono_literals;
int main() {
    pet::Status status;
    status.owner_epoch = "owner";
    status.revision = 1;
    status.host_state = "running";
    status.display_state = "thinking";
    status.source_kind = "turn";
    status.source_id = "turn";
    status.session_id = "session";
    auto now = pet::BubbleProjection::Clock::time_point{100s};
    pet::BubbleProjection projection;
    auto bubble = projection.update(status, true, {}, nullptr, nullptr, now);
    MIRAGE_CHECK(bubble && bubble->text == "正在思考…" && bubble->content_origin == "status_hint");
    pet::Preview thought{"request", "正在检查配置\n确认路径", 1, false};
    bubble = projection.update(status, true, {}, &thought, nullptr, now + 10ms);
    MIRAGE_CHECK(bubble && bubble->text == "正在检查配置 确认路径" && bubble->kind == "thinking");
    pet::Preview answer{"request", "原因未明，需要补充日志。", 2, false};
    bubble = projection.update(status, true, {}, &thought, &answer, now + 20ms);
    MIRAGE_CHECK(bubble && bubble->kind == "dialog" &&
                 bubble->text.find("原因未明") != std::string::npos);
    thought.sequence = 3;
    thought.text = "late thought";
    bubble = projection.update(status, true, {}, &thought, &answer, now + 30ms);
    MIRAGE_CHECK(bubble && bubble->kind == "dialog");
    answer = {"request", "", 4, false};
    thought = {"request", "", 5, false};
    bubble = projection.update(status, true, {}, &thought, &answer, now + 40ms);
    MIRAGE_CHECK(bubble && bubble->kind == "thinking" && bubble->content_origin == "status_hint");
    thought = {"retry", "新的思考", 6, false};
    bubble = projection.update(status, true, {}, &thought, &answer, now + 50ms);
    MIRAGE_CHECK(bubble && bubble->request_id == "retry" && bubble->text == "新的思考");
    status.display_state = "mouse";
    MIRAGE_CHECK(!projection.update(status, false, {}, &thought, &answer, now + 60ms));
    status.display_state = "thinking";
    bubble = projection.update(status, true, {}, &thought, &answer, now + 70ms);
    MIRAGE_CHECK(bubble && bubble->content_origin == "status_hint");
    status.display_state = "cancelled";
    MIRAGE_CHECK(!projection.update(status, false, {}, &thought, &answer, now + 80ms));
    status.source_id = "new-turn";
    status.display_state = "task_success";
    bubble =
        projection.update(status, false, "配置已修复，验证通过。", nullptr, nullptr, now + 90ms);
    MIRAGE_CHECK(bubble && bubble->final && bubble->remaining_ms == 5000);
    bubble = projection.update(status, false, "old", nullptr, nullptr, now + 2090ms);
    MIRAGE_CHECK(bubble && bubble->remaining_ms == 3000);
    MIRAGE_CHECK(!projection.update(status, false, "old", nullptr, nullptr, now + 5090ms));
    MIRAGE_CHECK(!projection.update(status, false, "late", nullptr, nullptr, now + 7090ms));
    MIRAGE_CHECK(pet::bubble_line("abc\n\t中文") == "abc 中文");
    MIRAGE_CHECK(pet::bubble_line("前文新🙂", 4) == "🙂");
    MIRAGE_CHECK(pet::bubble_line("\xc0\xaf").empty());
    MIRAGE_CHECK(pet::answer_summary("```cpp\nnot fixed\n```").empty());
    MIRAGE_CHECK(pet::answer_summary("如果检查通过，就已经完成修复。").empty());
    MIRAGE_CHECK(pet::answer_summary("配置尚未验证。").find("未") != std::string::npos);
    MIRAGE_CHECK(pet::answer_summary("还无法确定是哪项配置导致失败，需要补充日志后继续排查。")
                     .find("需要补充日志") != std::string::npos);
    status.bubble =
        pet::Bubble{"thinking", "model_preview", "request", "实时思考🙂", 8, false, false, 0};
    const auto encoded =
        mirage::runtime::ipc::encode_event({2, mirage::runtime::ipc::PetUpdatedEvent{status}});
    const auto decoded = mirage::runtime::ipc::decode_event(encoded);
    MIRAGE_CHECK(decoded.ok);
    MIRAGE_CHECK(std::get<mirage::runtime::ipc::PetUpdatedEvent>(decoded.event.payload).status ==
                 status);
    auto malformed = encoded;
    const auto position = malformed.find("\"remaining_ms\":0");
    MIRAGE_CHECK(position != std::string::npos);
    malformed.replace(position, std::string("\"remaining_ms\":0").size(), "\"remaining_ms\":6000");
    MIRAGE_CHECK(!mirage::runtime::ipc::decode_event(malformed).ok);
    mirage::pet_ui::BubblePresentation presentation;
    auto measure = [](const std::string &text) { return static_cast<float>(text.size()) * 10; };
    auto value = status.bubble;
    value->text = "abcdefghijk";
    presentation.update("owner", "source", value, now, measure, 1);
    MIRAGE_CHECK(std::abs(presentation.offset(now + 1s) - 24) < 0.01f);
    value->text += "lmnop";
    presentation.update("owner", "source", value, now + 1s, measure, 1);
    MIRAGE_CHECK(std::abs(presentation.offset(now + 1s) - 24) < 0.01f); // append does not restart
    value->kind = "dialog";
    value->content_origin = "answer_summary";
    value->final = true;
    value->remaining_ms = 1000;
    presentation.update("owner", "source", value, now + 2s, measure, 1);
    value->remaining_ms = 1000;
    presentation.update("owner", "source", value, now + 2900ms, measure, 1);
    MIRAGE_CHECK(!presentation.visible(now + 3100ms)); // stale snapshot cannot extend final hold
    value->final = false;
    value->remaining_ms = 0;
    value->request_id = "new-request";
    presentation.update("owner", "source", value, now + 4s, measure, 1);
    MIRAGE_CHECK(presentation.visible(now + 4s) && presentation.offset(now + 4s) == 0);
    return mirage::testing::finish("pet_bubble_test");
}
