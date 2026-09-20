// M2-06 observation assembler tests: the on-demand DesktopObservation
// assembly contract (design doc section 6). Requested components are
// mandatory — an environment that cannot deliver one reports its stable
// error and the whole assembly reports ok=false, while every component that
// did capture stays in the observation (incompleteness is explicit, never
// silent). The fake environment drives the positive and injected-failure
// paths; a partial-provider stub environment drives the null-accessor
// fail-closed paths.

#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/observation_assembler.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>

#include <string>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::ObservationAssembler;
using mirage::desktop::ObservationAssemblyLimits;
using mirage::desktop::ObservationComponents;
using mirage::desktop::SemanticNode;

/// A bare environment whose provider accessors return null (inherited base
/// behavior) unless a provider from the full fake is wired in explicitly.
/// This isolates single components against a missing-provider environment
/// without touching the production backends.
class PartialEnvironment final : public mirage::desktop::DesktopEnvironment {
  public:
    mirage::desktop::EnvironmentInfo info() const override { return {"partial", "stub"}; }

    mirage::desktop::WindowProvider *window() override { return window_provider_; }
    mirage::desktop::AccessibilityProvider *accessibility() override {
        return accessibility_provider_;
    }
    mirage::desktop::InputProvider *input() override { return input_provider_; }

    mirage::desktop::WindowProvider *window_provider_ = nullptr;
    mirage::desktop::AccessibilityProvider *accessibility_provider_ = nullptr;
    mirage::desktop::InputProvider *input_provider_ = nullptr;
};

SemanticNode node(std::string ref, std::string role, std::string name, std::size_t parent) {
    SemanticNode n;
    n.ref = std::move(ref);
    n.role = std::move(role);
    n.name = std::move(name);
    n.parent = parent;
    return n;
}

/// Focused window "w1" plus its two-node accessibility snapshot, shared by
/// the semantic-snapshot scenarios.
void seed_window_and_snapshot(mirage::testing::FakeDesktopEnvironment &env) {
    mirage::desktop::WindowInfo window;
    window.id = "w1";
    window.title = "Editor";
    window.geometry = {10, 20, 800, 600};
    window.focused = true;
    env.windows.push_back(window);

    mirage::desktop::SemanticSnapshot snapshot;
    snapshot.application = "FakeEditor";
    snapshot.window_title = "Editor";
    snapshot.nodes.push_back(node("@e1", "window", "Editor", mirage::desktop::kNoParent));
    auto caret = node("@e2", "editor", "main.cpp", 0);
    caret.focused = true;
    snapshot.nodes.push_back(caret);
    env.snapshots["w1"] = snapshot;
}

void empty_request_returns_a_clean_empty_observation() {
    mirage::testing::FakeDesktopEnvironment env;
    ObservationAssembler assembler(env);

    const ObservationComponents none;
    const auto outcome = assembler.assemble(none);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code.empty());
    MIRAGE_CHECK(outcome.active_window.requested == false);
    MIRAGE_CHECK(outcome.semantic_snapshot.requested == false);
    MIRAGE_CHECK(outcome.pointer_state.requested == false);
    MIRAGE_CHECK(outcome.environment_state.requested == false);
    MIRAGE_CHECK(!outcome.active_window.captured);
    MIRAGE_CHECK(!outcome.semantic_snapshot.captured);
    MIRAGE_CHECK(!outcome.pointer_state.captured);
    MIRAGE_CHECK(!outcome.environment_state.captured);
    MIRAGE_CHECK(outcome.observation.active_application.empty());
    MIRAGE_CHECK(outcome.observation.active_window.empty());
    MIRAGE_CHECK(outcome.observation.focused_element.empty());
    MIRAGE_CHECK(outcome.observation.environment_state.empty());
    MIRAGE_CHECK(outcome.observation.semantic_snapshot.nodes.empty());
    MIRAGE_CHECK(outcome.observation.pointer_state.x == 0);
    MIRAGE_CHECK(outcome.observation.pointer_state.y == 0);
    MIRAGE_CHECK(outcome.observation.window_focused == false);
}

