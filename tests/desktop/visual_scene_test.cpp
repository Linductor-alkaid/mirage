#include "../support/test.hpp"

#include <mirage/desktop/visual_scene.hpp>

#include <limits>
#include <vector>

using namespace mirage::desktop;

namespace {
VisualSceneObject object(std::uint64_t id = 1) {
    VisualSceneObject o;
    o.id = id;
    o.incarnation = o.content_revision = 1;
    o.bounds = {10, 20, 80, 24};
    o.source = VisualRegionSource::kOcr;
    o.text = "Save";
    o.confidence = 0.9;
    o.identity_confirmed = true;
    return o;
}
VisualSceneDelta frame(VisualScene &s, std::uint64_t sequence,
                       const std::vector<VisualSceneObject> &objects, bool full = false,
                       std::uint64_t context = 1, const std::vector<std::uint64_t> &removed = {}) {
    return s.apply({"source-session", context, sequence, s.revision(), full, objects, removed});
}
VisualSemanticBinding binding(std::uint64_t id = 1) {
    return {"source-session", "model-request", "保存按钮", 1, id, 1, 1, 1};
}

void incremental_retention_and_movement() {
    VisualScene s("source-session");
    auto a = object(), b = object(2);
    auto delta = frame(s, 1, {a, b}, true);
    MIRAGE_CHECK(delta.ok && delta.reset && delta.updated.size() == 2);
    MIRAGE_CHECK(s.annotate(binding()).ok);
    const auto *untouched = s.find(1);
    const auto initial_bytes = s.retained_bytes();
    b.text = "Delete";
    ++b.content_revision;
    delta = frame(s, 2, {b});
    MIRAGE_CHECK(delta.ok && delta.updated.size() == 1 && delta.updated[0].object.id == 2);
    MIRAGE_CHECK(s.find(1) == untouched); // unchanged entries were not rebuilt
    MIRAGE_CHECK(s.find(1)->binding->label == "保存按钮");
    MIRAGE_CHECK(s.find(1)->semantic_state == VisualSemanticState::kActive);
    MIRAGE_CHECK(s.find(1)->content_sequence == 1);
    a.bounds.x += 100;
    delta = frame(s, 3, {a});
    MIRAGE_CHECK(delta.ok && delta.updated.size() == 1);
    MIRAGE_CHECK(s.find(1)->binding->frame_sequence == 1);
    MIRAGE_CHECK(s.find(1)->content_sequence == 1);
    MIRAGE_CHECK(s.find(1)->semantic_state == VisualSemanticState::kActive);
    // A delayed annotation grounded in frame 1 may follow the same object,
    // but never a new object that appeared later at the same coordinates.
    auto late = binding();
    late.label = "保存当前文件";
    MIRAGE_CHECK(s.annotate(late).ok);
    const auto revision = s.revision();
    delta = frame(s, 100, {a, b}, true); // skipped capture sequences
    MIRAGE_CHECK(delta.ok && delta.updated.empty() && delta.removed.empty());
    MIRAGE_CHECK(s.revision() == revision);
    MIRAGE_CHECK(s.retained_bytes() < initial_bytes + 100);
    const auto snapshot = s.snapshot();
    MIRAGE_CHECK(snapshot.ok && snapshot.reset && snapshot.updated.size() == 2);
    MIRAGE_CHECK(snapshot.frame_sequence == 100);
}

void invalidate_suspend_and_recover() {
    VisualScene s("source-session");
    auto a = object();
    MIRAGE_CHECK(frame(s, 1, {a}, true).ok);
    MIRAGE_CHECK(s.annotate(binding()).ok);
    a.identity_confirmed = false;
    MIRAGE_CHECK(frame(s, 2, {a}).ok);
    MIRAGE_CHECK(s.find(1)->semantic_state == VisualSemanticState::kSuspended);
    MIRAGE_CHECK(s.annotate(binding()).error.code == "semantic_identity_unconfirmed");
    a.identity_confirmed = true;
    a.pending = true;
    MIRAGE_CHECK(frame(s, 3, {a}).ok);
    MIRAGE_CHECK(s.find(1)->semantic_state == VisualSemanticState::kSuspended);
    a.pending = false;
    MIRAGE_CHECK(frame(s, 4, {a}).ok);
    MIRAGE_CHECK(s.find(1)->semantic_state == VisualSemanticState::kActive);
    a.text = "Delete";
    MIRAGE_CHECK(frame(s, 5, {a}).error.code == "stale_scene_content");
    MIRAGE_CHECK(s.find(1)->object.text == "Save");
    ++a.content_revision;
    MIRAGE_CHECK(frame(s, 6, {a}).ok);
    MIRAGE_CHECK(s.find(1)->semantic_state == VisualSemanticState::kSuspended);
    MIRAGE_CHECK(s.find(1)->binding->label == "保存按钮"); // retained, not active
    MIRAGE_CHECK(s.annotate(binding()).error.code == "stale_semantic_binding");
    auto fresh = binding();
    fresh.label = "删除按钮";
    fresh.content_revision = 2;
    fresh.frame_sequence = 6;
    MIRAGE_CHECK(s.annotate(fresh).ok);
    MIRAGE_CHECK(s.find(1)->object.text == "Delete"); // model label did not overwrite OCR
    MIRAGE_CHECK(s.find(1)->semantic_state == VisualSemanticState::kActive);
}

void removal_replacement_and_scope() {
    VisualScene s("source-session");
    auto a = object();
    MIRAGE_CHECK(frame(s, 1, {a}, true).ok);
    MIRAGE_CHECK(s.annotate(binding()).ok);
    a.incarnation = 2;
    MIRAGE_CHECK(frame(s, 2, {a}).ok);
    MIRAGE_CHECK(!s.find(1)->binding);
    MIRAGE_CHECK(!s.annotate(binding()).ok);
    auto replacement = binding();
    replacement.incarnation = 2;
    MIRAGE_CHECK(!s.annotate(replacement).ok); // image predates this incarnation
    replacement.frame_sequence = 2;
    MIRAGE_CHECK(s.annotate(replacement).ok);
    auto removed = frame(s, 3, {}, false, 1, {1});
    MIRAGE_CHECK(removed.ok && removed.removed == std::vector<std::uint64_t>{1});
    MIRAGE_CHECK(s.size() == 0 && s.retained_bytes() == 0);
    // Even an erroneous reuse of the incarnation cannot accept the old label:
    // its grounding image predates the reintroduced object.
    MIRAGE_CHECK(frame(s, 4, {a}).ok);
    MIRAGE_CHECK(!s.annotate(replacement).ok);
    auto reset = frame(s, 5, {a}, true, 2);
    MIRAGE_CHECK(reset.ok && reset.reset && !s.find(1)->binding);
    MIRAGE_CHECK(!s.annotate(replacement).ok);
    MIRAGE_CHECK(!frame(s, 6, {a}, true, 1).ok);
    auto foreign = binding();
    foreign.source_session = "other-source";
    MIRAGE_CHECK(!s.annotate(foreign).ok);
    const auto revision = s.revision();
    MIRAGE_CHECK(!s.apply({"other-source", 3, 7, revision, true, {}, {}}).ok);
    MIRAGE_CHECK(s.revision() == revision);
}

void rejection_is_atomic_and_input_bounded() {
    VisualScene s("source-session", {2, 4096, 4096, 1024});
    auto a = object(), b = object(2), c = object(3);
    MIRAGE_CHECK(frame(s, 1, {a, b}, true).ok);
    MIRAGE_CHECK(s.annotate(binding()).ok);
    const auto revision = s.revision(), retained = s.retained_bytes();
    a.bounds.x = 20;
    MIRAGE_CHECK(!frame(s, 2, {a, c}).ok); // cannot commit a before failing c
    MIRAGE_CHECK(s.find(1)->object.bounds.x == 10);
    MIRAGE_CHECK(s.find(1)->semantic_state == VisualSemanticState::kActive);
    MIRAGE_CHECK(!frame(s, 2, {a, a}, true).ok);
    MIRAGE_CHECK(!frame(s, 2, {a}, false, 1, {1}).ok);
    MIRAGE_CHECK(!frame(s, 2, {}, false, 1, {99}).ok);
    MIRAGE_CHECK(!s.apply({"source-session", 1, 2, revision - 1, false, {}, {}}).ok);
    b.text.assign(4096, 'x');
    ++b.content_revision;
    MIRAGE_CHECK(frame(s, 2, {a, b}, true).error.code == "scene_budget_exceeded");
    MIRAGE_CHECK(s.revision() == revision && s.retained_bytes() == retained);
    MIRAGE_CHECK(s.find(1)->object.bounds.x == 10);
    b.text = std::string(1, '\x80');
    MIRAGE_CHECK(!frame(s, 2, {b}).ok);
    b = object(2);
    b.confidence = std::numeric_limits<double>::quiet_NaN();
    MIRAGE_CHECK(!frame(s, 2, {b}).ok);
    b = object(2);
    b.bounds.width = 0;
    MIRAGE_CHECK(!frame(s, 2, {b}).ok);
    auto bad = binding();
    bad.label = std::string("x\0y", 3);
    MIRAGE_CHECK(!s.annotate(bad).ok);
    bad.label.assign(1025, 'x');
    MIRAGE_CHECK(!s.annotate(bad).ok);
    MIRAGE_CHECK(s.revision() == revision && s.retained_bytes() == retained);
    // A full update removes absent objects, leaving their annotations behind
    // neither in the scene nor in a hidden retained history.
    auto delta = frame(s, 3, {}, true);
    MIRAGE_CHECK(delta.ok && delta.removed.size() == 2);
    MIRAGE_CHECK(s.size() == 0 && s.retained_bytes() == 0);
    VisualScene invalid("source-session", {0, 4096, 4096, 1024});
    MIRAGE_CHECK(!frame(invalid, 1, {a}, true).ok);
    MIRAGE_CHECK(invalid.snapshot().error.code == "scene_uninitialized");
    VisualScene seed("source-session");
    MIRAGE_CHECK(frame(seed, 1, {object()}, true).ok);
    const auto object_bytes = seed.retained_bytes();
    VisualScene tight("source-session", {1, object_bytes + 1, 4096, 1024});
    MIRAGE_CHECK(frame(tight, 1, {object()}, true).ok);
    const auto tight_revision = tight.revision();
    MIRAGE_CHECK(tight.annotate(binding()).error.code == "scene_budget_exceeded");
    MIRAGE_CHECK(!tight.find(1)->binding && tight.revision() == tight_revision);
    MIRAGE_CHECK(tight.retained_bytes() == object_bytes);
    VisualScene bad_source(std::string(129, 's'));
    MIRAGE_CHECK(!frame(bad_source, 1, {object()}, true).ok);
}

void all_candidate_kinds_and_soak() {
    VisualScene s("source-session");
    std::vector<VisualSceneObject> objects;
    for (unsigned i = 0; i < 4; ++i) {
        auto a = object(i + 1);
        a.source = static_cast<VisualRegionSource>(i);
        objects.push_back(a);
    }
    MIRAGE_CHECK(frame(s, 1, objects, true).ok);
    for (unsigned i = 1; i <= 4; ++i)
        MIRAGE_CHECK(s.annotate(binding(i)).ok);
    const auto bytes = s.retained_bytes(), revision = s.revision();
    for (std::uint64_t sequence = 2; sequence < 2002; ++sequence) {
        const auto delta = frame(s, sequence, objects, true);
        MIRAGE_CHECK(delta.ok && delta.updated.empty() && delta.removed.empty());
        MIRAGE_CHECK(s.retained_bytes() == bytes && s.revision() == revision);
    }
    // A changed final object produces one record, not a full scene copy.
    objects.back().bounds.y += 5;
    const auto delta = frame(s, 2002, objects, true);
    MIRAGE_CHECK(delta.ok && delta.updated.size() == 1);
    MIRAGE_CHECK(s.retained_bytes() == bytes);
}
} // namespace

int main() {
    incremental_retention_and_movement();
    invalidate_suspend_and_recover();
    removal_replacement_and_scope();
    rejection_is_atomic_and_input_bounded();
    all_candidate_kinds_and_soak();
    return mirage::testing::finish("visual_scene");
}
