#include <mirage/integration/mira_environment_binding.hpp>

#include <mirage/desktop/observation_assembler.hpp>

#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace mirage::integration {
namespace {

mira::Error pinned_error(mira::ErrorCode code, std::string message) {
    mira::Error error;
    error.code = code;
    error.safe_message = std::move(message);
    return error;
}

/// Translates one stable desktop ProviderError onto the pinned error
/// vocabulary. Unmapped codes stay PlatformError — never dropped, never
/// guessed into success-shaped outcomes.
mira::Error desktop_error(const mirage::desktop::ProviderError &error, const char *component) {
    mira::ErrorCode code = mira::ErrorCode::PlatformError;
    if (error.code == "cancelled") {
        code = mira::ErrorCode::Cancelled;
    } else if (error.code == "deadline_exceeded") {
        code = mira::ErrorCode::DeadlineExceeded;
    } else if (error.code == "not_found") {
        code = mira::ErrorCode::NotFound;
    } else if (error.code == "invalid_argument") {
        code = mira::ErrorCode::InvalidArgument;
    } else if (error.code == "permission_denied") {
        code = mira::ErrorCode::PermissionDenied;
    } else if (error.code == "already_running") {
        code = mira::ErrorCode::AlreadyExists;
    } else if (error.code == "unsupported_platform" || error.code == "unsupported_window" ||
               error.code == "unsupported_content" || error.code == "unsupported_element" ||
               error.code == "unsupported_hint") {
        code = mira::ErrorCode::UnsupportedCapability;
    } else if (error.code.size() > 9 && error.code.substr(error.code.size() - 9) == "too_large") {
        code = mira::ErrorCode::ResourceExhausted;
    }
    return pinned_error(code, std::string(component) + ": " + error.code + ": " + error.message);
}

mira::CaptureSpan span_between(const mira::Timestamp &begin, const mira::Timestamp &end,
                               mira::ClockDomainId domain) {
    mira::CaptureSpan span;
    span.clock_domain = domain;
    span.normalized_begin = begin;
    span.normalized_end = end;
    span.sync_quality = mira::ClockSyncQuality::Synced;
    return span;
}

/// Projects the frozen mirage role vocabulary onto the pinned coarse role
/// enum. The projection is best-effort context, never identity: addressing
/// goes through the @eN refs (kept in StableNodeHint) on the mirage provider
/// surface, and the semantic snapshot's own roles stay authoritative there.
mira::UiRole ui_role_of(const std::string &role) {
    using mira::UiRole;
    if (role == "button") {
        return UiRole::Button;
    }
    if (role == "checkbox") {
        return UiRole::CheckBox;
    }
    if (role == "switch" || role == "toggle") {
        return UiRole::Switch;
    }
    if (role == "text" || role == "label") {
        return UiRole::Text;
    }
    if (role == "entry" || role == "editor") {
        return UiRole::TextField;
    }
    if (role == "menu") {
        return UiRole::Menu;
    }
    if (role == "menuitem" || role == "check-menuitem" || role == "radio-menuitem") {
        return UiRole::MenuItem;
    }
    if (role == "menubar") {
        return UiRole::AppBar;
    }
    if (role == "tree" || role == "list") {
        return UiRole::List;
    }
    if (role == "treeitem" || role == "listitem") {
        return UiRole::ListItem;
    }
    if (role == "table" || role == "grid") {
        return UiRole::Grid;
    }
    if (role == "pagetab") {
        return UiRole::Tab;
    }
    if (role == "dialog") {
        return UiRole::Dialog;
    }
    if (role == "window" || role == "frame") {
        return UiRole::Window;
    }
    if (role == "panel" || role == "scrollpane") {
        return UiRole::Pane;
    }
    if (role == "scrollbar" || role == "slider") {
        return UiRole::Slider;
    }
    if (role == "image" || role == "icon") {
        return UiRole::Image;
    }
    if (role == "root" || role == "application") {
        return UiRole::Root;
    }
    return UiRole::Custom;
}

/// Projects a mirage SemanticSnapshot onto the pinned UiTreeSnapshot:
/// fresh node ids, parent indices resolved to ids, @eN refs preserved as
/// stable hints, screen-space bounds verbatim. `complete` reports a full
/// window capture (the accessibility provider delivers the whole subtree or
/// fails); an empty-but-valid snapshot stays incomplete so the pinned
/// validator keeps rejecting it as an authoritative answer. Returns an error
/// when the projection would violate the pinned snapshot contract (defensive:
/// the mapping produces valid snapshots by construction).
mira::Result<mira::UiTreeSnapshot>
project_structure(const mirage::desktop::SemanticSnapshot &snapshot,
                  const mira::CaptureSpan &capture) {
    mira::UiTreeSnapshot projected;
    projected.space = mira::CoordinateSpaceId::generate();
    projected.capture = capture;
    projected.truncated = false;
    projected.visible_only = false;
    projected.complete = !snapshot.nodes.empty();

    projected.nodes.reserve(snapshot.nodes.size());
    std::vector<mira::UiNodeId> ids;
    ids.reserve(snapshot.nodes.size());
    for (std::size_t index = 0; index < snapshot.nodes.size(); ++index) {
        ids.push_back(mira::UiNodeId::generate());
    }

    std::uint32_t max_depth = 0;
    for (std::size_t index = 0; index < snapshot.nodes.size(); ++index) {
        const auto &node = snapshot.nodes[index];
        mira::UiNode projected_node;
        projected_node.id = ids[index];
        if (node.parent != mirage::desktop::kNoParent && node.parent < ids.size()) {
            projected_node.parent = ids[node.parent];
        }
        projected_node.role = ui_role_of(node.role);
        projected_node.text = node.name;
        projected_node.content_description = node.description;
        projected_node.bounds =
            mira::RectF{static_cast<double>(node.geometry.x), static_cast<double>(node.geometry.y),
                        static_cast<double>(node.geometry.x + node.geometry.width),
                        static_cast<double>(node.geometry.y + node.geometry.height)};
        projected_node.space = projected.space;
        if (node.enabled) {
            projected_node.state = projected_node.state | mira::UiNodeState::Enabled;
        }
        if (node.focused) {
            projected_node.state = projected_node.state | mira::UiNodeState::Focused;
        }
        projected_node.stable_hint = mira::StableNodeHint{node.ref};
        projected_node.provenance.source = "mirage.desktop.accessibility";
        projected_node.provenance.method = "accessibility";
        projected.nodes.push_back(std::move(projected_node));

        std::uint32_t depth = 1;
        std::optional<std::size_t> cursor = node.parent;
        while (cursor.has_value() && cursor.value() != mirage::desktop::kNoParent) {
            ++depth;
            if (cursor.value() >= snapshot.nodes.size()) {
                break;
            }
            const auto parent = snapshot.nodes[cursor.value()].parent;
            cursor = parent == mirage::desktop::kNoParent ? std::nullopt
                                                          : std::optional<std::size_t>(parent);
        }
        max_depth = std::max(max_depth, depth);
    }
    projected.max_depth_reached = max_depth;

    if (const auto validated = mira::validate_ui_tree_snapshot(projected); !validated) {
        return validated.error();
    }
    return projected;
}

} // namespace