void environment_state_lists_providers_in_fixed_order() {
    mirage::testing::FakeDesktopEnvironment full;
    ObservationAssembler full_assembler(full);

    ObservationComponents components;
    components.environment_state = true;
    const auto outcome = full_assembler.assemble(components);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.environment_state.captured);
    // The fake carries all nine providers; the summary names them in the
    // fixed accessor order, prefixed with the platform.
    MIRAGE_CHECK(outcome.observation.environment_state ==
                 "test:filesystem,process,application,window,accessibility,screen,input,"
                 "clipboard,notification");

    // A partial inventory names exactly the present providers, still in the
    // fixed order regardless of wiring order.
    PartialEnvironment partial;
    partial.input_provider_ = full.input();
    partial.window_provider_ = full.window();
    partial.accessibility_provider_ = full.accessibility();
    ObservationAssembler partial_assembler(partial);
    const auto partial_outcome = partial_assembler.assemble(components);
    MIRAGE_CHECK(partial_outcome.ok);
    MIRAGE_CHECK(partial_outcome.observation.environment_state ==
                 "stub:window,accessibility,input");

    // No providers at all: platform prefix only, still a valid summary.
    PartialEnvironment bare;
    ObservationAssembler bare_assembler(bare);
    const auto bare_outcome = bare_assembler.assemble(components);
    MIRAGE_CHECK(bare_outcome.ok);
    MIRAGE_CHECK(bare_outcome.observation.environment_state == "stub:");
}

void active_window_component_reports_the_focused_window() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_window_and_snapshot(env);
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.active_window = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.active_window.requested);
    MIRAGE_CHECK(outcome.active_window.captured);
    MIRAGE_CHECK(outcome.active_window.error.code.empty());
    MIRAGE_CHECK(outcome.observation.active_window == "Editor");
    MIRAGE_CHECK(outcome.observation.window_geometry.x == 10);
    MIRAGE_CHECK(outcome.observation.window_geometry.y == 20);
    MIRAGE_CHECK(outcome.observation.window_geometry.width == 800);
    MIRAGE_CHECK(outcome.observation.window_geometry.height == 600);
    MIRAGE_CHECK(outcome.observation.window_focused);
}

void active_window_fails_closed_without_a_focused_window() {
    mirage::testing::FakeDesktopEnvironment env; // windows table empty
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.active_window = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.active_window.requested);
    MIRAGE_CHECK(!outcome.active_window.captured);
    MIRAGE_CHECK(outcome.active_window.error.code == "not_found");
    MIRAGE_CHECK(outcome.error.code == "not_found");
    MIRAGE_CHECK(outcome.observation.active_window.empty());
}

void active_window_fails_closed_without_a_window_provider() {
    PartialEnvironment env; // window() null
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.active_window = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.active_window.requested);
    MIRAGE_CHECK(!outcome.active_window.captured);
    MIRAGE_CHECK(outcome.active_window.error.code == "unsupported_platform");
    MIRAGE_CHECK(outcome.error.code == "unsupported_platform");
}

void semantic_snapshot_fills_snapshot_focus_and_application() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_window_and_snapshot(env);
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.active_window = true;
    components.semantic_snapshot = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.semantic_snapshot.requested);
    MIRAGE_CHECK(outcome.semantic_snapshot.captured);
    MIRAGE_CHECK(outcome.observation.semantic_snapshot.nodes.size() == 2);
    MIRAGE_CHECK(outcome.observation.semantic_snapshot.application == "FakeEditor");
    // focused_element is the first focused node's ref; active_application is
    // the accessibility application root.
    MIRAGE_CHECK(outcome.observation.focused_element == "@e2");
    MIRAGE_CHECK(outcome.observation.active_application == "FakeEditor");
}

