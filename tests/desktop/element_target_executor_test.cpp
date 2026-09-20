// M3-03 ElementTargetExecutor tests (DEC-005 resolution order, DEC-016
// decision 6): the ring order reference -> semantic/structural -> visual
// reference -> visual hint (ocr then template) -> spatial -> raw, each ring
// seeing only its own (masked) hints; traceable degraded fall-through on
// unresolved hints vs. decisive failures that never fall through; the
// pointer click sequence move -> press -> release with cooperative
// cancellation (including the best-effort release when the release is
// cancelled); and the fail-closed behavior when a dependency is not bound.

#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/element_target_executor.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/desktop/visual_snapshot.hpp>

#include <string>
#include <vector>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::ElementTarget;
using mirage::desktop::ElementTargetExecutor;
using mirage::desktop::InputLimits;
using mirage::desktop::InputOutcome;
using mirage::desktop::InputProvider;
using mirage::desktop::KeySym;
using mirage::desktop::MouseButton;
using mirage::desktop::PointerQueryOutcome;
using mirage::desktop::ResolutionRing;
using mirage::desktop::SemanticSnapshot;
using mirage::desktop::TargetResolution;
using mirage::desktop::VisualPublishOutcome;
using mirage::desktop::VisualReferenceRegistry;
using mirage::desktop::VisualRegionEntry;
using mirage::desktop::VisualRegionSource;
using mirage::desktop::VisualSnapshot;

/// The active generation used by most scenarios. Center coordinates the
/// assertions rely on: "@v1" (60,45), "@v2" (220,40), "@v3" (310,305),
/// "@v4" (1003,2002) — the last one has odd extents so the integer center
/// math stays honest.
VisualSnapshot four_region_snapshot() {
    VisualRegionEntry ocr;
    ocr.source = VisualRegionSource::kOcr;
    ocr.bounds = {10, 20, 100, 50};
    ocr.text = "alpha-text";
    ocr.confidence = 0.91;
    VisualRegionEntry tpl;
    tpl.source = VisualRegionSource::kTemplate;
    tpl.bounds = {200, 30, 40, 20};
    tpl.template_id = "tpl-1";
    tpl.confidence = 0.8;
    VisualRegionEntry detector;
    detector.source = VisualRegionSource::kDetector;
    detector.bounds = {300, 300, 21, 10};
    detector.text = "submit";
    detector.confidence = 0.7;
    VisualRegionEntry geometry;
    geometry.source = VisualRegionSource::kGeometry;
    geometry.bounds = {1000, 2000, 7, 5};
    VisualSnapshot snapshot;
    snapshot.regions.push_back(ocr);
    snapshot.regions.push_back(tpl);
    snapshot.regions.push_back(detector);
    snapshot.regions.push_back(geometry);
    return snapshot;
}

VisualPublishOutcome publish_active(VisualReferenceRegistry &registry) {
    const VisualPublishOutcome published = registry.publish(four_region_snapshot());
    MIRAGE_CHECK(published.ok);
    return published;
}

/// A pointer-only input provider that logs every landed action, so scenarios
/// assert on the exact injection sequence.
class RecordingInput final : public InputProvider {
  public:
    std::vector<std::string> log;

    InputOutcome inject_key(const KeySym &, bool, const InputLimits &,
                            const CancelToken &) override {
        return {false, false, {"invalid_argument", "not scripted"}};
    }
    InputOutcome type_text(const std::string &, const InputLimits &, const CancelToken &) override {
        return {false, false, {"invalid_argument", "not scripted"}};
    }
    PointerQueryOutcome pointer_position(const CancelToken &) override { return {}; }
    InputOutcome pointer_move(std::int32_t x, std::int32_t y, const InputLimits &,
                              const CancelToken &) override {
        log.push_back("move " + std::to_string(x) + "," + std::to_string(y));
        return {true, false, {}};
    }
    InputOutcome pointer_button(const MouseButton &button, bool pressed, const InputLimits &,
                                const CancelToken &) override {
        log.push_back(std::string(pressed ? "press " : "release ") + button);
        return {true, false, {}};
    }
};

/// Pointer provider whose move() requests cancellation on the shared token
/// after landing: the press must then observe the cancelled token and refuse.
class CancelOnMoveInput final : public InputProvider {
  public:
    explicit CancelOnMoveInput(CancelToken &hook) : hook_(hook) {}
    std::vector<std::string> log;

