#include <mirage/integration/mira_environment_binding.hpp>

#include <mirage/desktop/observation_assembler.hpp>

#include <cmath>
#include <map>
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

/// The first analysis-stage failure of a refresh: publication refusal wins
/// over the raw analysis error because it carries the registry semantics
/// (e.g. snapshot_too_large) the caller must see.
desktop::ProviderError visual_analysis_error(const EnvironmentVisualRefresh &refresh) {
    if (!refresh.publish.error.code.empty()) {
        return refresh.publish.error;
    }
    return refresh.analysis_error;
}

/// Publishes the captured frame into the artifact store and builds the
/// pinned screen component (plan item `M3-05`). Pixels are the canonical
/// Bgra8 capture, published raw; the store record (media type, byte size,
/// digest) is the only wire metadata (pinned DEC-013) and every field the
/// pinned validator checks is filled before the component is returned.
mira::Result<mira::ObservationComponent<mira::ScreenFrameDescriptor>>
project_screen(const EnvironmentVisualRefresh &refresh,
               const std::map<std::string, mira::DisplayId> &display_ids,
               const mira::CaptureSpan &capture, mira::IArtifactStore &store) {
    mira::ArtifactWriteSpec spec;
    spec.media_type = "image/x-bgra8888";
    spec.max_bytes = refresh.frame.pixels.empty() ? std::size_t{1} : refresh.frame.pixels.size();
    auto writer = store.begin(spec);
    if (!writer) {
        return writer.error();
    }
    if (const auto written =
            writer.value().write(refresh.frame.pixels.data(), refresh.frame.pixels.size());
        !written) {
        return written.error();
    }
    const auto committed = store.commit(writer.value());
    if (!committed) {
        return committed.error();
    }

    mira::ScreenFrameDescriptor descriptor;
    descriptor.frame_id = mira::FrameId::generate();
    const auto display = display_ids.find(refresh.display_id);
    descriptor.display_id =
        display != display_ids.end() ? display->second : mira::DisplayId::generate();
    descriptor.width_pixels = static_cast<std::uint32_t>(refresh.frame.width);
    descriptor.height_pixels = static_cast<std::uint32_t>(refresh.frame.height);
    descriptor.pixel_format = mira::PixelFormat::BGRA8888;
    descriptor.color_space = mira::ColorSpace::SRGB;
    descriptor.alpha_mode = mira::AlphaMode::Opaque;
    descriptor.native_rotation = mira::Rotation::Rotation0;
    descriptor.planes.push_back(
        mira::PlaneLayout{0U, static_cast<std::uint32_t>(refresh.frame.stride), 4U,
                          descriptor.width_pixels, descriptor.height_pixels});
    descriptor.pixel_space = mira::CoordinateSpaceId::generate();
    descriptor.capture = capture;
    descriptor.payload_artifact = committed.value().id;
    descriptor.payload_media_type = committed.value().media_type;
    descriptor.payload_byte_size = committed.value().byte_size;
    descriptor.payload_digest = committed.value().digest;

    if (const auto validated = mira::validate_frame_descriptor(descriptor); !validated) {
        return validated.error();
    }

    mira::ObservationComponent<mira::ScreenFrameDescriptor> component;
    component.value = std::move(descriptor);
    component.capture = capture;
    component.quality = mira::ComponentQuality::Good;
    component.provenance.source = "mirage.desktop.screen";
    component.provenance.method = "screen-capture";
    component.environment_epoch = 0;
    return component;
}