void semantic_snapshot_without_active_window_still_resolves_the_focus() {
    // Structure-only assembly: the snapshot needs the focused window id, so
    // the assembler resolves it internally while the active_window component
    // itself stays unrequested.
    mirage::testing::FakeDesktopEnvironment env;
    seed_window_and_snapshot(env);
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.semantic_snapshot = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.semantic_snapshot.captured);
    MIRAGE_CHECK(outcome.observation.semantic_snapshot.nodes.size() == 2);
    MIRAGE_CHECK(outcome.observation.focused_element == "@e2");
    MIRAGE_CHECK(outcome.observation.active_application == "FakeEditor");
    // The active_window component was not part of the request: it is not
    // captured and its observation fields stay empty.
    MIRAGE_CHECK(!outcome.active_window.requested);
    MIRAGE_CHECK(!outcome.active_window.captured);
    MIRAGE_CHECK(outcome.observation.active_window.empty());
    MIRAGE_CHECK(outcome.observation.window_focused == false);
}

void semantic_snapshot_reports_unsupported_window() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::WindowInfo window;
    window.id = "w1";
    window.title = "Terminal";
    window.focused = true;
    env.windows.push_back(window); // no snapshots entry for w1
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.active_window = true;
    components.semantic_snapshot = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.active_window.captured); // focus found fine
    MIRAGE_CHECK(!outcome.semantic_snapshot.captured);
    MIRAGE_CHECK(outcome.semantic_snapshot.error.code == "unsupported_window");
    MIRAGE_CHECK(outcome.error.code == "unsupported_window");
    MIRAGE_CHECK(outcome.observation.active_application.empty());
}

void semantic_snapshot_fails_closed_without_providers() {
    // No accessibility provider: unsupported_platform.
    PartialEnvironment no_a11y;
    ObservationComponents components;
    components.semantic_snapshot = true;
    const auto without_a11y = ObservationAssembler(no_a11y).assemble(components);
    MIRAGE_CHECK(!without_a11y.ok);
    MIRAGE_CHECK(without_a11y.semantic_snapshot.error.code == "unsupported_platform");
    MIRAGE_CHECK(without_a11y.error.code == "unsupported_platform");

    // Window provider missing too: the snapshot cannot even learn the
    // focused window id, and that is the same honest platform gap.
    PartialEnvironment no_window;
    no_window.accessibility_provider_ = nullptr;
    const auto without_window = ObservationAssembler(no_window).assemble(components);
    MIRAGE_CHECK(!without_window.ok);
    MIRAGE_CHECK(without_window.semantic_snapshot.error.code == "unsupported_platform");

    // With only the accessibility provider wired (no window provider), the
    // failure stays unsupported_platform, not a snapshot attempt.
    mirage::testing::FakeDesktopEnvironment full;
    PartialEnvironment a11y_only;
    a11y_only.accessibility_provider_ = full.accessibility();
    const auto outcome = ObservationAssembler(a11y_only).assemble(components);
    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.semantic_snapshot.error.code == "unsupported_platform");
}

void semantic_snapshot_failure_does_not_block_other_components() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_window_and_snapshot(env);
    env.failures.snapshot_error = true;
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.active_window = true;
    components.semantic_snapshot = true;
    components.pointer_state = true;
    components.environment_state = true;
    const auto outcome = assembler.assemble(components);

    // The failed component is reported...
    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(!outcome.semantic_snapshot.captured);
    MIRAGE_CHECK(outcome.semantic_snapshot.error.code == "io_error");
    MIRAGE_CHECK(outcome.error.code == "io_error"); // first failure in capture order
    // ...and every later component still captured.
    MIRAGE_CHECK(outcome.pointer_state.captured);
    MIRAGE_CHECK(outcome.environment_state.captured);
    MIRAGE_CHECK(!outcome.observation.environment_state.empty());
    MIRAGE_CHECK(outcome.observation.pointer_state.x == 0);
}

