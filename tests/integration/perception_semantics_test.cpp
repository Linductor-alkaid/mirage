#include "../../integration/mira/src/perception_event_batch.hpp"
#include "../../integration/mira/src/perception_semantics.hpp"
#include "../../integration/mirador/src/perception_journal.hpp"
#include "../support/test.hpp"
#include <set>

namespace {
using namespace mirage::integration;
using Json = mira::JsonValue;
using Clock = std::chrono::steady_clock;
PerceptionSample sample(std::uint64_t frame, const std::string &text = "Search", int x = 10) {
    PerceptionSample s;
    s.ok = s.objects_complete = true;
    s.source_epoch = 1;
    s.frame_sequence = frame;
    PerceptionObject entry;
    entry.content_observed = true;
    entry.object.id = 7;
    entry.object.incarnation = 1;
    entry.object.source = mirage::desktop::VisualRegionSource::kOcr;
    entry.object.bounds = {x, 10, 80, 20};
    entry.object.text = text;
    entry.object.confidence = .9;
    entry.object.identity_confirmed = true;
    s.objects.push_back(entry);
    return s;
}
const Json &first(const Json &projection) {
    return projection.find("objects")->as_array()->front();
}
std::string ref(const Json &projection) {
    return *first(projection).find("object_ref")->as_string();
}
std::string state(const Json &projection) {
    return *first(projection).find("semantic_state")->as_string();
}
void inheritance_and_late_results() {
    detail::PerceptionJournal journal("source");
    detail::PerceptionSemantics semantics;
    const auto now = Clock::now();
    journal.append(sample(1), {}, now);
    semantics.sync(journal.view());
    const auto original = ref(semantics.project());
    MIRAGE_CHECK(!semantics.attention_binding(original));
    MIRAGE_CHECK(!semantics.annotate(original, "搜索框").ok); // not yet consumed
    semantics.consumed("host-request-1");
    MIRAGE_CHECK(semantics.attention_binding(original));
    MIRAGE_CHECK(!semantics.annotate("invented", "搜索框").ok);
    journal.append(sample(2, "Search", 50), {}, now);
    semantics.sync(journal.view());
    MIRAGE_CHECK(semantics.attention_binding(original));     // same version may move
    MIRAGE_CHECK(semantics.annotate(original, "搜索框").ok); // moved while model thought
    auto view = semantics.project();
    MIRAGE_CHECK(state(view) == "active");
    MIRAGE_CHECK(*first(view).find("semantic_label")->as_string() == "搜索框");
    MIRAGE_CHECK(first(view).find("x")->as_integer() == 50);
    MIRAGE_CHECK(first(view).find("annotation_frame")->as_integer() == 1);
    MIRAGE_CHECK(*first(view).find("annotation_request_id")->as_string() == "host-request-1");
    const auto moved = ref(view);
    semantics.consumed("host-request-2");
    // Changes outside notification focus still invalidate object semantics.
    journal.focus({0, 0, 1, 1});
    journal.append(sample(3, "Delete", 50), {}, now);
    semantics.sync(journal.view());
    MIRAGE_CHECK(!semantics.attention_binding(moved));
    MIRAGE_CHECK(!semantics.annotate(moved, "过时解释").ok);
    view = semantics.project();
    MIRAGE_CHECK(state(view) == "suspended");
    MIRAGE_CHECK(*first(view).find("text")->as_string() == "Delete");
    const auto fresh = ref(view);
    semantics.consumed("host-request-3");
    MIRAGE_CHECK(!semantics.annotate(fresh, "").ok);
    MIRAGE_CHECK(!semantics.annotate(fresh, std::string(1025, 'a')).ok);
    MIRAGE_CHECK(semantics.annotate(fresh, "删除按钮").ok);
    auto uncertain = sample(4, "Delete", 50);
    uncertain.objects[0].content_observed = false;
    journal.append(uncertain, {}, now);
    semantics.sync(journal.view());
    MIRAGE_CHECK(state(semantics.project()) == "suspended");
    journal.append(sample(5, "Delete", 51), {}, now);
    semantics.sync(journal.view());
    MIRAGE_CHECK(state(semantics.project()) == "active");
    // ABA in between model requests must not inherit an old meaning.
    journal.append(sample(6, "Search"), {}, now);
    journal.append(sample(7, "Delete"), {}, now);
    semantics.sync(journal.view());
    MIRAGE_CHECK(state(semantics.project()) == "suspended");
}
void reset_gap_budget_and_expiry() {
    detail::PerceptionJournal journal("source");
    detail::PerceptionSemantics semantics;
    const auto now = Clock::now();
    journal.append(sample(1), {}, now);
    semantics.sync(journal.view());
    auto current = ref(semantics.project());
    semantics.consumed("request");
    MIRAGE_CHECK(semantics.annotate(current, "search").ok);
    auto gap = sample(2);
    gap.resync = true;
    journal.append(gap, {}, now);
    semantics.sync(journal.view());
    MIRAGE_CHECK(state(semantics.project()) == "suspended");
    auto replacement = sample(3);
    replacement.objects[0].object.incarnation = 2;
    journal.append(replacement, {}, now);
    semantics.sync(journal.view());
    MIRAGE_CHECK(state(semantics.project()) == "none");
    auto source_reset = sample(4);
    source_reset.source_epoch = 2;
    journal.append(source_reset, {}, now);
    semantics.sync(journal.view());
    MIRAGE_CHECK(!semantics.annotate(current, "stale").ok);
    const auto old_revision = journal.view().scene->updated.front().object.content_revision;
    journal.clear();
    auto after_failure = sample(5);
    after_failure.source_epoch = 2;
    journal.append(after_failure, {}, now);
    MIRAGE_CHECK(journal.view().scene->updated.front().object.content_revision > old_revision);
    auto expired = journal.view();
    expired.scene_observed_at = now - std::chrono::seconds(120);
    semantics.sync(expired);
    MIRAGE_CHECK(semantics.project().find("objects")->as_array()->empty());
    auto resident = expired;
    resident.state = "sampling";
    resident.frames = 500;
    semantics.sync(resident, now);
    MIRAGE_CHECK(!semantics.project().find("objects")->as_array()->empty());
    // Source progress preserves advisory identity without relabelling old OCR as fresh.
    semantics.sync(resident, now + std::chrono::seconds(6));
    MIRAGE_CHECK(semantics.project().find("objects")->as_array()->empty());
    auto big = sample(6);
    big.source_epoch = 2;
    for (std::uint64_t i = 8; i < 100; ++i) {
        auto entry = big.objects.front();
        entry.object.id = i;
        big.objects.push_back(entry);
    }
    journal.append(big, {}, now);
    semantics.sync(journal.view());
    auto projection = semantics.project();
    MIRAGE_CHECK(projection.find("truncated")->as_boolean() == true);
    MIRAGE_CHECK(projection.find("objects")->as_array()->size() <= 16);
    MIRAGE_CHECK(mira::to_json_string(projection).size() < 3500);
    semantics.consumed("budget-request");
    MIRAGE_CHECK(!semantics.annotate(current, "evicted request").ok);
    std::set<std::int64_t> labeled;
    for (int round = 0; round < 128 && labeled.size() < big.objects.size(); ++round) {
        const auto next = semantics.project();
        semantics.consumed("capacity-request");
        MIRAGE_CHECK(mira::to_json_string(next).size() < 3500);
        for (const auto &object : *next.find("objects")->as_array()) {
            if (*object.find("semantic_state")->as_string() != "none")
                continue;
            MIRAGE_CHECK(
                semantics.annotate(*object.find("object_ref")->as_string(), "test meaning").ok);
            labeled.insert(*object.find("entity")->as_integer());
        }
    }
    MIRAGE_CHECK(labeled.size() == big.objects.size()); // existing labels cannot starve candidates
    auto bad = sample(7);
    bad.source_epoch = 2;
    bad.objects[0].object.text.assign(4097, 'x');
    journal.append(bad, {}, now);
    MIRAGE_CHECK(!journal.view().scene && !journal.view().scene_error.empty());
    semantics.sync(journal.view());
    MIRAGE_CHECK(semantics.project().find("objects")->as_array()->empty());
    auto over_capacity = sample(8);
    over_capacity.source_epoch = 2;
    over_capacity.objects.resize(257, over_capacity.objects.front());
    journal.append(over_capacity, {}, now);
    MIRAGE_CHECK(!journal.view().scene &&
                 journal.view().scene_error == "object_projection_budget_exceeded");
}
void deferred_evidence_never_renews_or_reactivates_labels() {
    detail::PerceptionJournal journal("deferred");
    detail::PerceptionSemantics semantics;
    const auto recognized = Clock::now() - std::chrono::seconds(2);
    journal.append(sample(1), {}, recognized);
    semantics.sync(journal.view());
    const auto current = ref(semantics.project());
    semantics.consumed("recognized-request");
    MIRAGE_CHECK(semantics.annotate(current, "搜索框").ok);
    const auto revision = journal.view().scene->updated.front().object.content_revision;
    auto deferred = sample(2);
    deferred.recognition_deferred = true;
    deferred.recognition_reason = "media_deferred_unknown";
    deferred.objects[0].content_observed = false;
    deferred.objects[0].invalidate_content = true;
    journal.append(deferred, {}, Clock::now());
    semantics.sync(journal.view());
    auto projection = semantics.project();
    MIRAGE_CHECK(projection.find("recognition_deferred")->as_boolean() == true);
    MIRAGE_CHECK(projection.find("age_ms")->as_integer() >= 2000);
    MIRAGE_CHECK(journal.view().scene_observed_at == recognized);
    MIRAGE_CHECK(state(projection) == "suspended" && ref(projection).empty());
    MIRAGE_CHECK(*first(projection).find("text")->as_string() == "Search");
    MIRAGE_CHECK(!semantics.annotate(current, "stale result").ok);
    const auto invalidated = journal.view().scene->updated.front().object.content_revision;
    MIRAGE_CHECK(invalidated > revision);
    deferred.frame_sequence = 3;
    deferred.objects[0].invalidate_content = false;
    journal.append(deferred, {}, Clock::now());
    MIRAGE_CHECK(journal.view().scene->updated.front().object.content_revision == invalidated);
    // Identical text after an unknown interval cannot prove ABA did not occur.
    journal.append(sample(4), {}, Clock::now());
    semantics.sync(journal.view());
    projection = semantics.project();
    MIRAGE_CHECK(projection.find("recognition_deferred")->as_boolean() == false);
    MIRAGE_CHECK(state(projection) == "suspended" && !ref(projection).empty());
    MIRAGE_CHECK(!semantics.annotate(current, "stale result").ok);
    const auto fresh = ref(projection);
    semantics.consumed("refreshed-request");
    MIRAGE_CHECK(semantics.annotate(fresh, "重新确认的搜索框").ok);
    MIRAGE_CHECK(state(semantics.project()) == "active");
    journal.clear();
    MIRAGE_CHECK(!journal.view().recognition_deferred && journal.view().recognition_reason.empty());
}
void lossless_event_batching_and_prefixes() {
    PerceptionView view;
    const auto now = Clock::now();
    for (unsigned n = 1; n <= 24; ++n) {
        PerceptionEvent event;
        event.sequence = event.frame_sequence = n;
        event.source_epoch = event.entity = event.instance_generation = 1;
        event.confirmed = true;
        event.kind = "position_or_appearance_changed";
        event.text = "same recognized text, possible different appearance";
        event.observed_at = now - std::chrono::milliseconds(240 - n * 10);
        event.x = n;
        event.y = 2;
        event.width = 3;
        event.height = 4;
        view.events.push_back(event);
    }
    auto barrier = view.events.back();
    barrier.sequence = 25;
    barrier.kind = "text_changed";
    barrier.text = "Important transition";
    view.events.push_back(barrier);
    view.newest = 25;
    std::vector<std::shared_ptr<PerceptionPreview>> retained;
    for (unsigned n = 0; n < 8; ++n) {
        auto preview = std::make_shared<PerceptionPreview>();
        preview->id = "batch-frame-" + std::to_string(n + 1);
        view.events[n].frame_id = preview->id;
        preview->observed_at = view.events[n].observed_at;
        view.previews.emplace_back(preview->id, preview);
        retained.push_back(std::move(preview));
    }
    auto batch = detail::perception_event_batch(view, 0, now);
    MIRAGE_CHECK(batch.rows.size() == 4 && batch.cursor == 25);
    // Signed to match mira/json's as_integer() (optional<int64_t>): MSVC
    // raises the heterogeneous comparison against uint64_t as a
    // signed/unsigned mismatch (C4389) under warnings-as-errors.
    std::int64_t restored = 0;
    for (const auto &row : batch.rows) {
        if (const auto *prior = row.find("prior_samples")) {
            MIRAGE_CHECK(prior->as_array()->size() == 7);
            for (const auto &point : *prior->as_array()) {
                ++restored;
                MIRAGE_CHECK(point.find("cursor")->as_integer() == restored);
                MIRAGE_CHECK(point.find("y")->as_number() == 2);
                MIRAGE_CHECK(point.find("width")->as_number() == 3);
                MIRAGE_CHECK(point.find("height")->as_number() == 4);
                if (restored <= 8) {
                    MIRAGE_CHECK(*point.find("frame_id")->as_string() ==
                                 "batch-frame-" + std::to_string(restored));
                    MIRAGE_CHECK(point.find("preview_available")->as_boolean() == true);
                }
                MIRAGE_CHECK(point.find("x")->as_number() == double(restored));
                MIRAGE_CHECK(point.find("frame_sequence")->as_integer() == restored);
                MIRAGE_CHECK(point.find("age_ms")->as_integer() ==
                             240 - std::int64_t(restored) * 10);
            }
        }
        ++restored;
        MIRAGE_CHECK(row.find("cursor")->as_integer() == restored);
    }
    MIRAGE_CHECK(restored == 25);
    Json::Array flat;
    for (const auto &event : view.events) {
        auto one = view;
        one.events = {event};
        auto encoded = detail::perception_event_batch(one, 0, now);
        flat.push_back(encoded.rows.front());
    }
    const auto flat_bytes = mira::to_json_string(Json{flat}).size();
    const auto batch_bytes = mira::to_json_string(Json{batch.rows}).size();
    MIRAGE_CHECK(batch_bytes < flat_bytes);
    std::fprintf(stderr, "event batch: 25 observations, 4 rows, flat=%zu B, batch=%zu B\n",
                 flat_bytes, batch_bytes);

    MIRAGE_CHECK(*batch.rows.back().find("text")->as_string() == "Important transition");
    // Every non-motion kind, gap, identity, source, text, confirmation and
    // ordering change is a boundary; no semantic event is discarded.
    for (const auto *kind : {"appeared", "disappeared", "text_changed", "relation_changed",
                             "source_reset", "observed_unconfirmed", "media_keyframe"}) {
        auto changed = view;
        changed.events[1].kind = kind;
        auto split = detail::perception_event_batch(changed, 0, now);
        MIRAGE_CHECK(!split.rows[0].find("prior_samples"));
        MIRAGE_CHECK(*split.rows[1].find("kind")->as_string() == kind);
    }
    const auto boundary = [&](auto mutate) {
        auto changed = view;
        mutate(changed.events[1]);
        auto split = detail::perception_event_batch(changed, 0, now);
        MIRAGE_CHECK(!split.rows.front().find("prior_samples"));
    };
    boundary([](auto &e) { ++e.entity; });
    boundary([](auto &e) { ++e.instance_generation; });
    boundary([](auto &e) { ++e.source_epoch; });
    boundary([](auto &e) { e.text = "new content"; });
    boundary([](auto &e) { e.confirmed = false; });
    boundary([](auto &e) { e.temporal_keyframe = true; });
    auto typed = view;
    typed.events.front().temporal_keyframe = true;
    const auto marked = detail::perception_event_batch(typed, 0, now);
    MIRAGE_CHECK(marked.rows.front().find("temporal_keyframe")->as_boolean() == true);
    MIRAGE_CHECK(!batch.rows.front().find("temporal_keyframe"));
    boundary([](auto &e) { ++e.sequence; });
    boundary([](auto &e) { e.observed_at -= std::chrono::seconds(2); });
    boundary([](auto &e) { e.observed_at += std::chrono::seconds(2); });
    boundary([](auto &e) { e.frame_sequence = 0; });
    auto resync = view;
    resync.resync = true;
    const auto singles = detail::perception_event_batch(resync, 0, now);
    MIRAGE_CHECK(singles.rows.size() <= 16 && singles.cursor < 25);
    MIRAGE_CHECK(!singles.rows.front().find("prior_samples"));
    // A full journal forces byte-budget shortening. Every raw event must
    // remain recoverable across the successive acknowledged prefixes.
    const auto prototype = view.events.front();
    view.events.clear();
    for (unsigned n = 1; n <= 96; ++n) {
        auto e = prototype;
        e.frame_id.clear();
        e.sequence = e.frame_sequence = n;
        e.text.assign(256, 'x');
        e.observed_at = now - std::chrono::milliseconds(480 - n * 5);
        view.events.push_back(std::move(e));
    }
    barrier.sequence = 97;
    view.events.push_back(barrier);
    view.newest = 97;
    MIRAGE_CHECK(detail::perception_event_batch(view, 0, now).cursor < view.newest);
    std::uint64_t after = 0;
    unsigned total = 0;
    for (unsigned attempt = 0; after < view.newest && attempt < 97; ++attempt) {
        const auto next = detail::perception_event_batch(view, after, now);
        MIRAGE_CHECK(!next.rows.empty() && next.cursor > after);
        if (next.rows.empty())
            break;
        MIRAGE_CHECK(mira::to_json_string(Json{next.rows}).size() <= 7000);
        for (const auto &row : next.rows) {
            if (const auto *prior = row.find("prior_samples"))
                for (const auto &point : *prior->as_array())
                    MIRAGE_CHECK(point.find("cursor")->as_integer() == ++total);
            MIRAGE_CHECK(row.find("cursor")->as_integer() == ++total);
        }
        after = next.cursor;
    }
    MIRAGE_CHECK(total == 97 && after == 97);
    MIRAGE_CHECK(view.events.size() == 97); // source log unchanged
}
} // namespace
int main() {
    lossless_event_batching_and_prefixes();
    inheritance_and_late_results();
    reset_gap_budget_and_expiry();
    deferred_evidence_never_renews_or_reactivates_labels();
    return mirage::testing::finish("perception_semantics");
}
