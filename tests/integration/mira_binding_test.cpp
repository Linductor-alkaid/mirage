#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <mira/environment.hpp>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/observation_assembler.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/mira_host.hpp>

#include <executor/executor.hpp>

#include <mirador/pixel_format.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace {

namespace desktop = mirage::desktop;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::HostStatus;
using mirage::runtime::MiraHost;
using mirage::runtime::TaskProgress;

/// Temporary workspace for provider fixtures; removed on scope exit.
class TempWorkspace {
  public:
    TempWorkspace() {
        std::error_code ec;
        root_ = std::filesystem::temp_directory_path(ec) /
                ("mirage-m1-03-" + std::to_string(::getpid()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_, ec);
    }
    ~TempWorkspace() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    TempWorkspace(const TempWorkspace &) = delete;
    TempWorkspace &operator=(const TempWorkspace &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
}

bool is_32_lowercase_hex(const std::string &id) {
    if (id.size() != 32) {
        return false;
    }
    for (const char character : id) {
        const bool digit = character >= '0' && character <= '9';
        const bool lowercase = character >= 'a' && character <= 'f';
        if (!digit && !lowercase) {
            return false;
        }
    }
    return true;
}

void scenario_binding_identity_and_honest_capabilities() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    integration::MiraEnvironmentBinding binding(environment);

    const auto info = environment->info();
    MIRAGE_CHECK(info.platform == "linux");
    MIRAGE_CHECK(!info.name.empty());
    MIRAGE_CHECK(std::string_view(binding.binding_name()) == "mirage.desktop.linux-v1");

    const auto capabilities = binding.capabilities();
    MIRAGE_CHECK(!capabilities.screen_capture);
    MIRAGE_CHECK(!capabilities.ui_tree);
    MIRAGE_CHECK(!capabilities.foreground_app);
    MIRAGE_CHECK(!capabilities.device_state);
    MIRAGE_CHECK(!capabilities.atomic_observation);
    MIRAGE_CHECK(!capabilities.discrete_input);
    MIRAGE_CHECK(!capabilities.input_release);
    MIRAGE_CHECK(!capabilities.epoch_invalidation);
    MIRAGE_CHECK(capabilities.perception_sources == 0);
}

void scenario_observe_fails_closed_on_unsupported_components() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest request;
    request.required.screen = true;
    request.required.structure = true;
    const auto refused = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(!refused.has_value());
    MIRAGE_CHECK(refused.error().code == mira::ErrorCode::UnsupportedCapability);
    MIRAGE_CHECK(refused.error().safe_message.find("screen") != std::string::npos);
    MIRAGE_CHECK(refused.error().safe_message.find("structure") != std::string::npos);

    // An empty requirement is deliverable: a minimal component-free
    // observation is returned instead of silently incomplete content.
    mira::ObservationRequest empty_request;
    const auto observation = binding.observe(empty_request, mira::make_control_context());
    MIRAGE_CHECK(observation.has_value());
    MIRAGE_CHECK(!observation.value().id.is_nil());
    MIRAGE_CHECK(!observation.value().screen.has_value());
    MIRAGE_CHECK(!observation.value().structure.has_value());
    MIRAGE_CHECK(observation.value().perception.empty());
}

void scenario_execute_rejects_and_interrupt_is_idempotent() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    integration::MiraEnvironmentBinding binding(environment);

    mira::InputSequence sequence;
    sequence.events.push_back(mira::InputEvent{"tap", "0.5,0.5"});
    const auto receipt = binding.execute(sequence, mira::make_control_context());
    MIRAGE_CHECK(receipt.has_value());
    MIRAGE_CHECK(receipt.value().status == mira::ExecutionStatus::Rejected);
    MIRAGE_CHECK(!receipt.value().side_effect_may_have_occurred);
    MIRAGE_CHECK(!receipt.value().safe_message.empty());

    const auto first = binding.interrupt(mira::make_control_context());
    const auto second = binding.interrupt(mira::make_control_context());
    MIRAGE_CHECK(first.has_value());
    MIRAGE_CHECK(second.has_value());
}

void scenario_filesystem_provider_reads_and_fails_closed() {
    TempWorkspace workspace;
    // M1-05: reads are scoped to the declared roots; the workspace root is
    // the one readable place in this scenario.
    linux_backend::LinuxDesktopEnvironment environment({workspace.root()});
    const auto goal_path = workspace.root() / "goal.txt";
    write_text_file(goal_path, "mirage says hello\n");

    const auto read = environment.read_text_file(goal_path);
    MIRAGE_CHECK(read.ok);
    MIRAGE_CHECK(read.content == "mirage says hello\n");

    // Negative cases stay inside the scope: they exercise lookup and file
    // validation, not containment.
    const auto missing = environment.read_text_file(workspace.root() / "missing.txt");
    MIRAGE_CHECK(!missing.ok);
    MIRAGE_CHECK(missing.error.code == "not_found");

    const auto directory = environment.read_text_file(workspace.root());
    MIRAGE_CHECK(!directory.ok);
    MIRAGE_CHECK(directory.error.code == "invalid_argument");

    const auto empty_path = environment.read_text_file({});
    MIRAGE_CHECK(!empty_path.ok);
    MIRAGE_CHECK(empty_path.error.code == "invalid_argument");

    // M1-05 fail-closed contract: a default-constructed environment declares
    // no read roots and must refuse every read with permission_denied, even
    // for an existing regular file. The refusal reports the requested path,
    // never any resolved target.
    linux_backend::LinuxDesktopEnvironment unscoped;
    const auto denied = unscoped.read_text_file(goal_path);
    MIRAGE_CHECK(!denied.ok);
    MIRAGE_CHECK(denied.error.code == "permission_denied");
    MIRAGE_CHECK(denied.error.message.find(goal_path.string()) != std::string::npos);
    MIRAGE_CHECK(denied.content.empty());
}

void scenario_process_provider_captures_streams_and_exit_code() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{5000};
    const auto outcome =
        environment.execute("printf 'out-line'; printf 'err-line' 1>&2; exit 3", limits);
    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.exited_normally);
    MIRAGE_CHECK(outcome.exit_code == 3);
    MIRAGE_CHECK(outcome.standard_output == "out-line");
    MIRAGE_CHECK(outcome.standard_error == "err-line");
    MIRAGE_CHECK(!outcome.timed_out);
    MIRAGE_CHECK(!outcome.output_truncated);
}

void scenario_process_provider_enforces_budget() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{200};
    const auto started = std::chrono::steady_clock::now();
    const auto outcome = environment.execute("sleep 30", limits);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.timed_out);
    MIRAGE_CHECK(outcome.error.code == "deadline_exceeded");
    MIRAGE_CHECK(elapsed < std::chrono::seconds{5});

    desktop::ProcessLimits invalid;
    invalid.timeout = std::chrono::milliseconds::zero();
    const auto zero_budget = environment.execute("true", invalid);
    MIRAGE_CHECK(!zero_budget.ok);
    MIRAGE_CHECK(zero_budget.error.code == "invalid_argument");

    const auto empty_command = environment.execute("", limits);
    MIRAGE_CHECK(!empty_command.ok);
    MIRAGE_CHECK(empty_command.error.code == "invalid_argument");
}