void cancelled_assembly_reports_cancelled_before_any_capture() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_window_and_snapshot(env);
    ObservationAssembler assembler(env);

    CancelToken cancel;
    cancel.request_cancel();

    ObservationComponents all;
    all.active_window = true;
    all.semantic_snapshot = true;
    all.pointer_state = true;
    all.environment_state = true;
    const auto outcome = assembler.assemble(all, {}, cancel);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
    MIRAGE_CHECK(!outcome.active_window.captured);
    MIRAGE_CHECK(!outcome.semantic_snapshot.captured);
    MIRAGE_CHECK(!outcome.pointer_state.captured);
    MIRAGE_CHECK(!outcome.environment_state.captured);
    // Nothing leaked into the observation either.
    MIRAGE_CHECK(outcome.observation.active_window.empty());
    MIRAGE_CHECK(outcome.observation.semantic_snapshot.nodes.empty());
}

void snapshot_budget_refuses_and_keeps_the_registry() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_window_and_snapshot(env);
    ObservationAssembler assembler(env);

    // Populate the reference registry from an earlier successful snapshot.
    const auto previous = env.accessibility()->semantic_snapshot("w1");
    MIRAGE_CHECK(previous.ok);
    MIRAGE_CHECK(env.current_refs.size() == 2);

    ObservationComponents components;
    components.active_window = true;
    components.semantic_snapshot = true;
    ObservationAssemblyLimits tight;
    tight.snapshot_limits.max_nodes = 1; // tree has 2 nodes
    const auto refused = assembler.assemble(components, tight, CancelToken{});

    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(!refused.semantic_snapshot.captured);
    MIRAGE_CHECK(refused.semantic_snapshot.error.code == "snapshot_too_large");
    MIRAGE_CHECK(refused.error.code == "snapshot_too_large");
    // A refused snapshot is not a fresh one: the registry keeps the refs it
    // had, so previously issued handles keep resolving.
    MIRAGE_CHECK(env.current_refs.size() == 2);

    // The exact boundary still succeeds.
    ObservationAssemblyLimits exact;
    exact.snapshot_limits.max_nodes = 2;
    const auto accepted = assembler.assemble(components, exact, CancelToken{});
    MIRAGE_CHECK(accepted.ok);
    MIRAGE_CHECK(accepted.observation.semantic_snapshot.nodes.size() == 2);
}

void pointer_state_reports_the_tracked_position() {
    mirage::testing::FakeDesktopEnvironment env;
    MIRAGE_CHECK(env.input()->pointer_move(42, 7).ok);
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.pointer_state = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.pointer_state.captured);
    MIRAGE_CHECK(outcome.observation.pointer_state.x == 42);
    MIRAGE_CHECK(outcome.observation.pointer_state.y == 7);
}

void pointer_state_fails_closed_without_input_provider() {
    PartialEnvironment env; // input() null
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.pointer_state = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.pointer_state.requested);
    MIRAGE_CHECK(!outcome.pointer_state.captured);
    MIRAGE_CHECK(outcome.pointer_state.error.code == "unsupported_platform");
    MIRAGE_CHECK(outcome.error.code == "unsupported_platform");
}

void pre_cancelled_pointer_request_reports_cancelled() {
    mirage::testing::FakeDesktopEnvironment env;
    ObservationAssembler assembler(env);

    CancelToken cancel;
    cancel.request_cancel();
    ObservationComponents components;
    components.pointer_state = true;
    const auto outcome = assembler.assemble(components, {}, cancel);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(!outcome.pointer_state.captured);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
}