    InputOutcome inject_key(const KeySym &, bool, const InputLimits &,
                            const CancelToken &) override {
        return {false, false, {"invalid_argument", "not scripted"}};
    }
    InputOutcome type_text(const std::string &, const InputLimits &, const CancelToken &) override {
        return {false, false, {"invalid_argument", "not scripted"}};
    }
    PointerQueryOutcome pointer_position(const CancelToken &) override { return {}; }
    InputOutcome pointer_move(std::int32_t x, std::int32_t y, const InputLimits &,
                              const CancelToken &) override {
        log.push_back("move " + std::to_string(x) + "," + std::to_string(y));
        hook_.request_cancel();
        return {true, false, {}};
    }
    InputOutcome pointer_button(const MouseButton &, bool pressed, const InputLimits &,
                                const CancelToken &cancel) override {
        if (cancel.cancelled()) {
            return {false, true, {"cancelled", "injection cancelled"}};
        }
        log.push_back(std::string(pressed ? "press " : "release ") + "left");
        return {true, false, {}};
    }

  private:
    CancelToken &hook_;
};

/// Pointer provider that lets move and press land and cancels on the FIRST
/// release (both the outcome flag and the shared token, like a real backend
/// observing a stop request). The executor must retry the release with a
/// fresh token — the retry only lands if it is truly fresh, because this
/// provider refuses any pointer_button call that arrives on a cancelled
/// token. Observable as a second release call.
class CancelOnReleaseInput final : public InputProvider {
  public:
    explicit CancelOnReleaseInput(CancelToken &hook) : hook_(hook) {}
    std::vector<std::string> log;
    int release_calls = 0;

    InputOutcome inject_key(const KeySym &, bool, const InputLimits &,
                            const CancelToken &) override {
        return {false, false, {"invalid_argument", "not scripted"}};
    }
    InputOutcome type_text(const std::string &, const InputLimits &, const CancelToken &) override {
        return {false, false, {"invalid_argument", "not scripted"}};
    }
    PointerQueryOutcome pointer_position(const CancelToken &) override { return {}; }
    InputOutcome pointer_move(std::int32_t x, std::int32_t y, const InputLimits &,
                              const CancelToken &) override {
        log.push_back("move " + std::to_string(x) + "," + std::to_string(y));
        return {true, false, {}};
    }
    InputOutcome pointer_button(const MouseButton &, bool pressed, const InputLimits &,
                                const CancelToken &cancel) override {
        if (cancel.cancelled()) {
            return {false, true, {"cancelled", "injection cancelled"}};
        }
        if (!pressed) {
            ++release_calls;
            if (release_calls == 1) {
                hook_.request_cancel();
                return {false, true, {"cancelled", "injection cancelled"}};
            }
        }
        log.push_back(std::string(pressed ? "press " : "release ") + "left");
        return {true, false, {}};
    }

  private:
    CancelToken &hook_;
};

/// Focused window "w1" whose "@e1" button is live in the reference registry;
/// callers flip `env.actionable_refs` to decide whether the semantic
/// activation succeeds or reports unsupported_element.
void seed_accessibility_window(mirage::testing::FakeDesktopEnvironment &env) {
    mirage::desktop::WindowInfo window;
    window.id = "w1";
    window.title = "Editor";
    window.focused = true;
    env.windows.push_back(window);

    SemanticSnapshot snapshot;
    snapshot.application = "FakeEditor";
    mirage::desktop::SemanticNode run;
    run.ref = "@e1";
    run.role = "button";
    run.name = "Run";
    run.geometry = {0, 0, 100, 200};
    snapshot.nodes.push_back(run);
    env.snapshots["w1"] = snapshot;

    // Taking the snapshot populates the provider's reference registry; any
    // other handle ("@e99") is stale and reports not_found.
    const auto taken = env.accessibility()->semantic_snapshot("w1");
    MIRAGE_CHECK(taken.ok);
    env.actionable_refs.push_back("@e1");
}

/// A semantic context (the snapshot a target was formed against) carrying
/// one node with bounds {0,0,100,200}.
SemanticSnapshot context_with_anchor() {
    SemanticSnapshot context;
    mirage::desktop::SemanticNode node;
    node.ref = "@e1";
    node.role = "button";
    node.name = "Run";
    node.geometry = {0, 0, 100, 200};
    context.nodes.push_back(node);
    return context;
}

ElementTarget by_reference(const std::string &id) {
    ElementTarget target;
    target.reference.id = id;
    return target;
}