void scenario_operation_surface_rejects_invalid_identities() {
    MiraHost host;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    MIRAGE_CHECK(host.start(std::make_shared<integration::MiraEnvironmentBinding>(environment)).ok);

    const auto malformed = host.begin_operation(mirage::runtime::TaskIdentity{"xyz"});
    MIRAGE_CHECK(!malformed.ok);
    MIRAGE_CHECK(malformed.error.code == "invalid_argument");

    // Well-formed but unknown task.
    const auto unknown =
        host.begin_operation(mirage::runtime::TaskIdentity{"00000000000000000000000000000000"});
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "pinned_runtime");

    mirage::runtime::OperationTicket malformed_ticket{"xyz", 0, "abc", "def"};
    const auto rejected = host.admit_operation_completion(malformed_ticket);
    MIRAGE_CHECK(!rejected.ok);
    MIRAGE_CHECK(rejected.error.code == "invalid_argument");

    MIRAGE_CHECK(host.shutdown().ok);
}

void scenario_end_to_end_task_reads_file_and_executes_shell() {
    TempWorkspace workspace;
    const auto goal_path = workspace.root() / "goal.txt";
    write_text_file(goal_path, "structured result payload\n");

    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>(
        std::vector<std::filesystem::path>{workspace.root()});
    auto binding = std::make_shared<integration::MiraEnvironmentBinding>(environment);
    MiraHost host;
    const auto started = host.start(binding);
    MIRAGE_CHECK(started.ok);
    MIRAGE_CHECK(host.status() == HostStatus::Running);

    const auto submission = host.submit_task("read the goal file and run a shell command");
    MIRAGE_CHECK(submission.ok);
    MIRAGE_CHECK(is_32_lowercase_hex(submission.task.id));
    const mirage::runtime::TaskIdentity task = submission.task;

    // Harness-side driver loop (DEC-008): each desktop action is bracketed by
    // the pinned operation boundary, so the work is visible in the control
    // plane.
    const auto read_operation = host.begin_operation(task);
    MIRAGE_CHECK(read_operation.ok);
    MIRAGE_CHECK(read_operation.ticket.task_id == task.id);
    MIRAGE_CHECK(is_32_lowercase_hex(read_operation.ticket.step_id));
    MIRAGE_CHECK(is_32_lowercase_hex(read_operation.ticket.operation_id));

    const auto file_read = environment->filesystem()->read_text_file(goal_path);
    MIRAGE_CHECK(file_read.ok);
    MIRAGE_CHECK(file_read.content == "structured result payload\n");
    MIRAGE_CHECK(host.admit_operation_completion(read_operation.ticket).ok);

    const auto shell_operation = host.begin_operation(task);
    MIRAGE_CHECK(shell_operation.ok);
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{5000};
    const auto shell = environment->process()->execute("printf 'shell says mirage'", limits);
    MIRAGE_CHECK(shell.ok);
    MIRAGE_CHECK(shell.exit_code == 0);
    MIRAGE_CHECK(shell.standard_output == "shell says mirage");
    MIRAGE_CHECK(host.admit_operation_completion(shell_operation.ticket).ok);

    // The task is still non-terminal after two settled operations; the
    // harness decides goal success.
    const auto mid_view = host.task_view(task);
    MIRAGE_CHECK(mid_view.ok);
    MIRAGE_CHECK(mid_view.view.progress == TaskProgress::Idle);

    MIRAGE_CHECK(host.complete_task(task, true).ok);
    const auto done_view = host.task_view(task);
    MIRAGE_CHECK(done_view.ok);
    MIRAGE_CHECK(done_view.view.progress == TaskProgress::Completed);
    MIRAGE_CHECK(done_view.view.success);

    const auto shutdown = host.shutdown();
    MIRAGE_CHECK(shutdown.ok);
    MIRAGE_CHECK(shutdown.report.clean);
    MIRAGE_CHECK(host.status() == HostStatus::Stopped);
}

void scenario_operation_completion_does_not_revive_cancelled_task() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<integration::MiraEnvironmentBinding>(environment)).ok);

    const auto submission = host.submit_task("cancelled mid-operation");
    MIRAGE_CHECK(submission.ok);
    const mirage::runtime::TaskIdentity task = submission.task;

    const auto operation = host.begin_operation(task);
    MIRAGE_CHECK(operation.ok);
    MIRAGE_CHECK(host.cancel_task(task).ok);

    // Pinned behavior: the ticket belongs to a cancelled task era, so the
    // completion settles as a stale NoOp. The host reports the idempotent
    // ok, but the observable invariant is that the task stays Cancelled —
    // a late completion never revives settled work.
    const auto late = host.admit_operation_completion(operation.ticket);
    MIRAGE_CHECK(late.ok);
    const auto view = host.task_view(task);
    MIRAGE_CHECK(view.ok);
    MIRAGE_CHECK(view.view.progress == TaskProgress::Cancelled);

    MIRAGE_CHECK(host.shutdown().ok);
}

// ---- M2-06: observe/capabilities over a fully populated fake environment ----
//
// The default LinuxDesktopEnvironment scenarios above pin the honest all-false
// capability baseline; the scenarios below bind the FakeDesktopEnvironment
// (all nine providers present) so the structure/foreground delivery path, the
// required/optional component policy and the pinned error mapping are
// observable without an X server.

desktop::SemanticNode fake_node(std::string ref, std::string role, std::string name,
                                std::size_t parent, bool focused = false, bool enabled = true) {
    desktop::SemanticNode n;
    n.ref = std::move(ref);
    n.role = std::move(role);
    n.name = std::move(name);
    n.parent = parent;
    n.focused = focused;
    n.enabled = enabled;
    n.geometry = {5, 6, 40, 12};
    return n;
}

/// Focused window "w1" ("Editor") with a four-node snapshot whose roles,
/// states and geometry exercise every projection branch of the binding.
void seed_fake_desktop(mirage::testing::FakeDesktopEnvironment &env) {
    desktop::WindowInfo window;
    window.id = "w1";
    window.title = "Editor";
    window.geometry = {10, 20, 800, 600};
    window.focused = true;
    env.windows.push_back(window);

    desktop::SemanticSnapshot snapshot;
    snapshot.application = "FakeEditor";
    snapshot.window_title = "Editor";
    snapshot.nodes.push_back(fake_node("@e1", "window", "Editor", desktop::kNoParent));
    snapshot.nodes.push_back(fake_node("@e2", "panel", "main", 0));
    snapshot.nodes.push_back(fake_node("@e3", "button", "Run", 1));
    // Focused but disabled: exercises both state flags independently.
    snapshot.nodes.push_back(fake_node("@e4", "entry", "Name", 1, /*focused=*/true,
                                       /*enabled=*/false));
    env.snapshots["w1"] = snapshot;
}

