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
    MIRAGE_CHECK(!semantics.annotate(original, "搜索框").ok); // not yet consumed
    semantics.consumed("host-request-1");
    MIRAGE_CHECK(!semantics.annotate("invented", "搜索框").ok);
    journal.append(sample(2, "Search", 50), {}, now);
    semantics.sync(journal.view());
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
} // namespace
int main() {
    inheritance_and_late_results();
    reset_gap_budget_and_expiry();
    return mirage::testing::finish("perception_semantics");
}