// ---- visual reference ring ----------------------------------------------------

void visual_reference_click_executes_the_pointer_sequence() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    const VisualPublishOutcome published = publish_active(registry);
    MIRAGE_CHECK(published.snapshot.scope_ref == "@vs1");

    ElementTargetExecutor executor(env.accessibility(), &registry, &input);
    const TargetResolution outcome = executor.click(by_reference("@v2"), nullptr);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.ring == ResolutionRing::kVisualReference);
    MIRAGE_CHECK(outcome.point.x == 220);
    MIRAGE_CHECK(outcome.point.y == 40);
    MIRAGE_CHECK(outcome.error.code.empty());
    MIRAGE_CHECK(outcome.steps.size() == 1);
    if (outcome.steps.size() == 1) {
        MIRAGE_CHECK(outcome.steps[0].ring == ResolutionRing::kVisualReference);
        MIRAGE_CHECK(outcome.steps[0].resolved);
    }
    MIRAGE_CHECK(input.log.size() == 3);
    if (input.log.size() == 3) {
        MIRAGE_CHECK(input.log[0] == "move 220,40");
        MIRAGE_CHECK(input.log[1] == "press left");
        MIRAGE_CHECK(input.log[2] == "release left");
    }
}

void visual_reference_center_handles_odd_extents() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(nullptr, &registry, &input);
    const TargetResolution outcome = executor.click(by_reference("@v4"), nullptr);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.ring == ResolutionRing::kVisualReference);
    MIRAGE_CHECK(outcome.point.x == 1003); // 1000 + 7/2, int64 intermediate
    MIRAGE_CHECK(outcome.point.y == 2002); // 2000 + 5/2
    MIRAGE_CHECK(input.log.size() == 3);
    if (input.log.size() == 3) {
        MIRAGE_CHECK(input.log[0] == "move 1003,2002");
    }
}

void stale_visual_reference_reports_not_found_without_injection() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    // A replacement generation with fewer regions: "@v2" stops resolving.
    VisualSnapshot replacement;
    VisualRegionEntry solo;
    solo.source = VisualRegionSource::kOcr;
    solo.bounds = {0, 0, 10, 10};
    solo.text = "fresh";
    replacement.regions.push_back(solo);
    MIRAGE_CHECK(registry.publish(replacement).ok);

    ElementTargetExecutor executor(nullptr, &registry, &input);
    const TargetResolution outcome = executor.click(by_reference("@v2"), nullptr);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "not_found");
    MIRAGE_CHECK(outcome.steps.size() == 1);
    if (outcome.steps.size() == 1) {
        MIRAGE_CHECK(outcome.steps[0].ring == ResolutionRing::kVisualReference);
        MIRAGE_CHECK(!outcome.steps[0].resolved);
        MIRAGE_CHECK(outcome.steps[0].error.code == "not_found");
    }
    MIRAGE_CHECK(input.log.empty());
}

// ---- visual hint ring ---------------------------------------------------------

void ocr_hint_matches_kocr_text_exactly() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(nullptr, &registry, &input);
    ElementTarget target;
    target.visual.ocr_text = "alpha-text";
    const TargetResolution hit = executor.click(target, nullptr);

    MIRAGE_CHECK(hit.ok);
    MIRAGE_CHECK(hit.ring == ResolutionRing::kVisualHint);
    MIRAGE_CHECK(hit.point.x == 60);
    MIRAGE_CHECK(hit.point.y == 45);
    MIRAGE_CHECK(input.log.size() == 3);
    if (input.log.size() == 3) {
        MIRAGE_CHECK(input.log[0] == "move 60,45");
    }

    // Matching is exact: case or substring differences do not resolve.
    ElementTarget miss_target;
    miss_target.visual.ocr_text = "ALPHA-TEXT";
    const TargetResolution miss = executor.click(miss_target, nullptr);
    MIRAGE_CHECK(!miss.ok);
    MIRAGE_CHECK(miss.error.code == "not_found");
    MIRAGE_CHECK(miss.steps.size() == 1);
    if (miss.steps.size() == 1) {
        MIRAGE_CHECK(miss.steps[0].ring == ResolutionRing::kVisualHint);
        MIRAGE_CHECK(!miss.steps[0].resolved);
    }
    MIRAGE_CHECK(input.log.size() == 3); // no injection for the miss
}