void fake_capabilities_are_reported_honestly() {
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    integration::MiraEnvironmentBinding binding(environment);

    const auto capabilities = binding.capabilities();
    MIRAGE_CHECK(capabilities.foreground_app);
    MIRAGE_CHECK(capabilities.ui_tree);
    MIRAGE_CHECK(!capabilities.screen_capture);
    MIRAGE_CHECK(!capabilities.device_state);
    MIRAGE_CHECK(!capabilities.discrete_input);
    MIRAGE_CHECK(!capabilities.atomic_observation);
    MIRAGE_CHECK(!capabilities.input_release);
    MIRAGE_CHECK(!capabilities.epoch_invalidation);
    MIRAGE_CHECK(capabilities.perception_sources == 0);
    MIRAGE_CHECK(capabilities.max_component_skew == std::chrono::nanoseconds::zero());

    // execute/interrupt keep their M2-05 semantics on a capable environment:
    // dispatch is refused before any side effect, interrupt stays idempotent.
    mira::InputSequence sequence;
    sequence.events.push_back(mira::InputEvent{"tap", "0.5,0.5"});
    const auto receipt = binding.execute(sequence, mira::make_control_context());
    MIRAGE_CHECK(receipt.has_value());
    MIRAGE_CHECK(receipt.value().status == mira::ExecutionStatus::Rejected);
    MIRAGE_CHECK(!receipt.value().side_effect_may_have_occurred);
    MIRAGE_CHECK(binding.interrupt(mira::make_control_context()).has_value());
    MIRAGE_CHECK(binding.interrupt(mira::make_control_context()).has_value());
}

void fake_observe_delivers_structure_and_foreground() {
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    seed_fake_desktop(*environment);
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest request;
    request.required.structure = true;
    request.required.foreground = true;
    const auto result = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    const mira::Observation &observation = result.value();

    MIRAGE_CHECK(!observation.id.is_nil());
    MIRAGE_CHECK(observation.atomicity == mira::ObservationAtomicity::NonAtomic);
    MIRAGE_CHECK(observation.environment_epoch == 0);
    MIRAGE_CHECK(observation.perception.empty());
    MIRAGE_CHECK(observation.topology.displays.empty()); // fake declares no displays
    MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Good);
    MIRAGE_CHECK(observation.quality.degradations.empty());

    // The aggregate span and every component capture are ordered in time.
    MIRAGE_CHECK(observation.aggregate_span.normalized_begin.monotonic <=
                 observation.aggregate_span.normalized_end.monotonic);

    // Structure component: present, pinned-validator clean, projection exact.
    MIRAGE_CHECK(observation.structure.has_value());
    const mira::UiTreeSnapshot &structure = observation.structure->value;
    MIRAGE_CHECK(mira::validate_ui_tree_snapshot(structure).has_value());
    MIRAGE_CHECK(structure.nodes.size() == 4);
    MIRAGE_CHECK(structure.complete);
    MIRAGE_CHECK(!structure.truncated);
    MIRAGE_CHECK(!structure.visible_only);
    MIRAGE_CHECK(!structure.space.is_nil());
    MIRAGE_CHECK(structure.max_depth_reached == 3);
    MIRAGE_CHECK(observation.structure->capture.normalized_begin.monotonic <=
                 observation.structure->capture.normalized_end.monotonic);
    MIRAGE_CHECK(observation.structure->quality == mira::ComponentQuality::Good);
    MIRAGE_CHECK(observation.structure->provenance.source == "mirage.desktop.accessibility");

    std::set<mira::UiNodeId> ids;
    for (std::size_t i = 0; i < structure.nodes.size(); ++i) {
        MIRAGE_CHECK(!structure.nodes[i].id.is_nil());
        MIRAGE_CHECK(ids.insert(structure.nodes[i].id).second); // fresh, unique ids
        MIRAGE_CHECK(structure.nodes[i].space == structure.space);
        MIRAGE_CHECK(structure.nodes[i].stable_hint.has_value());
        MIRAGE_CHECK(structure.nodes[i].stable_hint->hint ==
                     "@e" + std::to_string(i + 1)); // @eN refs survive as hints
        MIRAGE_CHECK(structure.nodes[i].provenance.source == "mirage.desktop.accessibility");
    }
    // Parent indices are resolved onto the fresh ids.
    MIRAGE_CHECK(!structure.nodes[0].parent.has_value());
    MIRAGE_CHECK(structure.nodes[1].parent == structure.nodes[0].id);
    MIRAGE_CHECK(structure.nodes[2].parent == structure.nodes[1].id);
    MIRAGE_CHECK(structure.nodes[3].parent == structure.nodes[1].id);
    // Role projection, text, bounds (global pixel space) and state flags.
    MIRAGE_CHECK(structure.nodes[0].role == mira::UiRole::Window);
    MIRAGE_CHECK(structure.nodes[0].text == "Editor");
    MIRAGE_CHECK(structure.nodes[1].role == mira::UiRole::Pane);
    MIRAGE_CHECK(structure.nodes[2].role == mira::UiRole::Button);
    MIRAGE_CHECK(structure.nodes[2].text == "Run");
    MIRAGE_CHECK(structure.nodes[3].role == mira::UiRole::TextField);
    MIRAGE_CHECK(structure.nodes[3].text == "Name");
    for (std::size_t i = 0; i < structure.nodes.size(); ++i) {
        // geometry {5, 6, 40, 12} -> RectF{5, 6, 45, 18} per node.
        MIRAGE_CHECK(structure.nodes[i].bounds.left == 5.0);
        MIRAGE_CHECK(structure.nodes[i].bounds.top == 6.0);
        MIRAGE_CHECK(structure.nodes[i].bounds.right == 45.0);
        MIRAGE_CHECK(structure.nodes[i].bounds.bottom == 18.0);
    }
    // The three enabled nodes carry Enabled and no Focused; @e4 carries the
    // inverse combination, so both flags move independently.
    for (std::size_t i = 0; i < 3; ++i) {
        MIRAGE_CHECK(mira::has_state(structure.nodes[i].state, mira::UiNodeState::Enabled));
        MIRAGE_CHECK(!mira::has_state(structure.nodes[i].state, mira::UiNodeState::Focused));
    }
    MIRAGE_CHECK(!mira::has_state(structure.nodes[3].state, mira::UiNodeState::Enabled));
    MIRAGE_CHECK(mira::has_state(structure.nodes[3].state, mira::UiNodeState::Focused));

    // Foreground component: package from the accessibility application root,
    // activity from the window title.
    MIRAGE_CHECK(observation.foreground.has_value());
    MIRAGE_CHECK(observation.foreground->value.package_name == "FakeEditor");
    MIRAGE_CHECK(observation.foreground->value.activity_name == "Editor");
    MIRAGE_CHECK(observation.foreground->capture.normalized_begin.monotonic <=
                 observation.foreground->capture.normalized_end.monotonic);
    MIRAGE_CHECK(observation.foreground->quality == mira::ComponentQuality::Good);
    MIRAGE_CHECK(observation.foreground->provenance.source == "mirage.desktop.window");
}

