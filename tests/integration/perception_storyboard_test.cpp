#include "../../integration/mira/src/content_task_assessment.hpp"
#include "../../integration/mira/src/content_visual_supply.hpp"
#include "../../integration/mira/src/perception_storyboard.hpp"
#include "../../integration/mira/src/perception_temporal_selection.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <source_location>
#include <stdexcept>

namespace {
namespace integration = mirage::integration;
namespace detail = integration::detail;
using Clock = std::chrono::steady_clock;
int checks = 0;
void check(bool value, const std::source_location loc = std::source_location::current()) {
    ++checks;
    if (!value)
        throw std::runtime_error("storyboard check failed at " + std::to_string(loc.line()));
}
struct Fixture {
    Clock::time_point now = Clock::now();
    integration::PerceptionView view;
    std::vector<std::shared_ptr<integration::PerceptionPreview>> frames;
    Fixture() {
        view.source_epoch = 2;
        view.dropped = 3;
        view.filtered = 4;
        view.resync = true;
        for (int i = 0; i < 4; ++i) {
            auto p = std::make_shared<integration::PerceptionPreview>();
            p->id = "frame-" + std::to_string(i);
            p->observed_at = now - std::chrono::seconds{4 - i};
            auto &image = p->image;
            image.width = 640;
            image.height = 320;
            image.stride = 640 * 4;
            image.pixels.resize(image.stride * 320);
            for (std::size_t j = 0; j < image.pixels.size(); j += 4) {
                image.pixels[j] = static_cast<std::uint8_t>(20 + i * 40);
                image.pixels[j + 3] = 255;
            }
            integration::PerceptionEvent event;
            event.source_epoch = 2;
            event.frame_sequence = std::uint64_t(i + 1);
            event.frame_id = p->id;
            event.observed_at = p->observed_at;
            view.events.push_back(event);
            view.previews.emplace_back(p->id, p);
            frames.push_back(std::move(p));
        }
    }
    auto make(std::vector<std::string> ids = {"frame-3", "frame-1", "frame-0", "frame-2"}) {
        return detail::make_perception_storyboard(view, ids, now, {});
    }
};
void delivered_evidence_window_and_barriers() {
    using namespace std::chrono_literals;
    Fixture f;
    detail::PerceptionContentEvidence ledger;
    ledger.sync(f.view);
    const auto count = [&] {
        return *ledger.project(f.now).find("retained_samples")->as_integer();
    };
    check(count() == 0); // media, events and offered pictures are not completed input
    const std::vector<std::string> two{"frame-3", "frame-1"}, one{"frame-0"};
    auto offered = ledger.offer(f.view, two, f.now);
    check(bool(offered) && count() == 0);
    check(ledger.consumed(*offered, f.now) && count() == 2);
    auto state = ledger.project(f.now);
    check(state.find("observer_span_ms")->as_integer() == 2000);
    check(state.find("largest_between_samples_ms")->as_integer() == 2000);
    check(ledger.consumed(*offered, f.now) && count() == 2); // retry is idempotent
    auto earlier = ledger.offer(f.view, one, f.now);
    check(earlier && ledger.consumed(*earlier, f.now) && count() == 3);
    state = ledger.project(f.now + 60s);
    check(state.find("observer_span_ms")->as_integer() == 3000);
    check(state.find("first_sample_age_ms")->as_integer() == 64000);
    check(state.find("last_sample_age_ms")->as_integer() == 61000);
    check(state.find("continuous_coverage")->as_boolean() == false &&
          state.find("complete_video")->as_boolean() == false &&
          state.find("audio")->as_boolean() == false);
    check(mira::to_json_string(state).size() <= 1024);
    const auto reject = [&](auto mutate) {
        auto bad = *offered;
        mutate(bad);
        check(!ledger.consumed(bad, f.now) && count() == 3);
    };
    reject([](auto &o) { ++o.generation; });
    reject([](auto &o) { ++o.source_epoch; });
    reject([](auto &o) { o.samples.clear(); });
    reject([](auto &o) { o.samples.resize(5, o.samples.front()); });
    reject([](auto &o) { o.samples.front().frame_id.assign(65, 'x'); });
    reject([](auto &o) { o.samples.front().frame_sequence = 0; });
    reject([&](auto &o) { o.samples.front().observed_at = f.now + 1ms; });
    reject([&](auto &o) { o.samples.front().observed_at = f.now - 120s; });
    reject([](auto &o) { o.samples.front().frame_id = "conflicting-id"; });
    reject([](auto &o) { o.samples.front().frame_sequence = 100; });
    auto changed = f.view;
    changed.events.front().source_epoch = 0;
    check(!ledger.offer(changed, one, f.now));
    changed = f.view;
    changed.events.front().observed_at += 1ns;
    check(!ledger.offer(changed, one, f.now));
    changed = f.view;
    changed.previews.clear();
    check(!ledger.offer(changed, one, f.now));
    check(!ledger.offer(f.view, std::vector<std::string>{"frame-0", "frame-0"}, f.now));
    check(!ledger.offer(f.view, std::vector<std::string>{}, f.now));
    const auto interrupted = [&](auto mutate, const char *reason) {
        detail::PerceptionContentEvidence local;
        local.sync(f.view);
        const auto old = local.offer(f.view, two, f.now);
        check(old && local.consumed(*old, f.now));
        auto next = f.view;
        mutate(next);
        local.sync(next);
        const auto report = local.project(f.now);
        check(*report.find("state")->as_string() == "interrupted");
        check(*report.find("last_interruption")->as_string() == reason);
        check(report.find("retained_samples")->as_integer() == 0);
        check(!local.consumed(*old, f.now));
        local.sync(f.view); // even an ABA return cannot re-admit the old offer
        check(!local.consumed(*old, f.now));
        const auto fresh = local.offer(f.view, two, f.now);
        check(fresh && local.consumed(*fresh, f.now));
        check(*local.project(f.now).find("state")->as_string() == "sparse_observation");
    };
    interrupted([](auto &v) { ++v.source_epoch; }, "source_changed");
    interrupted([](auto &v) { ++v.dropped; }, "observation_gap");
    for (const auto *status : {"failed", "stopped", "degraded"})
        interrupted([&](auto &v) { v.state = status; }, "source_unavailable");
    detail::PerceptionContentEvidence revoked;
    revoked.sync(f.view);
    const auto permitted = revoked.offer(f.view, two, f.now);
    check(permitted && revoked.consumed(*permitted, f.now));
    revoked.sync(f.view, false);
    check(revoked.project(f.now).find("retained_samples")->as_integer() == 0);
    check(*revoked.project(f.now).find("last_interruption")->as_string() == "authority_changed");
    check(!revoked.consumed(*permitted, f.now));
    detail::PerceptionContentEvidence resync;
    f.view.resync = false;
    resync.sync(f.view);
    const auto before = resync.offer(f.view, two, f.now);
    check(before && resync.consumed(*before, f.now));
    f.view.resync = true;
    resync.sync(f.view);
    check(*resync.project(f.now).find("last_interruption")->as_string() == "source_resync");
    resync.replace_source();
    resync.sync(f.view);
    check(!resync.consumed(*before, f.now));
    check(*resync.project(f.now).find("last_interruption")->as_string() == "source_replaced");
    // Long tasks retain metadata for only the latest 32 delivered samples.
    detail::PerceptionContentEvidence bounded;
    bounded.sync(f.view);
    const auto seed = *bounded.offer(f.view, one, f.now);
    for (unsigned i = 1; i <= 40; ++i) {
        auto batch = seed;
        batch.samples = {{"window-" + std::to_string(i), i, f.now - 40ms + i * 1ms}};
        check(bounded.consumed(batch, f.now));
    }
    auto window = bounded.project(f.now);
    check(window.find("retained_samples")->as_integer() == 32);
    check(window.find("history_truncated")->as_boolean() == true);
    check(window.find("observer_span_ms")->as_integer() == 31);
    auto old = seed;
    old.samples = {{"window-1", 1, f.now - 39ms}};
    check(bounded.consumed(old, f.now)); // outside retained window never counted twice
    check(bounded.project(f.now).find("retained_samples")->as_integer() == 32);
    auto lost = f.view;
    lost.state = "failed";
    bounded.sync(lost);
    const auto segment = bounded.project(f.now).find("segment")->as_integer().value();
    bounded.sync(lost);
    check(bounded.project(f.now).find("segment")->as_integer() == segment);
    check(!bounded.offer(lost, one, f.now));
}
void content_assessment_contract() {
    using namespace std::chrono_literals;
    Fixture f;
    detail::PerceptionContentEvidence ledger;
    ledger.sync(f.view);
    const std::string owner = "task/session/epoch", foreign = "other/task/epoch";
    detail::ContentTaskAssessment task;
    check(!task.result().active && !task.result().satisfied);
    check(task.assess("answerable", "answer", "sparse", {}, ledger, owner) ==
          "content_task_not_started");
    check(!task.begin("invented", false, ledger));
    check(task.begin("observed_samples", false, ledger));
    check(*task.project().find("state")->as_string() == "locating");
    const auto offered =
        *ledger.offer(f.view, std::vector<std::string>{"frame-0", "frame-1"}, f.now);
    const auto ref =
        detail::PerceptionContentEvidence::reference(owner, offered, offered.samples[0]);
    check(!ledger.contains_reference(owner, ref));
    check(task.assess("answerable", "answer", "sparse", {ref}, ledger, owner) ==
          "content_reference_not_delivered");
    check(ledger.consumed(offered, f.now));
    task.sync(ledger, owner);
    check(*task.project().find("state")->as_string() == "observing_content");
    check(ledger.contains_reference(owner, ref) && !ledger.contains_reference(foreign, ref));
    check(task.assess("answerable", "bounded answer", "sparse samples", {ref}, ledger, owner)
              .empty());
    check(task.result().satisfied && task.result().answer == "bounded answer");
    const auto preserved = mira::to_json_string(task.project());
    check(task.begin("observed_samples", false, ledger));
    check(!task.begin("entire_video", false, ledger));
    check(!task.begin("observed_samples", true, ledger));
    for (const auto &refs : std::vector<std::vector<std::string>>{{}, {"forged"}, {ref, ref}})
        check(!task.assess("answerable", "replacement", "limits", refs, ledger, owner).empty());
    check(!task.assess("answerable", "", "limits", {ref}, ledger, owner).empty());
    check(!task.assess("answerable", "x", "", {ref}, ledger, owner).empty());
    check(
        !task.assess("answerable", std::string(1025, 'x'), "limits", {ref}, ledger, owner).empty());
    check(!task.assess("answerable", "x", std::string(513, 'x'), {ref}, ledger, owner).empty());
    check(!task.assess("invented", "x", "limits", {ref}, ledger, owner).empty());
    check(mira::to_json_string(task.project()) == preserved &&
          task.result().answer == "bounded answer");
    check(ledger.consumed(offered, f.now)); // exact retry does not invalidate an assessment
    task.sync(ledger, owner);
    check(task.result().satisfied);
    auto next = *ledger.offer(f.view, std::vector<std::string>{"frame-2"}, f.now);
    check(ledger.consumed(next, f.now));
    task.sync(ledger, owner);
    check(!task.result().satisfied && task.result().answer.empty());
    check(task.result().reason == "new_content_requires_assessment");
    check(task.assess("insufficient", "ignored", "need another scene", {}, ledger, owner).empty());
    check(!task.result().satisfied && task.result().answer.empty());
    check(task.assess("answerable", "new answer", "sparse", {ref}, ledger, owner).empty());
    ++f.view.dropped;
    ledger.sync(f.view);
    task.sync(ledger, owner);
    check(task.result().reason == "content_evidence_retired" && task.result().answer.empty());
    check(task.assess("insufficient", "", "gap", {}, ledger, owner) == "content_task_interrupted");
    check(task.begin("observed_samples", false, ledger));
    check(*task.project().find("state")->as_string() == "locating");
    const auto fresh = *ledger.offer(f.view, std::vector<std::string>{"frame-0"}, f.now);
    const auto new_ref =
        detail::PerceptionContentEvidence::reference(owner, fresh, fresh.samples[0]);
    check(new_ref != ref && ledger.consumed(fresh, f.now));
    check(!ledger.contains_reference(owner, ref));
    for (bool audio : {false, true}) {
        detail::ContentTaskAssessment unavailable;
        check(unavailable.begin(audio ? "observed_samples" : "entire_video", audio, ledger));
        check(unavailable
                  .assess("answerable", "unsupported full answer", "missing coverage", {}, ledger,
                          owner)
                  .empty());
        check(!unavailable.result().satisfied && unavailable.result().answer.empty());
        check(unavailable.result().reason ==
              (audio ? "audio_evidence_unavailable" : "whole_video_coverage_unavailable"));
        check(mira::to_json_string(unavailable.project()).size() <= 512);
    }
    // At a full window, unchanged sample count must not mask a new content revision.
    detail::PerceptionContentEvidence window;
    window.sync(f.view);
    auto batch = fresh;
    batch.generation = window.generation();
    for (unsigned i = 1; i <= 32; ++i) {
        batch.samples = {{"window-" + std::to_string(i), i, f.now - 100ms + i * 1ms}};
        check(window.consumed(batch, f.now));
    }
    detail::ContentTaskAssessment bounded;
    check(bounded.begin("observed_samples", false, window));
    auto last = detail::PerceptionContentEvidence::reference(owner, batch, batch.samples[0]);
    check(bounded.assess("answerable", "last", "sparse", {last}, window, owner).empty());
    batch.samples = {{"window-33", 33, f.now - 67ms}};
    check(window.consumed(batch, f.now) && window.size() == 32);
    bounded.sync(window, owner);
    check(!bounded.result().satisfied &&
          bounded.result().reason == "new_content_requires_assessment");
    check(bounded.assess("answerable", "still retained", "sparse", {last}, window, owner).empty());
    for (unsigned i = 34; i <= 64; ++i) {
        batch.samples = {{"window-" + std::to_string(i), i, f.now - 100ms + i * 1ms}};
        check(window.consumed(batch, f.now));
    }
    bounded.sync(window, owner);
    check(bounded.result().reason == "content_evidence_retired");
    check(window.references(owner).as_array()->size() == 4);
    check(mira::to_json_string(window.references(owner)).size() <= 1024);
    check(mira::to_json_string(bounded.project()).size() <= 512);
    detail::PerceptionContentEvidence escaped;
    escaped.sync(f.view);
    batch.generation = escaped.generation();
    for (unsigned i = 1; i <= 4; ++i) {
        batch.samples = {{std::string(63, '\1') + std::to_string(i), i, f.now - 5ms + i * 1ms}};
        check(escaped.consumed(batch, f.now));
    }
    check(!escaped.references(owner).as_array()->empty());
    check(mira::to_json_string(escaped.references(owner)).size() <= 1024);
}
void automatic_supply_gate() {
    using namespace std::chrono_literals;
    detail::ContentVisualSupply gate;
    const auto now = Clock::now();
    check(gate.ready(now));
    gate.attempted(now);
    check(!gate.ready(now + 4999ms) && gate.ready(now + 5s));
    gate.completed(now + 3s);
    check(!gate.ready(now + 7999ms) && gate.ready(now + 8s));
    check(gate.project(now + 4s).find("retry_after_ms")->as_integer() == 4000);
    gate.completed(now + 1s); // completion cannot shorten a prior admission deadline
    check(!gate.ready(now + 7s));
    gate.reset();
    check(gate.ready(now));
    gate.failed(123);
    check(gate.project().find("failure_code")->as_integer() == 123);
    check(mira::to_json_string(gate.project()).size() <= 256);
    gate.reason("cooldown");
    check(!gate.project().find("failure_code")->as_integer());
    Fixture f;
    detail::PerceptionContentEvidence ledger;
    ledger.sync(f.view);
    detail::ContentTaskAssessment task;
    check(task.begin("observed_samples", false, ledger, true) && task.wants_images());
    check(!task.begin("observed_samples", false, ledger, false));
    check(task.begin("observed_samples", false, ledger, true));
    check(task.assess("insufficient", "", "sparse", {}, ledger, "owner").empty());
    check(!task.wants_images());
    ++f.view.dropped;
    ledger.sync(f.view);
    task.sync(ledger, "owner");
    check(task.interrupted() && !task.wants_images());
    check(task.begin("observed_samples", false, ledger, true) && task.wants_images());
    check(mira::to_json_string(task.project()).size() <= 512);
}
void temporal_window_selection() {
    using namespace std::chrono_literals;
    struct TemporalFixture {
        Clock::time_point now = Clock::now();
        integration::PerceptionView view;
        detail::PerceptionContentEvidence ledger;
        std::vector<std::shared_ptr<integration::PerceptionPreview>> frames;
        const std::string scope = "window/task/session/epoch";
        TemporalFixture() {
            view.source_epoch = 7;
            const std::array offsets{0, 10, 20, 400, 410, 800, 810, 1000};
            for (unsigned i = 0; i < offsets.size(); ++i) {
                auto frame = std::make_shared<integration::PerceptionPreview>();
                frame->id = "window-" + std::to_string(i);
                frame->observed_at = now - 1100ms + std::chrono::milliseconds{offsets[i]};
                frame->image.width = 4;
                frame->image.height = 2;
                frame->image.stride = 16;
                frame->image.pixels.assign(32, 1);
                integration::PerceptionEvent event;
                event.source_epoch = 7;
                event.frame_sequence = i + 1;
                event.frame_id = frame->id;
                event.observed_at = frame->observed_at;
                view.events.push_back(event);
                view.previews.emplace_back(frame->id, frame);
                frames.push_back(std::move(frame));
            }
            ledger.sync(view);
        }
        auto select(std::chrono::milliseconds window = 1100ms, std::size_t limit = 4,
                    const mirage::desktop::CancelToken &cancel = {}) {
            return detail::select_temporal_frames(view, ledger, scope, window, limit, now, cancel);
        }
    } f;
    const auto selected = f.select();
    check(selected.reason.empty() && selected.eligible == 8);
    check(selected.ids == std::vector<std::string>{"window-0", "window-4", "window-5", "window-7"});
    check(f.select(1100ms, 3).ids == std::vector<std::string>{"window-0", "window-4", "window-7"});
    check(f.select(1100ms, 2).ids == std::vector<std::string>{"window-0", "window-7"});
    check(f.select(500ms).ids == std::vector<std::string>{"window-5", "window-6", "window-7"});
    check(f.select(100ms).reason == "need_two_new_temporal_frames");
    check(f.select(99ms).reason == "invalid_argument");
    check(f.select(120001ms).reason == "invalid_argument");
    check(f.select(1100ms, 1).reason == "invalid_argument");
    check(f.select(1100ms, 5).reason == "invalid_argument");
    check(f.select().ids == selected.ids); // offering alone never marks candidates read
    auto offered = *f.ledger.offer(f.view, selected.ids, f.now);
    check(f.ledger.consumed(offered, f.now));
    auto remaining = f.select();
    check(remaining.eligible == 4 &&
          remaining.ids ==
              std::vector<std::string>{"window-1", "window-2", "window-3", "window-6"});
    check(f.ledger.consumed(*f.ledger.offer(f.view, remaining.ids, f.now), f.now));
    check(f.select().reason == "no_new_temporal_frames");
    // A failed ACK may retain an old preview past the 32-sample ledger window.
    // Unknown history is never re-labelled as a novel temporal sample.
    auto rolling = offered;
    for (unsigned i = 0; i < 40; ++i) {
        rolling.samples = {{"later-" + std::to_string(i), 100 + i, f.now - 40ms + i * 1ms}};
        check(f.ledger.consumed(rolling, f.now));
    }
    check(f.select().reason == "no_new_temporal_frames");
    // Equal-distance candidates choose the earlier observer time deterministically.
    TemporalFixture tied;
    for (unsigned i = 0; i < 8; ++i) {
        tied.frames[i]->observed_at = tied.now - 800ms + i * 100ms;
        tied.view.events[i].observed_at = tied.frames[i]->observed_at;
    }
    check(tied.select(1100ms, 3).ids ==
          std::vector<std::string>{"window-0", "window-3", "window-7"});
    const auto filtered = [](auto mutate) {
        TemporalFixture local;
        mutate(local);
        const auto result = local.select(120000ms);
        check(result.reason.empty() && result.eligible == 7);
    };
    filtered([](auto &x) { x.frames[0].reset(); });
    filtered([](auto &x) { x.frames[0]->observed_at = x.now + 1ms; });
    filtered([](auto &x) { x.frames[0]->observed_at = x.now - 120s; });
    filtered([](auto &x) { x.view.events[0].source_epoch = 0; });
    filtered([](auto &x) { x.view.events[0].frame_sequence = 0; });
    filtered([](auto &x) { x.view.events[0].observed_at += 1ms; });
    const auto rejected = [](auto mutate, const char *reason) {
        TemporalFixture local;
        mutate(local);
        const auto result = local.select();
        check(result.ids.empty() && result.reason == reason);
    };
    rejected([](auto &x) { x.view.previews.push_back(x.view.previews[0]); },
             "source_capacity_exceeded");
    rejected([](auto &x) { x.view.events.resize(257); }, "source_capacity_exceeded");
    rejected([](auto &x) { x.frames[4]->image.width = 5; }, "source_geometry_changed");
    rejected([](auto &x) { x.view.events[4].frame_sequence = 1; }, "ambiguous_temporal_order");
    rejected([](auto &x) { x.view.previews[1] = x.view.previews[0]; }, "ambiguous_temporal_order");
    rejected(
        [](auto &x) {
            x.frames[1]->observed_at = x.frames[0]->observed_at;
            x.view.events[1].observed_at = x.frames[1]->observed_at;
        },
        "ambiguous_temporal_order");
    rejected([](auto &x) { ++x.view.source_epoch; }, "source_unavailable");
    rejected([](auto &x) { x.ledger.sync(x.view, false); }, "source_unavailable");
    unsigned checks_until_cancel = 0;
    const auto stopped = tied.select(
        1100ms, 4, mirage::desktop::CancelToken{[&] { return ++checks_until_cancel > 3; }});
    check(stopped.ids.empty() && stopped.reason == "cancelled");
    // New source generation cannot treat the old segment's delivery as a current read.
    ++f.view.dropped;
    f.ledger.sync(f.view);
    check(f.select().eligible == 8 && f.select().ids == selected.ids);
    const auto board = detail::make_perception_storyboard(f.view, selected.ids, f.now, {});
    check(board && board.value().image.width == 640 && board.value().image.height == 640);
}
void pixels_and_time() {
    Fixture f;
    auto board = f.make();
    check(static_cast<bool>(board));
    const auto &image = board.value().image;
    check(image.width == 640 && image.height == 640 && image.pixels.size() == 640 * 640 * 4);
    const mira::JsonValue meta{board.value().metadata};
    const auto &tiles = *meta.find("tiles")->as_array();
    check(tiles.size() == 4 && *meta.find("sparse")->as_boolean() &&
          !*meta.find("audio")->as_boolean() && !*meta.find("complete_video")->as_boolean());
    check(*meta.find("dropped")->as_integer() == 3 && *meta.find("filtered")->as_integer() == 4 &&
          *meta.find("resync")->as_boolean());
    for (int i = 0; i < 4; ++i) {
        const auto &tile = tiles[std::size_t(i)];
        check(*tile.find("frame_id")->as_string() == "frame-" + std::to_string(i));
        check(*tile.find("offset_ms")->as_integer() == i * 1000 &&
              *tile.find("age_ms")->as_integer() == (4 - i) * 1000);
        const int x = (i % 2) * 320, y = (i / 2) * 320 + 80;
        check(*tile.find("x")->as_integer() == x && *tile.find("y")->as_integer() == y &&
              *tile.find("width")->as_integer() == 320 &&
              *tile.find("height")->as_integer() == 160);
        for (int row = 0; row < 160; ++row)
            for (int col = 0; col < 320; ++col)
                check(
                    image.pixels[std::size_t(y + row) * image.stride + std::size_t(x + col) * 4] ==
                    20 + i * 40);
    }
    check(image.pixels[0] == 0 && image.pixels[3] == 255); // letterbox
    auto two = f.make({"frame-0", "frame-3"});
    check(two && two.value().image.height == 320);
    mira::JsonValue two_meta{two.value().metadata};
    check(*two_meta.find("tiles")->as_array()->back().find("since_previous_ms")->as_integer() ==
          3000);
    auto three = f.make({"frame-0", "frame-1", "frame-2"});
    check(three && three.value().image.pixels.back() == 255 &&
          three.value().image.pixels[639 * three.value().image.stride + 639 * 4] == 0);
}
void boundaries() {
    Fixture f;
    check(!f.make({}) && !f.make({"frame-0"}));
    check(!f.make({"frame-0", "frame-0"}));
    check(!f.make({"frame-0", "frame-1", "frame-2", "frame-3", "frame-4"}));
    auto unavailable = [&](const char *reason) {
        auto board = f.make({"frame-0", "frame-1"});
        check(board && board.value().image.pixels.empty());
        mira::JsonValue meta{board.value().metadata};
        check(!*meta.find("available")->as_boolean() &&
              *meta.find("reason")->as_string() == reason);
    };
    f.frames[0]->observed_at = f.now - std::chrono::seconds{120};
    unavailable("expired_consumed_or_evicted");
    f.frames[0]->observed_at = f.now + std::chrono::seconds{1};
    unavailable("expired_consumed_or_evicted");
    f.frames[0]->observed_at = f.now - std::chrono::seconds{4};
    f.view.source_epoch = 3;
    unavailable("source_epoch_or_event_unavailable");
    f.view.source_epoch = 2;
    f.frames[0]->image.width = 320;
    unavailable("source_geometry_changed");
    f.frames[0]->image.width = 640;
    f.frames[0]->image.stride = 1;
    check(!f.make());
    f.frames[0]->image.stride = 640 * 4;
    f.view.events[1].frame_sequence = 1;
    unavailable("ambiguous_temporal_order");
    f.view.events[1].frame_sequence = 2;
    f.frames[1]->observed_at = f.frames[0]->observed_at;
    f.view.events[1].observed_at = f.frames[0]->observed_at;
    unavailable("ambiguous_temporal_order");
    f.frames[0].reset();
    unavailable("expired_consumed_or_evicted");
    auto cancelled = detail::make_perception_storyboard(
        f.view, {"frame-0", "frame-1"}, f.now, mirage::desktop::CancelToken{[] { return true; }});
    check(!cancelled && cancelled.error().code == mira::ErrorCode::Cancelled);
    Fixture small;
    for (auto &frame : small.frames) {
        frame->image.width = 8;
        frame->image.height = 4;
        frame->image.stride = 32;
        frame->image.pixels.resize(128);
    }
    auto board = small.make();
    check(static_cast<bool>(board));
    mira::JsonValue meta{board.value().metadata};
    check(*meta.find("tiles")->as_array()->front().find("width")->as_integer() == 8);
}
} // namespace
int main() {
    try {
        delivered_evidence_window_and_barriers();
        content_assessment_contract();
        automatic_supply_gate();
        temporal_window_selection();
        pixels_and_time();
        boundaries();
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
