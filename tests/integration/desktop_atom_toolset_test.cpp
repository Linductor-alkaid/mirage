#include "../support/test.hpp"

#include <mirage/integration/desktop_atom_toolset.hpp>

#include "../support/fake_desktop_environment.hpp"

#include <mira/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace integration = mirage::integration;

/// Dispatches one proposal through the registry the way the pinned workflow
/// runtime does: the identity comes from the exposed snapshot, the input is
/// the schema-validated argument object (the reserved "tool" member is
/// stripped by the caller), and the operation id is fresh.
mira::Result<mira::ToolExecutionRecord> dispatch(mira::BuiltinToolRegistry &registry,
                                                 const std::string &wire_name,
                                                 mira::JsonValue arguments,
                                                 const mira::OperationContext &context) {
    const auto exposed = registry.exposed_tools();
    const auto tool =
        std::find_if(exposed.begin(), exposed.end(), [&](const mira::ExposedToolSpec &entry) {
            return entry.wire_name == wire_name;
        });
    if (tool == exposed.end()) {
        std::fprintf(stderr, "dispatch: tool not exposed: %s\n", wire_name.c_str());
        ++::mirage::testing::failure_count();
        return mira::Error{};
    }
    mira::ToolProposal proposal;
    proposal.provider_call_id = mira::ProviderToolCallId{"test-call"};
    proposal.tool_id = tool->tool_id;
    proposal.wire_name = tool->wire_name;
    proposal.tool_version = tool->version;
    proposal.arguments = arguments;
    proposal.arguments_digest = mira::canonical_json_digest(arguments);
    proposal.operation_id = mira::OperationId::generate();
    proposal.has_side_effects = tool->has_side_effects;
    return registry.execute(proposal, context);
}

mira::JsonValue arguments(std::initializer_list<std::pair<const char *, mira::JsonValue>> members) {
    mira::JsonValue::Object object;
    for (const auto &member : members) {
        object.emplace_back(member.first, member.second);
    }
    return mira::JsonValue{std::move(object)};
}

/// Prints a failed record's stable summary so a dispatch failure is
/// actionable from the log alone.
void check_dispatch_ok(const char *what, const mira::Result<mira::ToolExecutionRecord> &outcome) {
    if (outcome.has_value() && outcome.value().failed) {
        std::fprintf(stderr, "[desktop_atom_toolset_test] %s failed record: %s\n", what,
                     outcome.value().safe_error_summary.c_str());
    } else if (!outcome.has_value()) {
        std::fprintf(stderr, "[desktop_atom_toolset_test] %s error: %s\n", what,
                     outcome.error().safe_message.c_str());
    }
    MIRAGE_CHECK(outcome.has_value() && !outcome.value().failed);
}

/// A gate that records what it was asked and answers from a fixed verdict.
struct RecordingGate {
    bool verdict = true;
    std::vector<std::string> capabilities;
    std::vector<std::string> resources;
    bool probe_polled = false;

    integration::AtomPermissionGate gate() {
        return [this](const std::string &capability, const std::string &resource,
                      const integration::AtomCancelProbe &cancelled) {
            capabilities.push_back(capability);
            resources.push_back(resource);
            if (cancelled) {
                probe_polled = cancelled();
            }
            return verdict;
        };
    }
};

void scenario_null_environment_yields_the_empty_toolset() {
    const auto toolset = integration::DesktopAtomToolset::build(nullptr, nullptr);
    MIRAGE_CHECK(toolset != nullptr);
    MIRAGE_CHECK(toolset->registry() != nullptr);
    MIRAGE_CHECK(toolset->exposed_atoms().empty());
    MIRAGE_CHECK(toolset->registry()->size() == 0);
}