void fake_observe_without_components_returns_minimal_observation() {
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    seed_fake_desktop(*environment);
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest request;
    const auto result = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    const mira::Observation &observation = result.value();
    MIRAGE_CHECK(!observation.id.is_nil());
    MIRAGE_CHECK(!observation.structure.has_value());
    MIRAGE_CHECK(!observation.foreground.has_value());
    MIRAGE_CHECK(!observation.screen.has_value());
    MIRAGE_CHECK(observation.perception.empty());
    MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Good);
    MIRAGE_CHECK(observation.quality.degradations.empty());
}

void fake_observe_fails_closed_when_required_structure_is_missing() {
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    // Focused window hit, but no accessibility tree for it.
    desktop::WindowInfo window;
    window.id = "w1";
    window.title = "Editor";
    window.focused = true;
    environment->windows.push_back(window);
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest request;
    request.required.structure = true;
    request.required.foreground = true;
    const auto unsupported = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(!unsupported.has_value());
    MIRAGE_CHECK(unsupported.error().code == mira::ErrorCode::UnsupportedCapability);
    MIRAGE_CHECK(unsupported.error().safe_message.find("structure") != std::string::npos);
}

void fake_observe_reports_not_found_without_any_window() {
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest foreground_only;
    foreground_only.required.foreground = true;
    const auto no_window = binding.observe(foreground_only, mira::make_control_context());
    MIRAGE_CHECK(!no_window.has_value());
    MIRAGE_CHECK(no_window.error().code == mira::ErrorCode::NotFound);
    MIRAGE_CHECK(no_window.error().safe_message.find("foreground") != std::string::npos);

    // A structure requirement over the same empty desktop reports not_found
    // too: the snapshot is taken against the focused window and none exists.
    mira::ObservationRequest structure_too;
    structure_too.required.structure = true;
    const auto structure_not_found = binding.observe(structure_too, mira::make_control_context());
    MIRAGE_CHECK(!structure_not_found.has_value());
    MIRAGE_CHECK(structure_not_found.error().code == mira::ErrorCode::NotFound);
    MIRAGE_CHECK(structure_not_found.error().safe_message.find("structure") != std::string::npos);
}

void fake_observe_maps_provider_errors_onto_the_pinned_vocabulary() {
    // PlatformError: an injected I/O failure inside the snapshot capture.
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    seed_fake_desktop(*environment);
    environment->failures.snapshot_error = true;
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest request;
    request.required.structure = true;
    const auto io_failure = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(!io_failure.has_value());
    MIRAGE_CHECK(io_failure.error().code == mira::ErrorCode::PlatformError);
    MIRAGE_CHECK(io_failure.error().safe_message.find("io_error") != std::string::npos);

    // ResourceExhausted: a snapshot one node over the default 4096 budget is
    // refused, never truncated ("too_large" suffix mapping).
    auto oversized = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    seed_fake_desktop(*oversized);
    desktop::SemanticSnapshot huge;
    huge.application = "FakeEditor";
    huge.nodes.push_back(fake_node("@e1", "window", "Editor", desktop::kNoParent));
    for (std::size_t i = 0; i < 4096; ++i) {
        huge.nodes.push_back(fake_node("@n" + std::to_string(i), "button", "node", 0));
    }
    oversized->snapshots["w1"] = std::move(huge);
    MIRAGE_CHECK(oversized->accessibility()->semantic_snapshot("w1").error.code ==
                 "snapshot_too_large");
    integration::MiraEnvironmentBinding oversized_binding(oversized);
    const auto exhausted = oversized_binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(!exhausted.has_value());
    MIRAGE_CHECK(exhausted.error().code == mira::ErrorCode::ResourceExhausted);
    MIRAGE_CHECK(exhausted.error().safe_message.find("structure") != std::string::npos);
}

void fake_observe_degrades_optional_structure() {
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    desktop::WindowInfo window;
    window.id = "w1";
    window.title = "Editor";
    window.focused = true;
    environment->windows.push_back(window); // no snapshots entry for w1
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest request;
    request.required.foreground = true;
    request.optional.structure = true;
    const auto result = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    const mira::Observation &observation = result.value();
    MIRAGE_CHECK(!observation.structure.has_value()); // optional miss is not fatal...
    MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Degraded);
    bool structure_degradation = false;
    for (const auto &note : observation.quality.degradations) {
        structure_degradation = structure_degradation || note.find("structure unavailable") == 0;
    }
    MIRAGE_CHECK(structure_degradation);
    // ...and the required foreground component is still delivered. Its
    // package name comes from the accessibility root, which only a captured
    // structure provides, so it degrades to an empty name with the title kept.
    MIRAGE_CHECK(observation.foreground.has_value());
    MIRAGE_CHECK(observation.foreground->value.activity_name == "Editor");
    MIRAGE_CHECK(observation.foreground->value.package_name.empty());
}

void fake_observe_refuses_required_screen_despite_screen_provider() {
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    environment->displays.push_back({"d1", {0, 0, 640, 480}, true});
    integration::MiraEnvironmentBinding binding(environment);

    // The environment has a screen provider, but the binding cannot declare
    // screen_capture until the M3 artifact-store integration: the request
    // must fail closed at the capability gate, before any capture.
    MIRAGE_CHECK(environment->screen() != nullptr);
    MIRAGE_CHECK(!binding.capabilities().screen_capture);

    mira::ObservationRequest request;
    request.required.screen = true;
    const auto refused = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(!refused.has_value());
    MIRAGE_CHECK(refused.error().code == mira::ErrorCode::UnsupportedCapability);
    MIRAGE_CHECK(refused.error().safe_message.find("screen") != std::string::npos);
}

void fake_observe_honours_operation_cancellation_and_deadlines() {
    auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    seed_fake_desktop(*environment);
    integration::MiraEnvironmentBinding binding(environment);

    // Cancelled before capture.
    mira::OperationContext cancelled_context = mira::make_control_context();
    cancelled_context.cancellation_requested = [] { return true; };
    mira::ObservationRequest request;
    request.required.structure = true;
    request.required.foreground = true;
    const auto cancelled = binding.observe(request, cancelled_context);
    MIRAGE_CHECK(!cancelled.has_value());
    MIRAGE_CHECK(cancelled.error().code == mira::ErrorCode::Cancelled);

    // A deadline that expires during the capture boundary fails the whole
    // observation even though the captures themselves succeeded.
    mira::OperationContext expired_context = mira::make_control_context();
    expired_context.started_at = mira::Timestamp::now();
    expired_context.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds{1};
    const auto expired = binding.observe(request, expired_context);
    MIRAGE_CHECK(!expired.has_value());
    MIRAGE_CHECK(expired.error().code == mira::ErrorCode::DeadlineExceeded);

    // An open, live context delivers the observation.
    const auto healthy = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(healthy.has_value());
}