MiraEnvironmentBinding::MiraEnvironmentBinding(
    std::shared_ptr<mirage::desktop::DesktopEnvironment> environment)
    : environment_(std::move(environment)) {
    if (!environment_) {
        throw std::invalid_argument("MiraEnvironmentBinding requires a desktop environment");
    }
    name_ = "mirage.desktop." + environment_->info().platform + "-v1";
    clock_domain_ = mira::ClockDomainId::generate();
}

const char *MiraEnvironmentBinding::binding_name() const { return name_.c_str(); }

mira::EnvironmentCapabilities MiraEnvironmentBinding::capabilities() const {
    // Declared exactly from what the bound environment can deliver on every
    // request (pinned contract: adapters must not declare a capability they
    // cannot honor, and Core never guesses from platform names).
    mira::EnvironmentCapabilities capabilities;
    auto *environment = environment_.get();
    const bool has_window = environment->window() != nullptr;
    capabilities.foreground_app = has_window;
    capabilities.ui_tree = has_window && environment->accessibility() != nullptr;
    return capabilities;
}

mira::Result<mira::Observation>
MiraEnvironmentBinding::observe(const mira::ObservationRequest &request,
                                const mira::OperationContext &context) {
    if (const auto validated = mira::validate_observation_request(request); !validated) {
        return validated.error();
    }
    if (context.cancelled()) {
        return pinned_error(mira::ErrorCode::Cancelled, "observation was cancelled before capture");
    }
    const auto capabilities = this->capabilities();
    const auto unsupported = mira::unsupported_required_components(capabilities, request);
    if (!unsupported.empty()) {
        std::string message = "observation requires unsupported components:";
        for (const auto &component : unsupported) {
            message += " " + component;
        }
        return pinned_error(mira::ErrorCode::UnsupportedCapability, std::move(message));
    }

    mira::Observation observation;
    observation.id = mira::ObservationId::generate();
    observation.session_id = context.session;
    observation.environment_epoch = 0;
    observation.atomicity = mira::ObservationAtomicity::NonAtomic;

    const mira::Timestamp started = mira::Timestamp::now();

    const bool need_structure = request.required.structure;
    const bool need_foreground = request.required.foreground;
    const bool want_structure = need_structure || request.optional.structure;
    const bool want_foreground = need_foreground || request.optional.foreground;

    mirage::desktop::ObservationAssembler assembler(*environment_);
    mirage::desktop::ObservationComponents components;
    components.active_window = want_foreground;
    components.semantic_snapshot = want_structure;
    const auto assembly = assembler.assemble(
        components, mirage::desktop::ObservationAssemblyLimits{}, mirage::desktop::CancelToken{});
    const mira::Timestamp finished = mira::Timestamp::now();

    if (assembly.cancelled) {
        return pinned_error(mira::ErrorCode::Cancelled, "observation was cancelled mid-capture");
    }
    // Required components must be delivered or the whole request fails
    // closed; failed optional components are recorded as degradations.
    if (need_structure && !assembly.semantic_snapshot.captured) {
        return desktop_error(assembly.semantic_snapshot.error, "structure");
    }
    if (need_foreground && !assembly.active_window.captured) {
        return desktop_error(assembly.active_window.error, "foreground");
    }

    const auto observation_span = span_between(started, finished, clock_domain_);
    observation.aggregate_span = observation_span;

    std::vector<std::string> degradations;
    if (assembly.ok) {
        observation.quality.overall = mira::ComponentQuality::Good;
    } else {
        observation.quality.overall = mira::ComponentQuality::Degraded;
    }

    // Best-effort display topology from the capture frontend; its absence is
    // a degradation note, not a failure (topology is not a requestable
    // component). The pinned cancellation probe is observed at this boundary:
    // the binding holds no mirage CancelToken of its own, so cooperative
    // cancellation is checked between captures, never mid-provider.
    if (context.cancelled_or_expired(finished)) {
        return pinned_error(context.expired(finished) ? mira::ErrorCode::DeadlineExceeded
                                                      : mira::ErrorCode::Cancelled,
                            "observation was cancelled or expired after capture");
    }
    if (mirage::desktop::ScreenProvider *screen = environment_->screen(); screen != nullptr) {
        const auto displays = screen->list_displays();
        if (displays.ok && !displays.displays.empty()) {
            std::vector<mira::DisplayInfo> pinned_displays;
            pinned_displays.reserve(displays.displays.size());
            for (const auto &display : displays.displays) {
                mira::DisplayInfo pinned_display;
                pinned_display.id = mira::DisplayId::generate();
                pinned_display.name = display.id;
                pinned_display.native_width_pixels =
                    static_cast<std::uint32_t>(display.geometry.width);
                pinned_display.native_height_pixels =
                    static_cast<std::uint32_t>(display.geometry.height);
                pinned_display.native_rotation = mira::Rotation::Rotation0;
                pinned_display.density_scale = 1.0;
                pinned_display.logical_width = static_cast<double>(display.geometry.width);
                pinned_display.logical_height = static_cast<double>(display.geometry.height);
                pinned_display.active = true;
                pinned_displays.push_back(std::move(pinned_display));
            }
            if (auto topology = mira::make_display_topology(0, std::move(pinned_displays));
                topology) {
                observation.topology = topology.value();
            } else {
                degradations.push_back("topology unavailable: " + topology.error().safe_message);
            }
        } else if (!displays.ok) {
            degradations.push_back("topology unavailable: " + displays.error.code);
        }
    }

    if (assembly.semantic_snapshot.captured) {
        auto structure =
            project_structure(assembly.observation.semantic_snapshot, observation_span);
        if (!structure) {
            return structure.error();
        }
        mira::ObservationComponent<mira::UiTreeSnapshot> component;
        component.value = std::move(structure.value());
        component.capture = observation_span;
        component.quality = mira::ComponentQuality::Good;
        component.provenance.source = "mirage.desktop.accessibility";
        component.provenance.method = "accessibility";
        component.environment_epoch = 0;
        observation.structure = std::move(component);
    } else if (want_structure) {
        degradations.push_back("structure unavailable: " + assembly.semantic_snapshot.error.code);
    }

    if (assembly.active_window.captured) {
        mira::ObservationComponent<mira::AppContext> component;
        // The accessibility application root is the honest desktop analog of
        // a package name; it is only known when the structure component was
        // captured in this observation.
        component.value.package_name = assembly.observation.active_application;
        component.value.activity_name = assembly.observation.active_window;
        component.capture = observation_span;
        component.quality = mira::ComponentQuality::Good;
        component.provenance.source = "mirage.desktop.window";
        component.provenance.method = "window-management";
        component.environment_epoch = 0;
        observation.foreground = std::move(component);
    } else if (want_foreground) {
        degradations.push_back("foreground unavailable: " + assembly.active_window.error.code);
    }

    if (!degradations.empty()) {
        observation.quality.overall = mira::ComponentQuality::Degraded;
        observation.quality.degradations = std::move(degradations);
    }
    return observation;
}

mira::Result<mira::ExecutionReceipt>
MiraEnvironmentBinding::execute(const mira::InputSequence &input,
                                const mira::OperationContext &context) {
    (void)input;
    mira::ExecutionReceipt receipt;
    // Refuse before any side effect: input dispatch is not mapped through
    // this binding (capabilities().discrete_input is false), desktop actions
    // run through the harness-side provider surface under the permission
    // gate (DEC-008, RULE-05).
    receipt.status = mira::ExecutionStatus::Rejected;
    receipt.side_effect_may_have_occurred = false;
    receipt.environment_epoch = 0;
    receipt.safe_message =
        "input dispatch is not available through the desktop environment binding";
    if (context.cancelled()) {
        receipt.safe_message = "input dispatch was cancelled before dispatch";
    }
    return receipt;
}

mira::Result<void> MiraEnvironmentBinding::interrupt(const mira::OperationContext &context) {
    (void)context;
    // Best-effort release: no input is dispatched through this binding, so
    // there is no in-flight platform input to release, and serialized Xlib
    // work cannot be unblocked from another thread. Idempotent success.
    return {};
}

} // namespace mirage::integration