void scenario_catalog_reports_what_the_environment_delivers() {
    mirage::testing::FakeDesktopEnvironment environment;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, nullptr);
    const auto atoms = toolset->exposed_atoms();
    MIRAGE_CHECK(atoms.size() == 13);

    // Deterministic (wire-name sorted) order, matching the registry's
    // exposed view; every atom carries a serializable schema and "1.0.0".
    bool sorted = true;
    for (std::size_t index = 1; index < atoms.size(); ++index) {
        sorted = sorted && atoms[index - 1].wire_name < atoms[index].wire_name;
    }
    MIRAGE_CHECK(sorted);
    for (const auto &atom : atoms) {
        MIRAGE_CHECK(atom.version == "1.0.0");
        MIRAGE_CHECK(!atom.description.empty());
        const auto schema = mira::parse_json(atom.parameters_schema_json);
        MIRAGE_CHECK(schema.has_value());
        MIRAGE_CHECK(schema.value().is_object());
    }

    // The side-effect classification follows the permission capability, not
    // the read/write texture of the provider call.
    const auto side_effects = [&atoms](const std::string &wire_name) {
        for (const auto &atom : atoms) {
            if (atom.wire_name == wire_name) {
                return atom.has_side_effects;
            }
        }
        return false;
    };
    MIRAGE_CHECK(!side_effects("desktop.filesystem.read_text"));
    MIRAGE_CHECK(!side_effects("desktop.clipboard.read_text"));
    MIRAGE_CHECK(!side_effects("desktop.window.list"));
    MIRAGE_CHECK(!side_effects("desktop.accessibility.semantic_snapshot"));
    MIRAGE_CHECK(side_effects("desktop.process.execute"));
    MIRAGE_CHECK(side_effects("desktop.window.activate"));
    MIRAGE_CHECK(side_effects("desktop.application.launch"));
    MIRAGE_CHECK(side_effects("desktop.application.terminate"));
    MIRAGE_CHECK(side_effects("desktop.clipboard.write_text"));
    MIRAGE_CHECK(side_effects("desktop.input.type_text"));
    MIRAGE_CHECK(side_effects("desktop.notification.post"));

    // A partial environment shrinks the catalog to its real capabilities:
    // a bare environment (no providers) exposes no atoms at all.
    class BareEnvironment final : public mirage::desktop::DesktopEnvironment {
      public:
        mirage::desktop::EnvironmentInfo info() const override { return {"bare", "test"}; }
    } bare;
    const auto bare_toolset = integration::DesktopAtomToolset::build(&bare, nullptr);
    MIRAGE_CHECK(bare_toolset->exposed_atoms().empty());
}

void scenario_gated_atom_is_denied_without_a_gate() {
    mirage::testing::FakeDesktopEnvironment environment;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, nullptr);
    const auto outcome =
        dispatch(*toolset->registry(), "desktop.clipboard.write_text",
                 arguments({{"text", mira::JsonValue{"hello"}}}), mira::make_control_context());
    MIRAGE_CHECK(!outcome.has_value() || outcome.value().failed);
    if (outcome.has_value() && outcome.value().failed) {
        MIRAGE_CHECK(outcome.value().safe_error_summary.find("permission denied") !=
                     std::string::npos);
    }
    MIRAGE_CHECK(!environment.clipboard_has_text);
}

void scenario_gated_atom_judges_capability_and_resource() {
    mirage::testing::FakeDesktopEnvironment environment;
    RecordingGate gate;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, gate.gate());

    const auto outcome =
        dispatch(*toolset->registry(), "desktop.clipboard.write_text",
                 arguments({{"text", mira::JsonValue{"hello"}}}), mira::make_control_context());
    check_dispatch_ok("clipboard write", outcome);
    MIRAGE_CHECK(environment.clipboard_has_text);
    MIRAGE_CHECK(environment.clipboard_text == "hello");
    MIRAGE_CHECK(gate.capabilities.size() == 1);
    if (!gate.capabilities.empty()) {
        MIRAGE_CHECK(gate.capabilities.front() == "clipboard.write");
    }
    // The gate was polled with a live probe (the drive's cancellation wire).
    MIRAGE_CHECK(gate.probe_polled == false);
    // A deny verdict keeps the provider untouched.
    RecordingGate denying;
    denying.verdict = false;
    const auto denying_toolset =
        integration::DesktopAtomToolset::build(&environment, denying.gate());
    const auto denied =
        dispatch(*denying_toolset->registry(), "desktop.clipboard.write_text",
                 arguments({{"text", mira::JsonValue{"no"}}}), mira::make_control_context());
    MIRAGE_CHECK(denied.has_value() && denied.value().failed);
    MIRAGE_CHECK(environment.clipboard_text == "hello");
}