// ---- M3-05: the wired visual surface (EnvironmentVisualPipeline + artifact
// store) over the fake environment's screen provider -------------------------
//
// The scenarios below wire a real VisualObservationPipeline (fake OCR /
// detection backends on an Executor blocking worker) and a MemoryArtifactStore
// into the binding, so the capture -> artifact publication -> session analysis
// -> registry publication cycle and the required/optional visual policy are
// observable without an X server. The fake backends are deterministic and
// identity-semantics only; nothing here speaks about real model quality
// (RULE-08).

struct WiredVisualOptions {
    bool with_store = true;
    bool with_backends = true;
    bool run_ocr = true;
    bool run_detector = true;
    bool start_pipeline = true;
    std::size_t store_bytes = std::size_t{256} << 20;
    /// Session / worker identity; empty derives a unique name (the executor
    /// registers blocking workers by name for its whole lifetime, so every
    /// fixture on the shared executor needs its own).
    std::string source_id;
};

struct WiredVisualFixture {
    std::shared_ptr<mirage::testing::FakeDesktopEnvironment> env =
        std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    std::unique_ptr<desktop::VisualReferenceRegistry> registry =
        std::make_unique<desktop::VisualReferenceRegistry>();
    std::unique_ptr<integration::FakeOcrBackend> ocr;
    std::unique_ptr<integration::FakeDetectorBackend> detector;
    std::unique_ptr<mira::MemoryArtifactStore> store;
    std::unique_ptr<integration::VisualObservationPipeline> pipeline;
    std::unique_ptr<integration::MiraEnvironmentBinding> binding;
};

/// One 640x480 primary display "d1" at the origin, a seeded focused window
/// and (optionally) the full visual wiring, pipeline started per options.
WiredVisualFixture make_wired_visual_fixture(executor::Executor &executor,
                                             const WiredVisualOptions &options = {}) {
    WiredVisualFixture fixture;
    fixture.env->displays.push_back({"d1", {0, 0, 640, 480}, true});
    seed_fake_desktop(*fixture.env);

    if (options.with_backends) {
        integration::FakeOcrConfig ocr_config;
        ocr_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
        fixture.ocr = std::make_unique<integration::FakeOcrBackend>(ocr_config);
        integration::FakeDetectorConfig detector_config;
        detector_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
        fixture.detector = std::make_unique<integration::FakeDetectorBackend>(detector_config);
    }
    if (options.with_store) {
        fixture.store = std::make_unique<mira::MemoryArtifactStore>(options.store_bytes);
    }
    static int fixture_counter = 0;
    integration::VisualObservationPipelineConfig config;
    config.source_id = options.source_id.empty()
                           ? "mirage.fake.screen-" + std::to_string(++fixture_counter)
                           : options.source_id;
    config.session.ocr_backend = fixture.ocr.get();
    config.session.detector_backend = fixture.detector.get();
    config.display_id = "d1";
    config.run_ocr = options.run_ocr;
    config.run_detector = options.run_detector;
    fixture.pipeline = std::make_unique<integration::VisualObservationPipeline>(
        executor, *fixture.env->screen(), *fixture.registry, config);
    if (options.start_pipeline) {
        std::string error;
        MIRAGE_CHECK(fixture.pipeline->start(error));
    }
    integration::MiraEnvironmentBinding::VisualWiring wiring;
    wiring.pipeline = fixture.pipeline.get();
    wiring.artifacts = fixture.store.get();
    fixture.binding = std::make_unique<integration::MiraEnvironmentBinding>(fixture.env, wiring);
    return fixture;
}