/// Projects one published visual region onto the pinned perception evidence
/// vocabulary. Bounds stay global desktop coordinates (DEC-016 decision 3),
/// declared in one generated space per observation like the structure
/// projection's node space.
mira::PerceptionEvidence project_region(const mirage::desktop::VisualRegionEntry &region,
                                        mira::CoordinateSpaceId space) {
    mira::PerceptionEvidence evidence;
    evidence.id = mira::EvidenceId::generate();
    switch (region.source) {
    case mirage::desktop::VisualRegionSource::kOcr:
        evidence.kind = "ocr.text";
        break;
    case mirage::desktop::VisualRegionSource::kDetector:
        evidence.kind = "detector.box";
        break;
    case mirage::desktop::VisualRegionSource::kTemplate:
        evidence.kind = "template.icon";
        break;
    case mirage::desktop::VisualRegionSource::kGeometry:
        evidence.kind = "geometry.region";
        break;
    }
    // The agent-facing line forms of DEC-016 decision 2: OCR and detector
    // evidence name themselves through text, a template hit through its
    // enrolled identifier, geometry carries no label.
    evidence.label = region.source == mirage::desktop::VisualRegionSource::kTemplate
                         ? region.template_id
                         : region.text;
    evidence.bounds =
        mira::RectF{static_cast<double>(region.bounds.x), static_cast<double>(region.bounds.y),
                    static_cast<double>(region.bounds.x + region.bounds.width),
                    static_cast<double>(region.bounds.y + region.bounds.height)};
    evidence.space = space;
    return evidence;
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
    : MiraEnvironmentBinding(std::move(environment), VisualWiring{}) {}

MiraEnvironmentBinding::MiraEnvironmentBinding(
    std::shared_ptr<mirage::desktop::DesktopEnvironment> environment,
    const VisualWiring &visual_wiring)
    : environment_(std::move(environment)), visual_wiring_(visual_wiring) {
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
    // The visual surface is declared only when the whole cycle can be
    // honored: a started pipeline for capture/analysis/publication and an
    // artifact store for the pinned frame payload record. A pipeline without
    // a store could not deliver a validator-clean ScreenFrameDescriptor, so
    // it must not claim screen_capture.
    if (visual_wiring_.pipeline != nullptr && visual_wiring_.artifacts != nullptr &&
        visual_wiring_.pipeline->running() && environment->screen() != nullptr) {
        capabilities.screen_capture = true;
        capabilities.perception_sources = 1;
    }
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
    const bool need_screen = request.required.screen;
    const std::size_t need_perception = request.required.perception;
    const bool want_structure = need_structure || request.optional.structure;
    const bool want_foreground = need_foreground || request.optional.foreground;
    const bool want_screen = need_screen || request.optional.screen;
    const bool want_perception = need_perception > 0 || request.optional.perception > 0;

    // Visual refresh first (M3-05): capture, and — when perception evidence
    // is requested — analysis and registry publication. The binding holds no
    // CancelToken of its own (M2-06 precedent): cooperative cancellation is
    // probed at stage boundaries, and the analysis itself is bounded by the
    // operation deadline inside the pipeline.
    std::optional<EnvironmentVisualRefresh> visual;
    mira::Timestamp visual_started = started;
    mira::Timestamp visual_finished = started;
    if (want_screen || want_perception) {
        if (visual_wiring_.pipeline == nullptr) {
            // Unreachable for required components (the capability gate above
            // already refused them); an optional-only request must still not
            // pretend the visual surface exists.
            return pinned_error(mira::ErrorCode::UnsupportedCapability,
                                "observation requests the visual surface but no pipeline is wired");
        }
        visual_started = mira::Timestamp::now();
        visual = visual_wiring_.pipeline->refresh(want_perception, mirage::desktop::CancelToken{},
                                                  context.deadline);
        visual_finished = mira::Timestamp::now();
        if (visual->capture_cancelled || (want_perception && visual->analysis_cancelled)) {
            return pinned_error(mira::ErrorCode::Cancelled,
                                "observation was cancelled during the visual refresh");
        }
        if (context.cancelled()) {
            return pinned_error(mira::ErrorCode::Cancelled,
                                "observation was cancelled after the visual refresh");
        }
        // Required visual components must be delivered or the whole request
        // fails closed, like structure/foreground below.
        if (need_screen && !visual->captured) {
            return desktop_error(visual->capture_error, "screen");
        }
        if (need_perception > 0 && !visual->analyzed) {
            return desktop_error(visual_analysis_error(*visual), "perception");
        }
    }

    // The assembler's visual component reads the generation this refresh
    // just published. With no fresh publication (capture-only refresh or a
    // failed analysis) it stays unrequested, so a stale generation is never
    // presented as this observation's visual state.
    mirage::desktop::VisualReferenceRegistry *registry = nullptr;
    if (visual.has_value() && visual->analyzed && visual->publish.ok) {
        registry = &visual_wiring_.pipeline->registry();
    }
    mirage::desktop::ObservationAssembler assembler(*environment_, registry);
    mirage::desktop::ObservationComponents components;
    components.active_window = want_foreground;
    components.semantic_snapshot = want_structure;
    components.visual_snapshot = registry != nullptr;
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
    std::map<std::string, mira::DisplayId> display_ids;
    if (mirage::desktop::ScreenProvider *screen = environment_->screen(); screen != nullptr) {
        const auto displays = screen->list_displays();
        if (displays.ok && !displays.displays.empty()) {
            std::vector<mira::DisplayInfo> pinned_displays;
            pinned_displays.reserve(displays.displays.size());
            for (const auto &display : displays.displays) {
                mira::DisplayInfo pinned_display;
                pinned_display.id = mira::DisplayId::generate();
                display_ids[display.id] = pinned_display.id;
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

    // Screen component (M3-05): publish the captured frame into the artifact
    // store and deliver the validator-clean descriptor. Required screen
    // already failed closed right after the refresh; here a projection or
    // publication failure of an optional request degrades the observation.
    if (visual.has_value() && visual->captured && want_screen) {
        auto screen_component = project_screen(
            *visual, display_ids, span_between(visual_started, visual_finished, clock_domain_),
            *visual_wiring_.artifacts);
        if (screen_component.has_value()) {
            observation.screen = std::move(screen_component.value());
        } else if (need_screen) {
            // The capture succeeded but the payload publication did not: a
            // required screen component is still undeliverable.
            return screen_component.error();
        } else {
            observation.quality.screen_missing = true;
            degradations.push_back("screen unavailable: " + screen_component.error().safe_message);
        }
    } else if (want_screen) {
        observation.quality.screen_missing = true;
        degradations.push_back("screen unavailable: " + (visual.has_value()
                                                             ? visual->capture_error.code
                                                             : std::string("not_wired")));
    }

    // Perception evidence (M3-05): the published visual regions projected
    // onto the pinned evidence vocabulary. The assembler's visual component
    // was requested exactly when this refresh published a fresh generation,
    // so the entries never outlive their own observation.
    if (want_perception && assembly.visual_snapshot.captured) {
        const mira::CaptureSpan visual_span =
            span_between(visual_started, visual_finished, clock_domain_);
        const auto evidence_space = mira::CoordinateSpaceId::generate();
        observation.perception.reserve(assembly.observation.visual_snapshot.regions.size());
        for (const auto &region : assembly.observation.visual_snapshot.regions) {
            mira::ObservationComponent<mira::PerceptionEvidence> component;
            component.value = project_region(region, evidence_space);
            component.capture = visual_span;
            component.quality = mira::ComponentQuality::Good;
            component.provenance.source = "mirage.desktop.visual";
            component.provenance.method = "mirador-fusion";
            component.environment_epoch = 0;
            observation.perception.push_back(std::move(component));
        }
        if (observation.perception.size() < need_perception) {
            // The pinned request semantics are a minimum evidence count;
            // fewer entries means the requirement is unmet, never a silent
            // shortfall.
            return pinned_error(mira::ErrorCode::NotFound,
                                "perception: visual analysis produced " +
                                    std::to_string(observation.perception.size()) +
                                    " evidence entries, " + std::to_string(need_perception) +
                                    " required");
        }
    } else if (want_perception) {
        degradations.push_back(
            "perception unavailable: " +
            (visual.has_value() ? visual_analysis_error(*visual).code : std::string("not_wired")));
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