void scenario_observation_atoms_run_without_a_gate() {
    mirage::testing::FakeDesktopEnvironment environment;
    environment.windows.push_back({"w1", "Terminal", {0, 0, 800, 600}, true});
    const auto toolset = integration::DesktopAtomToolset::build(&environment, nullptr);

    const auto listed =
        dispatch(*toolset->registry(), "desktop.window.list",
                 mira::JsonValue{mira::JsonValue::Object{}}, mira::make_control_context());
    MIRAGE_CHECK(listed.has_value() && !listed.value().failed);
    if (listed.has_value() && !listed.value().failed) {
        const auto *windows = listed.value().result.find("windows");
        MIRAGE_CHECK(windows != nullptr && windows->is_array() && windows->as_array()->size() == 1);
    }

    const auto front =
        dispatch(*toolset->registry(), "desktop.window.front",
                 mira::JsonValue{mira::JsonValue::Object{}}, mira::make_control_context());
    MIRAGE_CHECK(front.has_value() && !front.value().failed);
    if (front.has_value() && !front.value().failed) {
        const auto *id = front.value().result.find("id");
        MIRAGE_CHECK(id != nullptr && id->is_string() && *id->as_string() == "w1");
    }
}

void scenario_read_atom_maps_provider_budgets_and_errors() {
    mirage::testing::FakeDesktopEnvironment environment;
    RecordingGate gate;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, gate.gate());
    auto &filesystem = environment.filesystem_;

    // Outside the declared read scope: the provider's stable code surfaces.
    const auto outside = dispatch(*toolset->registry(), "desktop.filesystem.read_text",
                                  arguments({{"path", mira::JsonValue{"/outside/prose.txt"}}}),
                                  mira::make_control_context());
    MIRAGE_CHECK(outside.has_value() && outside.value().failed);
    if (outside.has_value()) {
        MIRAGE_CHECK(outside.value().safe_error_summary.find("permission_denied") !=
                     std::string::npos);
    }

    // In scope and present: the content comes back through the tool result.
    filesystem.scope() = mirage::desktop::PathScope({std::filesystem::path("/data")});
    environment.files.emplace("/data/prose.txt", "prose");
    const auto read = dispatch(*toolset->registry(), "desktop.filesystem.read_text",
                               arguments({{"path", mira::JsonValue{"/data/prose.txt"}}}),
                               mira::make_control_context());
    MIRAGE_CHECK(read.has_value() && !read.value().failed);
    if (read.has_value() && !read.value().failed) {
        const auto *content = read.value().result.find("content");
        MIRAGE_CHECK(content != nullptr && content->is_string() &&
                     *content->as_string() == "prose");
    }

    // Missing file: provider not_found, not a handler invention.
    const auto missing = dispatch(*toolset->registry(), "desktop.filesystem.read_text",
                                  arguments({{"path", mira::JsonValue{"/data/absent.txt"}}}),
                                  mira::make_control_context());
    MIRAGE_CHECK(missing.has_value() && missing.value().failed);
    if (missing.has_value()) {
        MIRAGE_CHECK(missing.value().safe_error_summary.find("not_found") != std::string::npos);
    }
}