bool is_nonzero_digest(const mira::Sha256Digest &digest) {
    for (const std::uint8_t byte : digest.bytes) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

void wired_visual_capabilities_are_reported_honestly(executor::Executor &executor) {
    // The whole cycle must be wired and live: pipeline + artifact store +
    // started session + screen provider. Each missing piece keeps the visual
    // surface undeclared.
    {
        const WiredVisualFixture fixture = make_wired_visual_fixture(executor);
        const auto capabilities = fixture.binding->capabilities();
        MIRAGE_CHECK(capabilities.screen_capture);
        MIRAGE_CHECK(capabilities.perception_sources == 1);
        MIRAGE_CHECK(capabilities.foreground_app);
        MIRAGE_CHECK(capabilities.ui_tree);
    }
    { // A pipeline without a store could not deliver the pinned payload
      // record, so it must not claim the screen surface.
        WiredVisualOptions options;
        options.with_store = false;
        const WiredVisualFixture fixture = make_wired_visual_fixture(executor, options);
        const auto capabilities = fixture.binding->capabilities();
        MIRAGE_CHECK(!capabilities.screen_capture);
        MIRAGE_CHECK(capabilities.perception_sources == 0);
    }
    { // Wired but not started: the session cannot serve a refresh yet.
        WiredVisualOptions options;
        options.start_pipeline = false;
        const WiredVisualFixture fixture = make_wired_visual_fixture(executor, options);
        const auto capabilities = fixture.binding->capabilities();
        MIRAGE_CHECK(!capabilities.screen_capture);
        MIRAGE_CHECK(capabilities.perception_sources == 0);
    }
    { // Stopping the pipeline withdraws the declaration (the host is
      // one-shot, so this is the terminal state of the scenario).
        WiredVisualFixture fixture = make_wired_visual_fixture(executor);
        MIRAGE_CHECK(fixture.binding->capabilities().screen_capture);
        fixture.pipeline->stop();
        MIRAGE_CHECK(!fixture.pipeline->running());
        const auto capabilities = fixture.binding->capabilities();
        MIRAGE_CHECK(!capabilities.screen_capture);
        MIRAGE_CHECK(capabilities.perception_sources == 0);
    }
}

void wired_required_screen_delivers_a_validator_clean_frame(executor::Executor &executor) {
    WiredVisualFixture fixture = make_wired_visual_fixture(executor);

    mira::ObservationRequest request;
    request.required.screen = true;
    const auto result = fixture.binding->observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    if (!result.has_value()) {
        std::fprintf(stderr, "observe failed: %s\n", result.error().safe_message.c_str());
        return;
    }
    const mira::Observation &observation = result.value();
    MIRAGE_CHECK(observation.screen.has_value());
    if (!observation.screen.has_value()) {
        return;
    }
    const mira::ScreenFrameDescriptor &frame = observation.screen->value;
    MIRAGE_CHECK(mira::validate_frame_descriptor(frame).has_value());
    MIRAGE_CHECK(frame.width_pixels == 640);
    MIRAGE_CHECK(frame.height_pixels == 480);
    MIRAGE_CHECK(frame.pixel_format == mira::PixelFormat::BGRA8888);
    MIRAGE_CHECK(frame.color_space == mira::ColorSpace::SRGB);
    MIRAGE_CHECK(frame.alpha_mode == mira::AlphaMode::Opaque);
    MIRAGE_CHECK(frame.native_rotation == mira::Rotation::Rotation0);
    MIRAGE_CHECK(frame.planes.size() == 1);
    if (frame.planes.size() == 1) {
        MIRAGE_CHECK(frame.planes[0].offset == 0);
        MIRAGE_CHECK(frame.planes[0].row_stride == 640U * 4U);
        MIRAGE_CHECK(frame.planes[0].pixel_stride == 4);
        MIRAGE_CHECK(frame.planes[0].width == 640);
        MIRAGE_CHECK(frame.planes[0].height == 480);
    }
    MIRAGE_CHECK(frame.payload_media_type == "image/x-bgra8888");
    MIRAGE_CHECK(frame.payload_byte_size == std::size_t{640} * 4U * 480U);
    MIRAGE_CHECK(is_nonzero_digest(frame.payload_digest));
    MIRAGE_CHECK(observation.screen->provenance.source == "mirage.desktop.screen");
    MIRAGE_CHECK(observation.screen->provenance.method == "screen-capture");
    MIRAGE_CHECK(observation.screen->quality == mira::ComponentQuality::Good);

    // The display id maps onto this observation's topology entry for "d1".
    MIRAGE_CHECK(observation.topology.displays.size() == 1);
    if (observation.topology.displays.size() == 1) {
        MIRAGE_CHECK(frame.display_id == observation.topology.displays[0].id);
    }

    // The payload reopens through the wired store and holds exactly the
    // captured bytes (uniform 0x5A fill of the fake provider).
    mira::ArtifactDescriptor record;
    record.id = frame.payload_artifact;
    record.digest = frame.payload_digest;
    record.byte_size = frame.payload_byte_size;
    record.media_type = frame.payload_media_type;
    const auto reopened = fixture.store->open(record);
    MIRAGE_CHECK(reopened.has_value());
    if (reopened.has_value()) {
        MIRAGE_CHECK(reopened.value().size() == frame.payload_byte_size);
        MIRAGE_CHECK(reopened.value().bytes().size() == frame.payload_byte_size);
        if (reopened.value().bytes().size() == frame.payload_byte_size) {
            MIRAGE_CHECK(std::to_integer<unsigned>(reopened.value().bytes().front()) == 0x5AU);
        }
    }

    // A screen-only request stays a capture-only refresh: no analysis ran,
    // no perception is claimed, quality stays clean.
    MIRAGE_CHECK(observation.perception.empty());
    MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Good);
    MIRAGE_CHECK(observation.quality.degradations.empty());
    MIRAGE_CHECK(fixture.env->capture_calls == 1);
    MIRAGE_CHECK(fixture.ocr->calls() == 0);
    MIRAGE_CHECK(fixture.detector->calls() == 0);
    MIRAGE_CHECK(fixture.registry->size() == 0); // nothing was published
}

void wired_required_perception_projects_published_regions(executor::Executor &executor) {
    WiredVisualFixture fixture = make_wired_visual_fixture(executor);

    mira::ObservationRequest request;
    request.required.perception = 1;
    const auto result = fixture.binding->observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    if (!result.has_value()) {
        std::fprintf(stderr, "observe failed: %s\n", result.error().safe_message.c_str());
        return;
    }
    const mira::Observation &observation = result.value();

    // The fake fusion merges the full-view OCR and detection evidence into
    // one region: kOcr form, deterministic text, frame-range bounds in
    // global desktop coordinates (the display sits at the origin).
    MIRAGE_CHECK(observation.perception.size() == 1);
    if (observation.perception.size() == 1) {
        const mira::PerceptionEvidence &evidence = observation.perception[0].value;
        MIRAGE_CHECK(!evidence.id.is_nil());
        MIRAGE_CHECK(evidence.kind == "ocr.text");
        MIRAGE_CHECK(evidence.label == "mirage-fake");
        MIRAGE_CHECK(evidence.bounds.left == 0.0);
        MIRAGE_CHECK(evidence.bounds.top == 0.0);
        MIRAGE_CHECK(evidence.bounds.right == 640.0);
        MIRAGE_CHECK(evidence.bounds.bottom == 480.0);
        MIRAGE_CHECK(!evidence.space.is_nil());
        MIRAGE_CHECK(observation.perception[0].quality == mira::ComponentQuality::Good);
        MIRAGE_CHECK(observation.perception[0].provenance.source == "mirage.desktop.visual");
        MIRAGE_CHECK(observation.perception[0].provenance.method == "mirador-fusion");
    }
    // Screen was not requested: the frame is not claimed even though the
    // refresh captured one.
    MIRAGE_CHECK(!observation.screen.has_value());
    MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Good);
    MIRAGE_CHECK(observation.quality.degradations.empty());
    MIRAGE_CHECK(fixture.env->capture_calls == 1);
    MIRAGE_CHECK(fixture.ocr->calls() == 1);
    MIRAGE_CHECK(fixture.detector->calls() == 1);
    MIRAGE_CHECK(fixture.registry->size() == 1);

    // A second observation is a fresh capture with fresh evidence
    // identities.
    const mira::EvidenceId first_evidence_id =
        observation.perception.empty() ? mira::EvidenceId{} : observation.perception[0].value.id;
    const auto second = fixture.binding->observe(request, mira::make_control_context());
    MIRAGE_CHECK(second.has_value());
    if (second.has_value()) {
        MIRAGE_CHECK(second.value().id != observation.id);
        MIRAGE_CHECK(second.value().perception.size() == 1);
        if (second.value().perception.size() == 1) {
            MIRAGE_CHECK(second.value().perception[0].value.id != first_evidence_id);
        }
    }
}