void ocr_hint_never_matches_non_ocr_regions() {
    // "submit" is the text of a kDetector region; the visual hint ring only
    // offers ocr_text to kOcr regions — cross-source matches would silently
    // click a different provenance than the agent referenced.
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(nullptr, &registry, &input);
    ElementTarget target;
    target.visual.ocr_text = "submit";
    const TargetResolution outcome = executor.click(target, nullptr);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.error.code == "not_found");
    MIRAGE_CHECK(input.log.empty());
}

void template_hint_matches_and_ocr_takes_precedence() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(nullptr, &registry, &input);

    ElementTarget template_target;
    template_target.visual.template_id = "tpl-1";
    const TargetResolution by_template = executor.click(template_target, nullptr);
    MIRAGE_CHECK(by_template.ok);
    MIRAGE_CHECK(by_template.ring == ResolutionRing::kVisualHint);
    MIRAGE_CHECK(by_template.point.x == 220);
    MIRAGE_CHECK(by_template.point.y == 40);

    // Both hints set and both would match: ocr_text is tried first.
    ElementTarget both_target;
    both_target.visual.ocr_text = "alpha-text";
    both_target.visual.template_id = "tpl-1";
    const TargetResolution both = executor.click(both_target, nullptr);
    MIRAGE_CHECK(both.ok);
    MIRAGE_CHECK(both.ring == ResolutionRing::kVisualHint);
    MIRAGE_CHECK(both.point.x == 60);
    MIRAGE_CHECK(both.point.y == 45);

    // A missing ocr_text falls through to the template hint within the ring.
    ElementTarget fallback_target;
    fallback_target.visual.ocr_text = "no-such-text";
    fallback_target.visual.template_id = "tpl-1";
    const TargetResolution fallback = executor.click(fallback_target, nullptr);
    MIRAGE_CHECK(fallback.ok);
    MIRAGE_CHECK(fallback.ring == ResolutionRing::kVisualHint);
    MIRAGE_CHECK(fallback.point.x == 220);
    MIRAGE_CHECK(fallback.point.y == 40);

    // An unknown template id alone does not resolve.
    ElementTarget unknown_template;
    unknown_template.visual.template_id = "no-such-tpl";
    const TargetResolution unknown = executor.click(unknown_template, nullptr);
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "not_found");
}

// ---- accessibility rings ------------------------------------------------------

void accessibility_reference_activation_avoids_pointer_injection() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_accessibility_window(env);
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(env.accessibility(), &registry, &input);
    const TargetResolution outcome = executor.click(by_reference("@e1"), nullptr);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.ring == ResolutionRing::kAccessibilityReference);
    MIRAGE_CHECK(outcome.error.code.empty());
    MIRAGE_CHECK(outcome.steps.size() == 1);
    if (outcome.steps.size() == 1) {
        MIRAGE_CHECK(outcome.steps[0].ring == ResolutionRing::kAccessibilityReference);
        MIRAGE_CHECK(outcome.steps[0].resolved);
    }
    // The semantic activation happened; no pointer action was injected.
    MIRAGE_CHECK(env.activated_refs.size() == 1);
    if (!env.activated_refs.empty()) {
        MIRAGE_CHECK(env.activated_refs.back() == "@e1");
    }
    MIRAGE_CHECK(input.log.empty());
    MIRAGE_CHECK(outcome.point.x == 0);
    MIRAGE_CHECK(outcome.point.y == 0);
}

void semantic_hint_resolves_through_accessibility() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_accessibility_window(env);
    RecordingInput input;

    ElementTargetExecutor executor(env.accessibility(), nullptr, &input);
    ElementTarget target;
    target.semantic.role = "button";
    target.semantic.name = "Run";
    const TargetResolution outcome = executor.click(target, nullptr);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.ring == ResolutionRing::kAccessibilityHint);
    MIRAGE_CHECK(outcome.steps.size() == 1);
    if (outcome.steps.size() == 1) {
        MIRAGE_CHECK(outcome.steps[0].ring == ResolutionRing::kAccessibilityHint);
        MIRAGE_CHECK(outcome.steps[0].resolved);
    }
    MIRAGE_CHECK(env.activated_refs.size() == 1);
    MIRAGE_CHECK(input.log.empty());
}