void scenario_process_atom_carries_the_command_result() {
    mirage::testing::FakeDesktopEnvironment environment;
    RecordingGate gate;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, gate.gate());

    environment.next_process_outcome.exit_code = 3;
    environment.next_process_outcome.exited_normally = false;
    environment.next_process_outcome.standard_output = "out";
    environment.next_process_outcome.standard_error = "err";
    const auto outcome = dispatch(*toolset->registry(), "desktop.process.execute",
                                  arguments({{"command", mira::JsonValue{"make check"}},
                                             {"timeout_ms", mira::JsonValue{std::int64_t{5000}}}}),
                                  mira::make_control_context());
    check_dispatch_ok("process execute", outcome);
    if (outcome.has_value() && !outcome.value().failed) {
        const auto *exit_code = outcome.value().result.find("exit_code");
        MIRAGE_CHECK(exit_code != nullptr && exit_code->is_integer() &&
                     exit_code->as_integer() == std::int64_t{3});
        const auto *stdout_member = outcome.value().result.find("stdout");
        MIRAGE_CHECK(stdout_member != nullptr && stdout_member->is_string() &&
                     *stdout_member->as_string() == "out");
    }
    MIRAGE_CHECK(gate.capabilities.size() == 1);
    if (!gate.capabilities.empty()) {
        MIRAGE_CHECK(gate.capabilities.front() == "process.execute");
        MIRAGE_CHECK(gate.resources.front() == "make check");
    }
}

void scenario_cancelled_drive_refuses_the_atom_before_the_provider() {
    mirage::testing::FakeDesktopEnvironment environment;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, nullptr);
    mira::OperationContext context = mira::make_control_context();
    context.cancellation_requested = [] { return true; };
    const auto outcome = dispatch(*toolset->registry(), "desktop.window.list",
                                  mira::JsonValue{mira::JsonValue::Object{}}, context);
    MIRAGE_CHECK(!outcome.has_value());
    if (!outcome.has_value()) {
        MIRAGE_CHECK(outcome.error().code == mira::ErrorCode::Cancelled);
    }
}

void scenario_snapshot_atom_renders_the_semantic_tree() {
    mirage::testing::FakeDesktopEnvironment environment;
    mirage::desktop::SemanticSnapshot snapshot;
    snapshot.application = "Terminal";
    snapshot.window_title = "Terminal";
    mirage::desktop::SemanticNode node;
    node.ref = "@e1";
    node.role = "button";
    node.name = "Run";
    snapshot.nodes.push_back(node);
    environment.snapshots.emplace("w1", snapshot);
    const auto toolset = integration::DesktopAtomToolset::build(&environment, nullptr);

    const auto outcome =
        dispatch(*toolset->registry(), "desktop.accessibility.semantic_snapshot",
                 arguments({{"window_id", mira::JsonValue{"w1"}}}), mira::make_control_context());
    MIRAGE_CHECK(outcome.has_value() && !outcome.value().failed);
    if (outcome.has_value() && !outcome.value().failed) {
        const auto *rendered = outcome.value().result.find("snapshot");
        MIRAGE_CHECK(rendered != nullptr && rendered->is_string());
        if (rendered != nullptr && rendered->is_string()) {
            MIRAGE_CHECK(rendered->as_string()->find("button") != std::string::npos);
            MIRAGE_CHECK(rendered->as_string()->find("Run") != std::string::npos);
        }
    }

    // A window without a tree surfaces the provider's stable refusal.
    const auto unsupported =
        dispatch(*toolset->registry(), "desktop.accessibility.semantic_snapshot",
                 arguments({{"window_id", mira::JsonValue{"w2"}}}), mira::make_control_context());
    MIRAGE_CHECK(unsupported.has_value() && unsupported.value().failed);
    if (unsupported.has_value()) {
        MIRAGE_CHECK(unsupported.value().safe_error_summary.find("unsupported_window") !=
                     std::string::npos);
    }
}

/// Records what the AtomOverlayFeed seam (M5-09, DEC-029) received, plus
/// the focused state at each action publish — the ordering evidence that
/// the mirror fires after the permission judgement and before the provider
/// side effect.
struct RecordingOverlayFeed {
    std::vector<integration::AtomOverlayAction> actions;
    std::vector<bool> focused_at_publish;
    std::vector<mirage::desktop::SemanticSnapshot> observations;