void wired_screen_and_perception_share_one_refresh(executor::Executor &executor) {
    WiredVisualFixture fixture = make_wired_visual_fixture(executor);

    mira::ObservationRequest request;
    request.required.screen = true;
    request.required.perception = 1;
    const auto result = fixture.binding->observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    if (!result.has_value()) {
        std::fprintf(stderr, "observe failed: %s\n", result.error().safe_message.c_str());
        return;
    }
    const mira::Observation &observation = result.value();
    MIRAGE_CHECK(observation.screen.has_value());
    MIRAGE_CHECK(observation.perception.size() == 1);
    if (observation.screen.has_value() && observation.perception.size() == 1) {
        // One refresh drove both components: the perception bounds span the
        // delivered frame (display at the origin).
        const mira::ScreenFrameDescriptor &frame = observation.screen->value;
        const mira::PerceptionEvidence &evidence = observation.perception[0].value;
        MIRAGE_CHECK(frame.width_pixels == 640);
        MIRAGE_CHECK(frame.height_pixels == 480);
        MIRAGE_CHECK(evidence.bounds.left == 0.0);
        MIRAGE_CHECK(evidence.bounds.top == 0.0);
        MIRAGE_CHECK(evidence.bounds.right == static_cast<double>(frame.width_pixels));
        MIRAGE_CHECK(evidence.bounds.bottom == static_cast<double>(frame.height_pixels));
    }
    // Exactly one capture and one analysis served the whole request.
    MIRAGE_CHECK(fixture.env->capture_calls == 1);
    MIRAGE_CHECK(fixture.ocr->calls() == 1);
    MIRAGE_CHECK(fixture.detector->calls() == 1);
    MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Good);
    MIRAGE_CHECK(observation.quality.degradations.empty());
}

void wired_perception_above_declared_sources_is_refused(executor::Executor &executor) {
    WiredVisualFixture fixture = make_wired_visual_fixture(executor);

    mira::ObservationRequest request;
    request.required.perception = 2; // one source is declared
    const auto refused = fixture.binding->observe(request, mira::make_control_context());
    MIRAGE_CHECK(!refused.has_value());
    MIRAGE_CHECK(refused.error().code == mira::ErrorCode::UnsupportedCapability);
    MIRAGE_CHECK(refused.error().safe_message.find("perception") != std::string::npos);
    // The capability gate refused the request before any visual work.
    MIRAGE_CHECK(fixture.env->capture_calls == 0);
    MIRAGE_CHECK(fixture.ocr->calls() == 0);
    MIRAGE_CHECK(fixture.detector->calls() == 0);
}

void wired_screen_capture_failure_degrades_optional_and_fails_required(
    executor::Executor &executor) {
    WiredVisualFixture fixture = make_wired_visual_fixture(executor);
    fixture.env->failures.capture_error = true;

    // Optional screen: the observation succeeds with an explicit,
    // never-silent degradation.
    mira::ObservationRequest optional_request;
    optional_request.optional.screen = true;
    const auto degraded = fixture.binding->observe(optional_request, mira::make_control_context());
    MIRAGE_CHECK(degraded.has_value());
    if (degraded.has_value()) {
        const mira::Observation &observation = degraded.value();
        MIRAGE_CHECK(!observation.screen.has_value());
        MIRAGE_CHECK(observation.quality.screen_missing);
        MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Degraded);
        bool screen_note = false;
        for (const auto &note : observation.quality.degradations) {
            screen_note = screen_note || note.rfind("screen unavailable", 0) == 0;
        }
        MIRAGE_CHECK(screen_note);
    }

    // Required screen: the capture failure fails the whole request, mapped
    // onto the pinned error vocabulary.
    mira::ObservationRequest required_request;
    required_request.required.screen = true;
    const auto failed = fixture.binding->observe(required_request, mira::make_control_context());
    MIRAGE_CHECK(!failed.has_value());
    if (!failed.has_value()) {
        MIRAGE_CHECK(failed.error().code == mira::ErrorCode::PlatformError);
        MIRAGE_CHECK(failed.error().safe_message.find("screen") != std::string::npos);
        MIRAGE_CHECK(failed.error().safe_message.find("io_error") != std::string::npos);
    }
}

void wired_perception_analysis_failure_degrades_optional_and_fails_required(
    executor::Executor &executor) {
    // run_ocr enabled with no OCR backend wired: the session settles the
    // analysis with kBackendUnavailable, surfaced as "io_error".
    WiredVisualOptions options;
    options.with_backends = false;
    options.run_detector = false;
    WiredVisualFixture fixture = make_wired_visual_fixture(executor, options);

    // Optional perception: explicit degradation, never silence.
    mira::ObservationRequest optional_request;
    optional_request.optional.perception = 1;
    const auto degraded = fixture.binding->observe(optional_request, mira::make_control_context());
    MIRAGE_CHECK(degraded.has_value());
    if (degraded.has_value()) {
        const mira::Observation &observation = degraded.value();
        MIRAGE_CHECK(observation.perception.empty());
        MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Degraded);
        bool perception_note = false;
        for (const auto &note : observation.quality.degradations) {
            perception_note = perception_note || note.rfind("perception unavailable", 0) == 0;
        }
        MIRAGE_CHECK(perception_note);
    }

    // Required perception: the analysis failure fails the whole request.
    mira::ObservationRequest required_request;
    required_request.required.perception = 1;
    const auto failed = fixture.binding->observe(required_request, mira::make_control_context());
    MIRAGE_CHECK(!failed.has_value());
    if (!failed.has_value()) {
        MIRAGE_CHECK(failed.error().code == mira::ErrorCode::PlatformError);
        MIRAGE_CHECK(failed.error().safe_message.find("perception") != std::string::npos);
        MIRAGE_CHECK(failed.error().safe_message.find("io_error") != std::string::npos);
    }
}

void wired_perception_without_analysis_stages_yields_empty_evidence(executor::Executor &executor) {
    // Fusion-only refresh (both backend stages disabled): the pinned fusion
    // emits zero regions for zero evidence, so an optional perception request
    // (a minimum-count hint on the required surface) observes an empty but
    // healthy visual generation. The backend calls prove the disabled stages
    // really stayed dark.
    WiredVisualOptions options;
    options.run_ocr = false;
    options.run_detector = false;
    WiredVisualFixture fixture = make_wired_visual_fixture(executor, options);

    mira::ObservationRequest request;
    request.optional.perception = 1;
    const auto result = fixture.binding->observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    if (result.has_value()) {
        MIRAGE_CHECK(result.value().perception.empty());
        MIRAGE_CHECK(result.value().quality.overall == mira::ComponentQuality::Good);
        MIRAGE_CHECK(result.value().quality.degradations.empty());
    }
    MIRAGE_CHECK(fixture.env->capture_calls == 1);
    MIRAGE_CHECK(fixture.ocr->calls() == 0);
    MIRAGE_CHECK(fixture.detector->calls() == 0);

    // A declared required count is a real minimum: zero fused regions fail
    // the request instead of silently under-delivering.
    mira::ObservationRequest counted;
    counted.required.perception = 1;
    const auto short_fall = fixture.binding->observe(counted, mira::make_control_context());
    MIRAGE_CHECK(!short_fall.has_value());
    if (!short_fall.has_value()) {
        MIRAGE_CHECK(short_fall.error().code == mira::ErrorCode::NotFound);
        MIRAGE_CHECK(short_fall.error().safe_message.find("perception") != std::string::npos);
    }
}