void degraded_accessibility_reference_falls_through_to_visual_hint() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_accessibility_window(env);
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(env.accessibility(), &registry, &input);
    ElementTarget target = by_reference("@e99"); // stale accessibility handle
    target.visual.ocr_text = "alpha-text";
    const TargetResolution outcome = executor.click(target, nullptr);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.ring == ResolutionRing::kVisualHint);
    MIRAGE_CHECK(outcome.point.x == 60);
    MIRAGE_CHECK(outcome.point.y == 45);
    // The degraded attempt is traceable: accessibility.reference failed with
    // not_found, then the visual hint ring decided.
    MIRAGE_CHECK(outcome.steps.size() == 2);
    if (outcome.steps.size() == 2) {
        MIRAGE_CHECK(std::string(resolution_ring_name(outcome.steps[0].ring)) ==
                     "accessibility.reference");
        MIRAGE_CHECK(!outcome.steps[0].resolved);
        MIRAGE_CHECK(outcome.steps[0].error.code == "not_found");
        MIRAGE_CHECK(std::string(resolution_ring_name(outcome.steps[1].ring)) == "visual.hint");
        MIRAGE_CHECK(outcome.steps[1].resolved);
    }
    MIRAGE_CHECK(input.log.size() == 3);
    if (input.log.size() == 3) {
        MIRAGE_CHECK(input.log[0] == "move 60,45");
        MIRAGE_CHECK(input.log[2] == "release left");
    }
}

void accessibility_action_failure_does_not_fall_through() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_accessibility_window(env);
    env.actionable_refs.clear(); // "@e1" resolves but exposes no action
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(env.accessibility(), &registry, &input);
    // The visual hint WOULD resolve: a fall-through here would silently act
    // somewhere else, so the action failure must decide instead.
    ElementTarget target = by_reference("@e1");
    target.visual.ocr_text = "alpha-text";
    const TargetResolution outcome = executor.click(target, nullptr);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "unsupported_element");
    MIRAGE_CHECK(outcome.steps.size() == 1);
    if (outcome.steps.size() == 1) {
        MIRAGE_CHECK(outcome.steps[0].ring == ResolutionRing::kAccessibilityReference);
        MIRAGE_CHECK(!outcome.steps[0].resolved);
        MIRAGE_CHECK(outcome.steps[0].error.code == "unsupported_element");
    }
    MIRAGE_CHECK(env.activated_refs.empty());
    MIRAGE_CHECK(input.log.empty());
}

// ---- spatial ring -------------------------------------------------------------

void spatial_with_visual_anchor_applies_the_offset() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(nullptr, &registry, &input);
    ElementTarget target;
    target.spatial.relative_to.id = "@v1";
    target.spatial.dx = 5;
    target.spatial.dy = -3;
    const TargetResolution outcome = executor.click(target, nullptr);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.ring == ResolutionRing::kSpatial);
    MIRAGE_CHECK(outcome.point.x == 15); // 10 + 5
    MIRAGE_CHECK(outcome.point.y == 17); // 20 - 3
    MIRAGE_CHECK(input.log.size() == 3);
    if (input.log.size() == 3) {
        MIRAGE_CHECK(input.log[0] == "move 15,17");
    }
}

void spatial_with_semantic_context_anchor() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    const SemanticSnapshot context = context_with_anchor();

    ElementTargetExecutor executor(env.accessibility(), nullptr, &input);
    ElementTarget target;
    target.spatial.relative_to.id = "@e1";
    target.spatial.dx = 10;
    target.spatial.dy = 20;
    const TargetResolution outcome = executor.click(target, &context);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.ring == ResolutionRing::kSpatial);
    MIRAGE_CHECK(outcome.point.x == 10);
    MIRAGE_CHECK(outcome.point.y == 20);
}

void spatial_degrades_when_the_anchor_does_not_resolve() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);
    const SemanticSnapshot context = context_with_anchor();

    ElementTargetExecutor executor(env.accessibility(), &registry, &input);

    // A semantic anchor without a context has no geometry source.
    ElementTarget without_context;
    without_context.spatial.relative_to.id = "@e1";
    const TargetResolution no_context = executor.click(without_context, nullptr);
    MIRAGE_CHECK(!no_context.ok);
    MIRAGE_CHECK(no_context.error.code == "not_found");
    MIRAGE_CHECK(no_context.steps.size() == 1);
    if (no_context.steps.size() == 1) {
        MIRAGE_CHECK(no_context.steps[0].ring == ResolutionRing::kSpatial);
        MIRAGE_CHECK(!no_context.steps[0].resolved);
        MIRAGE_CHECK(no_context.steps[0].error.code == "not_found");
    }

    // An anchor missing from the provided context degrades too.
    ElementTarget missing;
    missing.spatial.relative_to.id = "@e404";
    const TargetResolution missing_outcome = executor.click(missing, &context);
    MIRAGE_CHECK(!missing_outcome.ok);
    MIRAGE_CHECK(missing_outcome.error.code == "not_found");

    // A stale visual anchor degrades instead of clicking anything.
    ElementTarget stale_visual;
    stale_visual.spatial.relative_to.id = "@v9";
    const TargetResolution stale = executor.click(stale_visual, nullptr);
    MIRAGE_CHECK(!stale.ok);
    MIRAGE_CHECK(stale.error.code == "not_found");
    MIRAGE_CHECK(input.log.empty());
}