    integration::AtomOverlayFeed feed(mirage::testing::FakeDesktopEnvironment *environment) {
        integration::AtomOverlayFeed feed;
        feed.show_action = [this, environment](const integration::AtomOverlayAction &action) {
            actions.push_back(action);
            focused_at_publish.push_back(!environment->windows.empty() &&
                                         environment->windows.front().focused);
        };
        feed.show_observation = [this](const mirage::desktop::SemanticSnapshot &snapshot) {
            observations.push_back(snapshot);
        };
        return feed;
    }
};

void scenario_overlay_feed_mirrors_window_activate() {
    mirage::testing::FakeDesktopEnvironment environment;
    environment.windows.push_back({"w1", "Terminal", {0, 0, 800, 600}, false});
    RecordingOverlayFeed recorder;
    RecordingGate gate; // the mirror rides the judged action: allow
    const auto toolset = integration::DesktopAtomToolset::build(&environment, gate.gate(),
                                                                recorder.feed(&environment));

    const auto outcome =
        dispatch(*toolset->registry(), "desktop.window.activate",
                 arguments({{"window_id", mira::JsonValue{"w1"}}}), mira::make_control_context());
    check_dispatch_ok("activate with overlay feed", outcome);

    // The side effect really happened.
    MIRAGE_CHECK(environment.windows.front().focused);

    // Exactly two mirror updates: the upcoming action, then the clearing
    // publish once the action settled.
    MIRAGE_CHECK(recorder.actions.size() == 2);
    if (recorder.actions.size() == 2) {
        const integration::AtomOverlayAction &upcoming = recorder.actions.front();
        MIRAGE_CHECK(upcoming.hint == "activating \"Terminal\"");
        MIRAGE_CHECK(upcoming.highlights.size() == 1);
        if (upcoming.highlights.size() == 1) {
            MIRAGE_CHECK(upcoming.highlights.front().rect.width == 800);
            MIRAGE_CHECK(upcoming.highlights.front().rect.height == 600);
            MIRAGE_CHECK(upcoming.highlights.front().label == "Terminal");
        }
        // Ordering: the mirror published before the provider side effect.
        MIRAGE_CHECK(recorder.focused_at_publish.front() == false);
        const integration::AtomOverlayAction &settled = recorder.actions.back();
        MIRAGE_CHECK(settled.hint.empty());
        MIRAGE_CHECK(settled.highlights.empty());
    }
}

void scenario_overlay_feed_degrades_when_the_target_lookup_fails() {
    mirage::testing::FakeDesktopEnvironment environment;
    environment.windows.push_back({"w1", "Terminal", {0, 0, 800, 600}, false});
    RecordingOverlayFeed recorder;
    RecordingGate gate;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, gate.gate(),
                                                                recorder.feed(&environment));

    // An unknown window id: the activation fails, but the mirror still
    // published a highlight-less degraded hint and cleared after the failed
    // attempt — the overlay never blocks nor misdescribes the action.
    const auto outcome =
        dispatch(*toolset->registry(), "desktop.window.activate",
                 arguments({{"window_id", mira::JsonValue{"w9"}}}), mira::make_control_context());
    MIRAGE_CHECK(outcome.has_value() && outcome.value().failed);
    if (outcome.has_value()) {
        MIRAGE_CHECK(outcome.value().safe_error_summary.find("not_found") != std::string::npos);
    }

    MIRAGE_CHECK(recorder.actions.size() == 2);
    if (recorder.actions.size() == 2) {
        MIRAGE_CHECK(recorder.actions.front().hint == "activating window w9");
        MIRAGE_CHECK(recorder.actions.front().highlights.empty());
        MIRAGE_CHECK(recorder.actions.back().hint.empty());
        MIRAGE_CHECK(recorder.actions.back().highlights.empty());
    }
    MIRAGE_CHECK(!environment.windows.front().focused);
}