void wired_required_screen_fails_when_the_store_budget_is_exhausted(executor::Executor &executor) {
    WiredVisualOptions options;
    options.store_bytes = 1024; // far below one 640x480 BGRA frame
    WiredVisualFixture fixture = make_wired_visual_fixture(executor, options);

    mira::ObservationRequest request;
    request.required.screen = true;
    const auto failed = fixture.binding->observe(request, mira::make_control_context());
    MIRAGE_CHECK(!failed.has_value());
    if (!failed.has_value()) {
        MIRAGE_CHECK(failed.error().code == mira::ErrorCode::ResourceExhausted);
    }
}

void wired_observation_without_visual_requests_stays_dark(executor::Executor &executor) {
    WiredVisualFixture fixture = make_wired_visual_fixture(executor);

    // A request without screen/perception components triggers no capture
    // and no analysis, even though the pipeline is live.
    mira::ObservationRequest request;
    const auto result = fixture.binding->observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    if (result.has_value()) {
        MIRAGE_CHECK(!result.value().screen.has_value());
        MIRAGE_CHECK(result.value().perception.empty());
        MIRAGE_CHECK(result.value().quality.overall == mira::ComponentQuality::Good);
        MIRAGE_CHECK(result.value().quality.degradations.empty());
    }
    MIRAGE_CHECK(fixture.env->capture_calls == 0);
    MIRAGE_CHECK(fixture.ocr->calls() == 0);
    MIRAGE_CHECK(fixture.detector->calls() == 0);
    MIRAGE_CHECK(fixture.registry->size() == 0);
    MIRAGE_CHECK(fixture.registry->current().scope_ref.empty());

    // An optional visual request over an unwired binding fails closed
    // instead of pretending the surface exists (defense behind the
    // capability gate, which only refuses required components).
    auto bare_environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    bare_environment->displays.push_back({"d1", {0, 0, 640, 480}, true});
    integration::MiraEnvironmentBinding bare_binding(bare_environment);
    mira::ObservationRequest optional_visual;
    optional_visual.optional.screen = true;
    const auto refused = bare_binding.observe(optional_visual, mira::make_control_context());
    MIRAGE_CHECK(!refused.has_value());
    MIRAGE_CHECK(refused.error().code == mira::ErrorCode::UnsupportedCapability);
    MIRAGE_CHECK(refused.error().safe_message.find("visual") != std::string::npos);
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[mira_binding_test] scenario: %s\n", name);
    scenario();
}

template <typename Scenario> void run_executor_scenario(const char *name, Scenario scenario) {
    std::fprintf(stderr, "[mira_binding_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("binding_identity_and_honest_capabilities",
                 scenario_binding_identity_and_honest_capabilities);
    run_scenario("observe_fails_closed_on_unsupported_components",
                 scenario_observe_fails_closed_on_unsupported_components);
    run_scenario("execute_rejects_and_interrupt_is_idempotent",
                 scenario_execute_rejects_and_interrupt_is_idempotent);
    run_scenario("filesystem_provider_reads_and_fails_closed",
                 scenario_filesystem_provider_reads_and_fails_closed);
    run_scenario("process_provider_captures_streams_and_exit_code",
                 scenario_process_provider_captures_streams_and_exit_code);
    run_scenario("process_provider_enforces_budget", scenario_process_provider_enforces_budget);
    run_scenario("operation_surface_rejects_invalid_identities",
                 scenario_operation_surface_rejects_invalid_identities);
    run_scenario("end_to_end_task_reads_file_and_executes_shell",
                 scenario_end_to_end_task_reads_file_and_executes_shell);
    run_scenario("operation_completion_does_not_revive_cancelled_task",
                 scenario_operation_completion_does_not_revive_cancelled_task);
    run_scenario("fake_capabilities_are_reported_honestly",
                 fake_capabilities_are_reported_honestly);
    run_scenario("fake_observe_delivers_structure_and_foreground",
                 fake_observe_delivers_structure_and_foreground);
    run_scenario("fake_observe_without_components_returns_minimal_observation",
                 fake_observe_without_components_returns_minimal_observation);
    run_scenario("fake_observe_fails_closed_when_required_structure_is_missing",
                 fake_observe_fails_closed_when_required_structure_is_missing);
    run_scenario("fake_observe_reports_not_found_without_any_window",
                 fake_observe_reports_not_found_without_any_window);
    run_scenario("fake_observe_maps_provider_errors_onto_the_pinned_vocabulary",
                 fake_observe_maps_provider_errors_onto_the_pinned_vocabulary);
    run_scenario("fake_observe_degrades_optional_structure",
                 fake_observe_degrades_optional_structure);
    run_scenario("fake_observe_refuses_required_screen_despite_screen_provider",
                 fake_observe_refuses_required_screen_despite_screen_provider);
    run_scenario("fake_observe_honours_operation_cancellation_and_deadlines",
                 fake_observe_honours_operation_cancellation_and_deadlines);

    // M3-05 wired visual surface: a real VisualObservationPipeline over the
    // fake environment's screen provider, its fake backends served by a real
    // Executor blocking worker (EXEC-01: the executor stays with this test's
    // main as its external owner).
    executor::Executor executor;
    const bool executor_ready = executor.initialize_ex(executor::ExecutorConfig{}).ok;
    MIRAGE_CHECK(executor_ready);
    if (executor_ready) {
        run_executor_scenario("wired_visual_capabilities_are_reported_honestly",
                              [&] { wired_visual_capabilities_are_reported_honestly(executor); });
        run_executor_scenario("wired_required_screen_delivers_a_validator_clean_frame", [&] {
            wired_required_screen_delivers_a_validator_clean_frame(executor);
        });
        run_executor_scenario("wired_required_perception_projects_published_regions", [&] {
            wired_required_perception_projects_published_regions(executor);
        });
        run_executor_scenario("wired_screen_and_perception_share_one_refresh",
                              [&] { wired_screen_and_perception_share_one_refresh(executor); });
        run_executor_scenario("wired_perception_above_declared_sources_is_refused", [&] {
            wired_perception_above_declared_sources_is_refused(executor);
        });
        run_executor_scenario(
            "wired_screen_capture_failure_degrades_optional_and_fails_required",
            [&] { wired_screen_capture_failure_degrades_optional_and_fails_required(executor); });
        run_executor_scenario(
            "wired_perception_analysis_failure_degrades_optional_and_fails_required", [&] {
                wired_perception_analysis_failure_degrades_optional_and_fails_required(executor);
            });
        run_executor_scenario(
            "wired_perception_without_analysis_stages_yields_empty_evidence",
            [&] { wired_perception_without_analysis_stages_yields_empty_evidence(executor); });
        run_executor_scenario(
            "wired_required_screen_fails_when_the_store_budget_is_exhausted",
            [&] { wired_required_screen_fails_when_the_store_budget_is_exhausted(executor); });
        run_executor_scenario("wired_observation_without_visual_requests_stays_dark", [&] {
            wired_observation_without_visual_requests_stays_dark(executor);
        });
        executor.shutdown(true);
    }
    return mirage::testing::finish("mira_binding_test");
}