// ---- raw ring -----------------------------------------------------------------

void raw_point_click_acts_at_the_declared_point() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(env.accessibility(), &registry, &input);
    ElementTarget target;
    target.raw.x = 5;
    target.raw.y = 9;
    const TargetResolution outcome = executor.click(target, nullptr);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.ring == ResolutionRing::kRaw);
    MIRAGE_CHECK(outcome.point.x == 5);
    MIRAGE_CHECK(outcome.point.y == 9);
    MIRAGE_CHECK(outcome.steps.size() == 1);
    if (outcome.steps.size() == 1) {
        MIRAGE_CHECK(std::string(resolution_ring_name(outcome.steps[0].ring)) == "raw");
        MIRAGE_CHECK(outcome.steps[0].resolved);
    }
    MIRAGE_CHECK(input.log.size() == 3);
    if (input.log.size() == 3) {
        MIRAGE_CHECK(input.log[0] == "move 5,9");
        MIRAGE_CHECK(input.log[1] == "press left");
        MIRAGE_CHECK(input.log[2] == "release left");
    }
}

// ---- rejection and cancellation ------------------------------------------------

void target_without_hints_is_rejected_before_any_side_effect() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_accessibility_window(env);
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(env.accessibility(), &registry, &input);
    const TargetResolution outcome = executor.click(ElementTarget{}, nullptr);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "invalid_argument");
    MIRAGE_CHECK(outcome.steps.empty());
    MIRAGE_CHECK(env.activated_refs.empty());
    MIRAGE_CHECK(input.log.empty());
}

void pre_cancelled_click_reports_cancelled_without_side_effects() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_accessibility_window(env);
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    CancelToken cancel;
    cancel.request_cancel();
    ElementTargetExecutor executor(env.accessibility(), &registry, &input);
    const TargetResolution outcome = executor.click(by_reference("@v1"), nullptr, {}, cancel);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
    MIRAGE_CHECK(outcome.steps.empty());
    MIRAGE_CHECK(env.activated_refs.empty());
    MIRAGE_CHECK(input.log.empty());
}

void cancellation_after_move_reports_cancelled_without_press() {
    mirage::testing::FakeDesktopEnvironment env;
    VisualReferenceRegistry registry;
    publish_active(registry);
    CancelToken cancel;

    CancelOnMoveInput input(cancel);
    ElementTargetExecutor executor(nullptr, &registry, &input);
    const TargetResolution outcome = executor.click(by_reference("@v1"), nullptr, {}, cancel);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
    // The move landed, the press did not: the button was never held.
    MIRAGE_CHECK(input.log.size() == 1);
    if (input.log.size() == 1) {
        MIRAGE_CHECK(input.log[0] == "move 60,45");
    }
}

void cancelled_release_converges_with_a_best_effort_release() {
    mirage::testing::FakeDesktopEnvironment env;
    VisualReferenceRegistry registry;
    publish_active(registry);
    CancelToken cancel;

    CancelOnReleaseInput input(cancel);
    ElementTargetExecutor executor(nullptr, &registry, &input);
    const TargetResolution outcome = executor.click(by_reference("@v1"), nullptr, {}, cancel);

    // The cancellation during release is reported, but only after a fresh
    // token retried the release: a takeover must not leave the button held.
    // The retry only lands because it does NOT carry the cancelled token —
    // this provider refuses any call that arrives on one.
    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
    MIRAGE_CHECK(input.release_calls == 2);
    MIRAGE_CHECK(input.log.size() == 3);
    if (input.log.size() == 3) {
        MIRAGE_CHECK(input.log[0] == "move 60,45");
        MIRAGE_CHECK(input.log[1] == "press left");
        MIRAGE_CHECK(input.log[2] == "release left");
    }
}

// ---- unbound dependencies ------------------------------------------------------