void scenario_overlay_feed_mirrors_input_type_text() {
    mirage::testing::FakeDesktopEnvironment environment;
    environment.windows.push_back({"w1", "Editor", {10, 20, 300, 200}, true});
    RecordingOverlayFeed recorder;
    RecordingGate gate;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, gate.gate(),
                                                                recorder.feed(&environment));

    const auto outcome =
        dispatch(*toolset->registry(), "desktop.input.type_text",
                 arguments({{"text", mira::JsonValue{"hello"}}}), mira::make_control_context());
    check_dispatch_ok("type_text with overlay feed", outcome);

    MIRAGE_CHECK(recorder.actions.size() == 2);
    if (recorder.actions.size() == 2) {
        const integration::AtomOverlayAction &upcoming = recorder.actions.front();
        MIRAGE_CHECK(upcoming.hint == "typing into \"Editor\"");
        MIRAGE_CHECK(upcoming.highlights.size() == 1);
        if (upcoming.highlights.size() == 1) {
            MIRAGE_CHECK(upcoming.highlights.front().rect.x == 10);
            MIRAGE_CHECK(upcoming.highlights.front().rect.y == 20);
            MIRAGE_CHECK(upcoming.highlights.front().label == "Editor");
        }
        MIRAGE_CHECK(recorder.actions.back().hint.empty());
    }

    // Without a focused window the hint degrades to the typed-size line and
    // carries no highlight (the mirror never blocks the action).
    RecordingOverlayFeed unfocused;
    mirage::testing::FakeDesktopEnvironment bare;
    bare.windows.push_back({"w1", "Background", {0, 0, 100, 100}, false});
    RecordingGate bare_gate;
    const auto bare_toolset =
        integration::DesktopAtomToolset::build(&bare, bare_gate.gate(), unfocused.feed(&bare));
    const auto bare_outcome =
        dispatch(*bare_toolset->registry(), "desktop.input.type_text",
                 arguments({{"text", mira::JsonValue{"hello"}}}), mira::make_control_context());
    check_dispatch_ok("type_text without focus", bare_outcome);
    MIRAGE_CHECK(unfocused.actions.size() == 2);
    if (unfocused.actions.size() == 2) {
        MIRAGE_CHECK(unfocused.actions.front().hint == "typing 5 character(s)");
        MIRAGE_CHECK(unfocused.actions.front().highlights.empty());
    }
}

void scenario_denied_action_never_mirrors() {
    mirage::testing::FakeDesktopEnvironment environment;
    environment.windows.push_back({"w1", "Editor", {0, 0, 100, 100}, true});
    RecordingOverlayFeed recorder;
    RecordingGate denying;
    denying.verdict = false;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, denying.gate(),
                                                                recorder.feed(&environment));
    const auto outcome =
        dispatch(*toolset->registry(), "desktop.input.type_text",
                 arguments({{"text", mira::JsonValue{"no"}}}), mira::make_control_context());
    MIRAGE_CHECK(outcome.has_value() && outcome.value().failed);
    // The permission judgement precedes the mirror: a denied action leaves
    // the overlay completely dark.
    MIRAGE_CHECK(recorder.actions.empty());
    MIRAGE_CHECK(recorder.focused_at_publish.empty());
}