void combined_capture_keeps_successful_components_on_failure() {
    mirage::testing::FakeDesktopEnvironment env;
    seed_window_and_snapshot(env);
    env.input()->pointer_move(-5, 90);
    env.failures.snapshot_error = true;
    ObservationAssembler assembler(env);

    ObservationComponents all;
    all.active_window = true;
    all.semantic_snapshot = true;
    all.pointer_state = true;
    all.environment_state = true;
    const auto outcome = assembler.assemble(all);

    // Explicitly incomplete: ok=false, the first failure is reported, and
    // every component that captured is present in the observation.
    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.active_window.captured);
    MIRAGE_CHECK(!outcome.semantic_snapshot.captured);
    MIRAGE_CHECK(outcome.pointer_state.captured);
    MIRAGE_CHECK(outcome.environment_state.captured);
    MIRAGE_CHECK(outcome.observation.active_window == "Editor");
    MIRAGE_CHECK(outcome.observation.pointer_state.x == -5);
    MIRAGE_CHECK(outcome.observation.pointer_state.y == 90);
    MIRAGE_CHECK(!outcome.observation.environment_state.empty());
    MIRAGE_CHECK(outcome.observation.semantic_snapshot.nodes.empty());

    // The same request without the injected failure captures everything.
    env.failures.snapshot_error = false;
    const auto healthy = assembler.assemble(all);
    MIRAGE_CHECK(healthy.ok);
    MIRAGE_CHECK(healthy.error.code.empty());
    MIRAGE_CHECK(healthy.observation.semantic_snapshot.nodes.size() == 2);
    MIRAGE_CHECK(healthy.observation.active_application == "FakeEditor");
    MIRAGE_CHECK(healthy.observation.pointer_state.x == -5);
}

void active_window_success_survives_snapshot_failure() {
    // active_application is only known from a captured snapshot: a snapshot
    // failure must leave it empty even though the window itself was seen.
    mirage::testing::FakeDesktopEnvironment env;
    seed_window_and_snapshot(env);
    env.failures.snapshot_error = true;
    ObservationAssembler assembler(env);

    ObservationComponents components;
    components.active_window = true;
    components.semantic_snapshot = true;
    const auto outcome = assembler.assemble(components);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.active_window.captured);
    MIRAGE_CHECK(outcome.observation.active_window == "Editor");
    MIRAGE_CHECK(outcome.observation.active_application.empty());
    MIRAGE_CHECK(outcome.observation.focused_element.empty());
}

} // namespace

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[observation_assembler_test] scenario: %s\n", name);
    scenario();
}

int main() {
    run_scenario("empty_request_returns_a_clean_empty_observation",
                 empty_request_returns_a_clean_empty_observation);
    run_scenario("environment_state_lists_providers_in_fixed_order",
                 environment_state_lists_providers_in_fixed_order);
    run_scenario("active_window_component_reports_the_focused_window",
                 active_window_component_reports_the_focused_window);
    run_scenario("active_window_fails_closed_without_a_focused_window",
                 active_window_fails_closed_without_a_focused_window);
    run_scenario("active_window_fails_closed_without_a_window_provider",
                 active_window_fails_closed_without_a_window_provider);
    run_scenario("semantic_snapshot_fills_snapshot_focus_and_application",
                 semantic_snapshot_fills_snapshot_focus_and_application);
    run_scenario("semantic_snapshot_without_active_window_still_resolves_the_focus",
                 semantic_snapshot_without_active_window_still_resolves_the_focus);
    run_scenario("semantic_snapshot_reports_unsupported_window",
                 semantic_snapshot_reports_unsupported_window);
    run_scenario("semantic_snapshot_fails_closed_without_providers",
                 semantic_snapshot_fails_closed_without_providers);
    run_scenario("semantic_snapshot_failure_does_not_block_other_components",
                 semantic_snapshot_failure_does_not_block_other_components);
    run_scenario("cancelled_assembly_reports_cancelled_before_any_capture",
                 cancelled_assembly_reports_cancelled_before_any_capture);
    run_scenario("snapshot_budget_refuses_and_keeps_the_registry",
                 snapshot_budget_refuses_and_keeps_the_registry);
    run_scenario("pointer_state_reports_the_tracked_position",
                 pointer_state_reports_the_tracked_position);
    run_scenario("pointer_state_fails_closed_without_input_provider",
                 pointer_state_fails_closed_without_input_provider);
    run_scenario("pre_cancelled_pointer_request_reports_cancelled",
                 pre_cancelled_pointer_request_reports_cancelled);
    run_scenario("combined_capture_keeps_successful_components_on_failure",
                 combined_capture_keeps_successful_components_on_failure);
    run_scenario("active_window_success_survives_snapshot_failure",
                 active_window_success_survives_snapshot_failure);
    return mirage::testing::finish("observation_assembler_test");
}