void missing_accessibility_provider_degrades_to_the_visual_rings() {
    mirage::testing::FakeDesktopEnvironment env;
    RecordingInput input;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(nullptr, &registry, &input);

    // A reference target with a visual fallback: the accessibility ring is
    // a traceable unsupported_platform step, then the visual hint decides.
    ElementTarget target = by_reference("@e1");
    target.visual.ocr_text = "alpha-text";
    const TargetResolution degraded = executor.click(target, nullptr);
    MIRAGE_CHECK(degraded.ok);
    MIRAGE_CHECK(degraded.ring == ResolutionRing::kVisualHint);
    MIRAGE_CHECK(degraded.steps.size() == 2);
    if (degraded.steps.size() == 2) {
        MIRAGE_CHECK(degraded.steps[0].ring == ResolutionRing::kAccessibilityReference);
        MIRAGE_CHECK(!degraded.steps[0].resolved);
        MIRAGE_CHECK(degraded.steps[0].error.code == "unsupported_platform");
        MIRAGE_CHECK(degraded.steps[1].resolved);
    }
    MIRAGE_CHECK(input.log.size() == 3);

    // Without any visual fallback the same target resolves nowhere.
    const TargetResolution nowhere = executor.click(by_reference("@e1"), nullptr);
    MIRAGE_CHECK(!nowhere.ok);
    MIRAGE_CHECK(nowhere.error.code == "not_found");
    MIRAGE_CHECK(nowhere.steps.size() == 1);
    if (nowhere.steps.size() == 1) {
        MIRAGE_CHECK(nowhere.steps[0].error.code == "unsupported_platform");
    }
    MIRAGE_CHECK(input.log.size() == 3); // no injection for the nowhere case
}

void missing_visual_registry_degrades_the_visual_rings() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_accessibility_window(env);
    RecordingInput input;

    ElementTargetExecutor executor(env.accessibility(), nullptr, &input);

    const TargetResolution reference = executor.click(by_reference("@v1"), nullptr);
    MIRAGE_CHECK(!reference.ok);
    MIRAGE_CHECK(reference.error.code == "not_found");
    MIRAGE_CHECK(reference.steps.size() == 1);
    if (reference.steps.size() == 1) {
        MIRAGE_CHECK(reference.steps[0].ring == ResolutionRing::kVisualReference);
        MIRAGE_CHECK(!reference.steps[0].resolved);
        MIRAGE_CHECK(reference.steps[0].error.code == "unsupported_platform");
    }

    ElementTarget hint;
    hint.visual.ocr_text = "alpha-text";
    const TargetResolution visual_hint = executor.click(hint, nullptr);
    MIRAGE_CHECK(!visual_hint.ok);
    MIRAGE_CHECK(visual_hint.error.code == "not_found");
    MIRAGE_CHECK(visual_hint.steps.size() == 1);
    if (visual_hint.steps.size() == 1) {
        MIRAGE_CHECK(std::string(resolution_ring_name(visual_hint.steps[0].ring)) == "visual.hint");
        MIRAGE_CHECK(visual_hint.steps[0].error.code == "unsupported_platform");
    }
    MIRAGE_CHECK(input.log.empty());

    // Accessibility targets still resolve without the visual registry.
    const TargetResolution semantic = executor.click(by_reference("@e1"), nullptr);
    MIRAGE_CHECK(semantic.ok);
    MIRAGE_CHECK(semantic.ring == ResolutionRing::kAccessibilityReference);
    MIRAGE_CHECK(input.log.empty());
}

void missing_input_provider_resolves_but_blocks_the_injection() {
    mirage::testing::FakeDesktopEnvironment env;
    VisualReferenceRegistry registry;
    publish_active(registry);

    ElementTargetExecutor executor(nullptr, &registry, nullptr);
    const TargetResolution outcome = executor.click(by_reference("@v1"), nullptr);

    // The resolution decided (the step is resolved) but no input provider is
    // bound: the action fails closed instead of being silently dropped.
    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "unsupported_platform");
    MIRAGE_CHECK(outcome.steps.size() == 1);
    if (outcome.steps.size() == 1) {
        MIRAGE_CHECK(outcome.steps[0].ring == ResolutionRing::kVisualReference);
        MIRAGE_CHECK(outcome.steps[0].resolved);
    }
}

// ---- naming and classification --------------------------------------------------