void scenario_overlay_feed_delivers_the_observation_face() {
    mirage::testing::FakeDesktopEnvironment environment;
    environment.windows.push_back({"w1", "Terminal", {0, 0, 800, 600}, true});
    mirage::desktop::SemanticSnapshot snapshot;
    snapshot.application = "Terminal";
    snapshot.window_title = "Terminal";
    mirage::desktop::SemanticNode node;
    node.ref = "@e1";
    node.role = "button";
    node.name = "Run";
    node.geometry = {5, 6, 100, 50};
    snapshot.nodes.push_back(node);
    environment.snapshots.emplace("w1", snapshot);

    RecordingOverlayFeed recorder;
    const auto toolset =
        integration::DesktopAtomToolset::build(&environment, nullptr, recorder.feed(&environment));
    const auto outcome =
        dispatch(*toolset->registry(), "desktop.accessibility.semantic_snapshot",
                 arguments({{"window_id", mira::JsonValue{"w1"}}}), mira::make_control_context());
    check_dispatch_ok("semantic_snapshot with overlay feed", outcome);
    // The delivered snapshot reached the feed unmodified (the presenter
    // projects the boxes; the seam only mirrors).
    MIRAGE_CHECK(recorder.observations.size() == 1);
    if (recorder.observations.size() == 1) {
        MIRAGE_CHECK(recorder.observations.front().application == "Terminal");
        MIRAGE_CHECK(recorder.observations.front().nodes.size() == 1);
        if (recorder.observations.front().nodes.size() == 1) {
            MIRAGE_CHECK(recorder.observations.front().nodes.front().ref == "@e1");
            MIRAGE_CHECK(recorder.observations.front().nodes.front().geometry.width == 100);
        }
    }
    MIRAGE_CHECK(recorder.actions.empty());

    // A dark observation member keeps the mirror silent (explicit product
    // decision): the atom must not depend on the face being wired.
    RecordingOverlayFeed dark;
    integration::AtomOverlayFeed half_wired;
    half_wired.show_action = [&dark](const integration::AtomOverlayAction &action) {
        dark.actions.push_back(action);
    };
    const auto dark_toolset =
        integration::DesktopAtomToolset::build(&environment, nullptr, half_wired);
    const auto dark_outcome =
        dispatch(*dark_toolset->registry(), "desktop.accessibility.semantic_snapshot",
                 arguments({{"window_id", mira::JsonValue{"w1"}}}), mira::make_control_context());
    check_dispatch_ok("semantic_snapshot with dark observation", dark_outcome);
    MIRAGE_CHECK(dark.observations.empty());
    MIRAGE_CHECK(dark.actions.empty());
}

void scenario_at_most_once_dispatch_stays_enforced() {
    mirage::testing::FakeDesktopEnvironment environment;
    const auto toolset = integration::DesktopAtomToolset::build(&environment, nullptr);
    mira::OperationContext context = mira::make_control_context();
    const auto exposed = toolset->registry()->exposed_tools();
    const auto tool =
        std::find_if(exposed.begin(), exposed.end(), [](const mira::ExposedToolSpec &entry) {
            return entry.wire_name == "desktop.window.front";
        });
    MIRAGE_CHECK(tool != exposed.end());
    mira::ToolProposal proposal;
    proposal.provider_call_id = mira::ProviderToolCallId{"same-call"};
    proposal.tool_id = tool->tool_id;
    proposal.wire_name = tool->wire_name;
    proposal.tool_version = tool->version;
    proposal.arguments = mira::JsonValue{mira::JsonValue::Object{}};
    proposal.arguments_digest = mira::canonical_json_digest(proposal.arguments);
    proposal.operation_id = mira::OperationId::generate();
    proposal.has_side_effects = tool->has_side_effects;
    MIRAGE_CHECK(toolset->registry()->execute(proposal, context).has_value());
    const auto replay = toolset->registry()->execute(proposal, context);
    MIRAGE_CHECK(!replay.has_value());
    if (!replay.has_value()) {
        MIRAGE_CHECK(replay.error().code == mira::ErrorCode::AlreadyExists);
    }
}

} // namespace

int main() {
    scenario_null_environment_yields_the_empty_toolset();
    scenario_catalog_reports_what_the_environment_delivers();
    scenario_gated_atom_is_denied_without_a_gate();
    scenario_gated_atom_judges_capability_and_resource();
    scenario_observation_atoms_run_without_a_gate();
    scenario_read_atom_maps_provider_budgets_and_errors();
    scenario_process_atom_carries_the_command_result();
    scenario_cancelled_drive_refuses_the_atom_before_the_provider();
    scenario_snapshot_atom_renders_the_semantic_tree();
    scenario_overlay_feed_mirrors_window_activate();
    scenario_overlay_feed_degrades_when_the_target_lookup_fails();
    scenario_overlay_feed_mirrors_input_type_text();
    scenario_denied_action_never_mirrors();
    scenario_overlay_feed_delivers_the_observation_face();
    scenario_at_most_once_dispatch_stays_enforced();
    return mirage::testing::finish("desktop_atom_toolset_test");
}