void ring_names_and_visual_reference_predicate_are_stable() {
    MIRAGE_CHECK(std::string(resolution_ring_name(ResolutionRing::kAccessibilityReference)) ==
                 "accessibility.reference");
    MIRAGE_CHECK(std::string(resolution_ring_name(ResolutionRing::kAccessibilityHint)) ==
                 "accessibility.hint");
    MIRAGE_CHECK(std::string(resolution_ring_name(ResolutionRing::kVisualReference)) ==
                 "visual.reference");
    MIRAGE_CHECK(std::string(resolution_ring_name(ResolutionRing::kVisualHint)) == "visual.hint");
    MIRAGE_CHECK(std::string(resolution_ring_name(ResolutionRing::kSpatial)) == "spatial");
    MIRAGE_CHECK(std::string(resolution_ring_name(ResolutionRing::kRaw)) == "raw");

    MIRAGE_CHECK(mirage::desktop::is_visual_reference("@v1"));
    MIRAGE_CHECK(mirage::desktop::is_visual_reference("@v12"));
    MIRAGE_CHECK(mirage::desktop::is_visual_reference("@vs1")); // scope shape shares the prefix
    MIRAGE_CHECK(!mirage::desktop::is_visual_reference("@e1"));
    MIRAGE_CHECK(!mirage::desktop::is_visual_reference(""));
    MIRAGE_CHECK(!mirage::desktop::is_visual_reference("v1"));
    MIRAGE_CHECK(!mirage::desktop::is_visual_reference("@s1"));
}

} // namespace

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[element_target_executor_test] scenario: %s\n", name);
    scenario();
}

int main() {
    run_scenario("visual_reference_click_executes_the_pointer_sequence",
                 visual_reference_click_executes_the_pointer_sequence);
    run_scenario("visual_reference_center_handles_odd_extents",
                 visual_reference_center_handles_odd_extents);
    run_scenario("stale_visual_reference_reports_not_found_without_injection",
                 stale_visual_reference_reports_not_found_without_injection);
    run_scenario("ocr_hint_matches_kocr_text_exactly", ocr_hint_matches_kocr_text_exactly);
    run_scenario("ocr_hint_never_matches_non_ocr_regions", ocr_hint_never_matches_non_ocr_regions);
    run_scenario("template_hint_matches_and_ocr_takes_precedence",
                 template_hint_matches_and_ocr_takes_precedence);
    run_scenario("accessibility_reference_activation_avoids_pointer_injection",
                 accessibility_reference_activation_avoids_pointer_injection);
    run_scenario("semantic_hint_resolves_through_accessibility",
                 semantic_hint_resolves_through_accessibility);
    run_scenario("degraded_accessibility_reference_falls_through_to_visual_hint",
                 degraded_accessibility_reference_falls_through_to_visual_hint);
    run_scenario("accessibility_action_failure_does_not_fall_through",
                 accessibility_action_failure_does_not_fall_through);
    run_scenario("spatial_with_visual_anchor_applies_the_offset",
                 spatial_with_visual_anchor_applies_the_offset);
    run_scenario("spatial_with_semantic_context_anchor", spatial_with_semantic_context_anchor);
    run_scenario("spatial_degrades_when_the_anchor_does_not_resolve",
                 spatial_degrades_when_the_anchor_does_not_resolve);
    run_scenario("raw_point_click_acts_at_the_declared_point",
                 raw_point_click_acts_at_the_declared_point);
    run_scenario("target_without_hints_is_rejected_before_any_side_effect",
                 target_without_hints_is_rejected_before_any_side_effect);
    run_scenario("pre_cancelled_click_reports_cancelled_without_side_effects",
                 pre_cancelled_click_reports_cancelled_without_side_effects);
    run_scenario("cancellation_after_move_reports_cancelled_without_press",
                 cancellation_after_move_reports_cancelled_without_press);
    run_scenario("cancelled_release_converges_with_a_best_effort_release",
                 cancelled_release_converges_with_a_best_effort_release);
    run_scenario("missing_accessibility_provider_degrades_to_the_visual_rings",
                 missing_accessibility_provider_degrades_to_the_visual_rings);
    run_scenario("missing_visual_registry_degrades_the_visual_rings",
                 missing_visual_registry_degrades_the_visual_rings);
    run_scenario("missing_input_provider_resolves_but_blocks_the_injection",
                 missing_input_provider_resolves_but_blocks_the_injection);
    run_scenario("ring_names_and_visual_reference_predicate_are_stable",
                 ring_names_and_visual_reference_predicate_are_stable);
    return mirage::testing::finish("element_target_executor_test");
}
