// M3-05 visual observation end-to-end test (DEC-016, M3 exit condition):
// the full wired loop runs against real platform frontends — the M2-06
// topology (private D-Bus session hosting the wire-exact AT-SPI2 fixture
// desktop plus a real Xvfb window) extended by the M3-05 visual surface. A
// VisualObservationPipeline (deterministic fake backends on a real Executor
// blocking worker, RULE-08: no statements about real model quality) is
// wired into MiraEnvironmentBinding together with a MemoryArtifactStore, so
// one observe request closes the whole chain: display capture ->
// ScreenFrameDescriptor with a store-recorded payload -> session analysis ->
// registry publication -> perception evidence in global desktop coordinates
// -> "@v1" click injection at the fused region's center -> re-observe with
// a fresh generation -> fail-closed and degrade paths after the pipeline
// stops.

#include "../support/atspi_session.hpp"
#include "../support/fake_atspi_desktop.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"

#include <mira/artifact_store.hpp>

#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/element_target_executor.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <executor/executor.hpp>

#include <mirador/pixel_format.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <X11/Xlib.h>

namespace {

namespace desktop = mirage::desktop;
namespace linux_backend = mirage::platform::linux_backend;
using linux_backend::LinuxDesktopEnvironment;
using mirage::integration::FakeDetectorBackend;
using mirage::integration::FakeDetectorConfig;
using mirage::integration::FakeOcrBackend;
using mirage::integration::FakeOcrConfig;
using mirage::integration::VisualObservationPipeline;
using mirage::integration::VisualObservationPipelineConfig;
using mirage::testing::FakeAtspiDesktop;

constexpr const char *kWindowTitle = "FakeWindow — main";
constexpr const char *kApplicationName = "FakeEditor";

/// Seeds the environment's Executor-backed visual wiring: fake OCR /
/// detection backends (identity semantics, accepted format kBgra8), a
/// MemoryArtifactStore, a started VisualObservationPipeline over the
/// environment's real X11 screen provider, and the shared registry. The
/// executor reference must outlive the returned pipeline.
VisualObservationPipelineConfig wired_pipeline_config(FakeOcrBackend &ocr,
                                                      FakeDetectorBackend &detector) {
    VisualObservationPipelineConfig config;
    config.session.ocr_backend = &ocr;
    config.session.detector_backend = &detector;
    config.run_ocr = true;
    config.run_detector = true;
    // Empty display id: the pipeline captures the primary display (the one
    // Xvfb screen).
    config.display_id = "";
    return config;
}

bool is_nonzero_digest(const mira::Sha256Digest &digest) {
    for (const std::uint8_t byte : digest.bytes) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

/// The primary (here: only) display geometry as the environment reports it.
bool primary_display_geometry(LinuxDesktopEnvironment &environment, desktop::DisplayInfo &out) {
    const auto displays = environment.screen()->list_displays();
    if (!displays.ok || displays.displays.empty()) {
        return false;
    }
    for (const auto &display : displays.displays) {
        if (display.primary) {
            out = display;
            return true;
        }
    }
    out = displays.displays.front();
    return true;
}

} // namespace

int main() {
    try {
        mirage::testing::AtspiSession session;

        // Same fixture tree as the M2-06 observation e2e: frame -> panel ->
        // {button, entry}; the entry carries the Focused state, the button
        // exposes a semantic action.
        FakeAtspiDesktop fixture;
        fixture.nodes.push_back({"/org/a11y/atspi/accessible/root", ATSPI_ROLE_DESKTOP_FRAME,
                                 "test-desktop", "", -1, false, false, "", false});
        fixture.nodes.push_back({"/org/fake/root", ATSPI_ROLE_APPLICATION, kApplicationName, "", 0,
                                 false, false, "", false});
        fixture.nodes.push_back({"/org/fake/root/window", ATSPI_ROLE_FRAME, kWindowTitle, "", 1,
                                 false, false, "", false});
        fixture.nodes.push_back({"/org/fake/root/window/pane", ATSPI_ROLE_PANEL, "main", "", 2,
                                 false, false, "", false});
        fixture.nodes.push_back({"/org/fake/root/window/pane/run", ATSPI_ROLE_PUSH_BUTTON, "Run",
                                 "Run button", 3, true, false, "", false});
        fixture.nodes.push_back({"/org/fake/root/window/pane/name", ATSPI_ROLE_ENTRY, "Name",
                                 "Name field", 3, false, true, "hello", /*focused=*/true});
        if (!fixture.start(session.bus_address())) {
            return 1;
        }

        const std::string xvfb = mirage::testing::find_xvfb();
        if (xvfb.empty()) {
            std::fprintf(stderr, "Xvfb not found: install xvfb or set MIRAGE_XVFB\n");
            return 1;
        }
        mirage::testing::XvfbDisplay server(xvfb); // 640x480x24 screen
        Display *xdisplay = XOpenDisplay(server.display_name().c_str());
        if (xdisplay == nullptr) {
            std::fprintf(stderr, "cannot open Xvfb display %s\n", server.display_name().c_str());
            return 1;
        }
        const Window xwindow =
            XCreateSimpleWindow(xdisplay, DefaultRootWindow(xdisplay), 10, 10, 200, 150, 0, 0, 0);
        XStoreName(xdisplay, xwindow, kWindowTitle);
        XMapWindow(xdisplay, xwindow);
        XSync(xdisplay, False);

        auto environment = std::make_shared<LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{},
            linux_backend::X11Options{true, server.display_name()},
            linux_backend::AtspiOptions{true});
        if (environment->accessibility() == nullptr || environment->window() == nullptr ||
            environment->screen() == nullptr || environment->input() == nullptr) {
            std::fprintf(stderr, "backend failed to initialize\n");
            XCloseDisplay(xdisplay);
            return 1;
        }

        // Focus the fixture window through the environment's own activation
        // path (M2-06 semantics: no WM runs on Xvfb, activation takes the
        // input-focus fallback).
        std::string window_id;
        for (const auto &window : environment->window()->list_windows().windows) {
            if (window.title == kWindowTitle) {
                window_id = window.id;
            }
        }
        if (window_id.empty() || !environment->window()->activate(window_id).ok ||
            !environment->window()->front_window().found) {
            std::fprintf(stderr, "could not focus the fixture window\n");
            XCloseDisplay(xdisplay);
            return 1;
        }

        desktop::DisplayInfo display = {};
        if (!primary_display_geometry(*environment, display)) {
            std::fprintf(stderr, "X11 screen provider reported no display\n");
            XCloseDisplay(xdisplay);
            return 1;
        }

        // ---- the M3-05 wiring: executor, fake backends, store, pipeline ----
        executor::Executor executor;
        const bool executor_ready = executor.initialize_ex(executor::ExecutorConfig{}).ok;
        MIRAGE_CHECK(executor_ready);
        if (!executor_ready) {
            XCloseDisplay(xdisplay);
            return 1;
        }
        FakeOcrConfig ocr_config;
        ocr_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
        FakeOcrBackend ocr(ocr_config);
        FakeDetectorConfig detector_config;
        detector_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
        FakeDetectorBackend detector(detector_config);
        mira::MemoryArtifactStore store;
        desktop::VisualReferenceRegistry registry;
        VisualObservationPipeline pipeline(executor, *environment->screen(), registry,
                                           wired_pipeline_config(ocr, detector));
        std::string start_error;
        MIRAGE_CHECK(pipeline.start(start_error));
        MIRAGE_CHECK(pipeline.running());

        mirage::integration::MiraEnvironmentBinding::VisualWiring wiring;
        wiring.pipeline = &pipeline;
        wiring.artifacts = &store;
        mirage::integration::MiraEnvironmentBinding binding(environment, wiring);

        // ---- capabilities: the visual surface joins the M2-06 surface ----
        const auto capabilities = binding.capabilities();
        MIRAGE_CHECK(capabilities.screen_capture);
        MIRAGE_CHECK(capabilities.ui_tree);
        MIRAGE_CHECK(capabilities.foreground_app);
        MIRAGE_CHECK(capabilities.perception_sources == 1);

        // ---- one observe closes capture -> payload -> analysis -> evidence --
        mira::ObservationRequest request;
        request.required.screen = true;
        request.required.structure = true;
        request.required.foreground = true;
        request.required.perception = 1;
        const auto first = binding.observe(request, mira::make_control_context());
        MIRAGE_CHECK(first.has_value());
        if (!first.has_value()) {
            std::fprintf(stderr, "observe failed: %s\n",
                         first.error().safe_message.c_str()); // loud, then stop
            pipeline.stop();
            executor.shutdown(true);
            XCloseDisplay(xdisplay);
            return 1;
        }
        const mira::Observation &observation = first.value();

        MIRAGE_CHECK(!observation.id.is_nil());
        MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Good);
        MIRAGE_CHECK(observation.quality.degradations.empty());

        // Screen component: validator-clean, matching the Xvfb display
        // geometry, with a payload that reopens from the wired store.
        MIRAGE_CHECK(observation.screen.has_value());
        if (observation.screen.has_value()) {
            const mira::ScreenFrameDescriptor &frame = observation.screen->value;
            MIRAGE_CHECK(mira::validate_frame_descriptor(frame).has_value());
            MIRAGE_CHECK(frame.width_pixels == static_cast<std::uint32_t>(display.geometry.width));
            MIRAGE_CHECK(frame.height_pixels ==
                         static_cast<std::uint32_t>(display.geometry.height));
            MIRAGE_CHECK(frame.pixel_format == mira::PixelFormat::BGRA8888);
            MIRAGE_CHECK(frame.payload_media_type == "image/x-bgra8888");
            MIRAGE_CHECK(frame.payload_byte_size ==
                         static_cast<std::uint64_t>(frame.width_pixels) * 4U * frame.height_pixels);
            MIRAGE_CHECK(is_nonzero_digest(frame.payload_digest));
            MIRAGE_CHECK(observation.topology.displays.size() == 1);
            if (observation.topology.displays.size() == 1) {
                MIRAGE_CHECK(frame.display_id == observation.topology.displays[0].id);
            }
            mira::ArtifactDescriptor record;
            record.id = frame.payload_artifact;
            record.digest = frame.payload_digest;
            record.byte_size = frame.payload_byte_size;
            record.media_type = frame.payload_media_type;
            const auto reopened = store.open(record);
            MIRAGE_CHECK(reopened.has_value());
            if (reopened.has_value()) {
                MIRAGE_CHECK(reopened.value().size() == frame.payload_byte_size);
            }
        }

        // Perception evidence: the fused full-view region in global desktop
        // coordinates (the fake fusion merges the OCR and detection evidence
        // of the whole frame into one region, translated onto the display).
        MIRAGE_CHECK(observation.perception.size() >= 1);
        if (!observation.perception.empty()) {
            const mira::PerceptionEvidence &evidence = observation.perception[0].value;
            MIRAGE_CHECK(evidence.kind == "ocr.text");
            MIRAGE_CHECK(evidence.label == "mirage-fake");
            MIRAGE_CHECK(evidence.bounds.left >= static_cast<double>(display.geometry.x));
            MIRAGE_CHECK(evidence.bounds.top >= static_cast<double>(display.geometry.y));
            MIRAGE_CHECK(evidence.bounds.right <=
                         static_cast<double>(display.geometry.x + display.geometry.width));
            MIRAGE_CHECK(evidence.bounds.bottom <=
                         static_cast<double>(display.geometry.y + display.geometry.height));
            MIRAGE_CHECK(!evidence.space.is_nil());
            MIRAGE_CHECK(observation.perception[0].provenance.source == "mirage.desktop.visual");
            MIRAGE_CHECK(observation.perception[0].provenance.method == "mirador-fusion");
        }

        // Structure and foreground keep their M2-06 semantics.
        MIRAGE_CHECK(observation.structure.has_value());
        if (observation.structure.has_value()) {
            const mira::UiTreeSnapshot &structure = observation.structure->value;
            MIRAGE_CHECK(mira::validate_ui_tree_snapshot(structure).has_value());
            MIRAGE_CHECK(structure.nodes.size() == 4);
            MIRAGE_CHECK(structure.complete);
            std::set<std::string> hints;
            for (std::size_t i = 0; i < structure.nodes.size(); ++i) {
                MIRAGE_CHECK(structure.nodes[i].stable_hint.has_value());
                MIRAGE_CHECK(hints.insert(structure.nodes[i].stable_hint->hint).second);
                MIRAGE_CHECK(structure.nodes[i].space == structure.space);
            }
            MIRAGE_CHECK(structure.nodes[0].stable_hint->hint == "@e1");
            MIRAGE_CHECK(structure.nodes[0].role == mira::UiRole::Window);
            MIRAGE_CHECK(structure.nodes[0].text == kWindowTitle);
            MIRAGE_CHECK(structure.nodes[2].stable_hint->hint == "@e3");
            MIRAGE_CHECK(structure.nodes[2].role == mira::UiRole::Button);
            MIRAGE_CHECK(structure.nodes[2].text == "Run");
            MIRAGE_CHECK(structure.nodes[3].stable_hint->hint == "@e4");
            MIRAGE_CHECK(structure.nodes[3].role == mira::UiRole::Text);
            for (std::size_t i = 0; i < structure.nodes.size(); ++i) {
                MIRAGE_CHECK(mira::has_state(structure.nodes[i].state,
                                             mira::UiNodeState::Focused) == (i == 3));
            }
        }
        MIRAGE_CHECK(observation.foreground.has_value());
        if (observation.foreground.has_value()) {
            MIRAGE_CHECK(observation.foreground->value.package_name == kApplicationName);
            MIRAGE_CHECK(observation.foreground->value.activity_name == kWindowTitle);
        }

        // The pinned evaluation accepts the observation: every required
        // component present, fresh, and within the declared skew bound.
        const mira::ObservationEvaluation evaluation =
            mira::evaluate_observation(observation, request, mira::Timestamp::now());
        MIRAGE_CHECK(evaluation.satisfies_request);

        // ---- "@v1" closes the loop into an injected pointer click --------
        const desktop::VisualSnapshot active_before = registry.current();
        MIRAGE_CHECK(active_before.scope_ref == "@vs1");
        MIRAGE_CHECK(active_before.regions.size() == 1);
        MIRAGE_CHECK(active_before.regions[0].ref == "@v1");

        desktop::ElementTargetExecutor actions(environment->accessibility(), &pipeline.registry(),
                                               environment->input());
        desktop::ElementTarget target;
        target.reference.id = "@v1";
        const desktop::TargetResolution click = actions.click(target, nullptr);
        MIRAGE_CHECK(click.ok);
        MIRAGE_CHECK(click.ring == desktop::ResolutionRing::kVisualReference);
        if (click.ok) {
            // The click lands at the fused region's center.
            const desktop::VisualRegionEntry &region = active_before.regions[0];
            const std::int32_t center_x = region.bounds.x + region.bounds.width / 2;
            const std::int32_t center_y = region.bounds.y + region.bounds.height / 2;
            MIRAGE_CHECK(click.point.x == center_x);
            MIRAGE_CHECK(click.point.y == center_y);
            // The injection really moved the server-side pointer: verify
            // through this test's independent X connection.
            Window root_return = None;
            Window child_return = None;
            int root_x = 0;
            int root_y = 0;
            int win_x = 0;
            int win_y = 0;
            unsigned int mask = 0;
            const bool queried =
                XQueryPointer(xdisplay, DefaultRootWindow(xdisplay), &root_return, &child_return,
                              &root_x, &root_y, &win_x, &win_y, &mask) != 0;
            MIRAGE_CHECK(queried);
            if (queried) {
                MIRAGE_CHECK(root_x == center_x);
                MIRAGE_CHECK(root_y == center_y);
            }
        }

        // ---- re-observe: a fresh observation is a fresh generation -------
        const auto second = binding.observe(request, mira::make_control_context());
        MIRAGE_CHECK(second.has_value());
        if (second.has_value()) {
            const mira::Observation &refreshed_observation = second.value();
            MIRAGE_CHECK(refreshed_observation.id != observation.id);
            MIRAGE_CHECK(refreshed_observation.perception.size() >= 1);
            if (!observation.perception.empty() && !refreshed_observation.perception.empty()) {
                MIRAGE_CHECK(refreshed_observation.perception[0].value.id !=
                             observation.perception[0].value.id);
            }
            MIRAGE_CHECK(refreshed_observation.screen.has_value());
            if (observation.screen.has_value() && refreshed_observation.screen.has_value()) {
                MIRAGE_CHECK(refreshed_observation.screen->value.frame_id !=
                             observation.screen->value.frame_id);
            }
        }
        // The registry advanced as a whole: next scope handle, same
        // deterministic region numbering, the stale "@vs1" generation is
        // fully replaced.
        const desktop::VisualSnapshot active_after = registry.current();
        MIRAGE_CHECK(active_after.scope_ref == "@vs2");
        MIRAGE_CHECK(active_after.regions.size() == 1);
        MIRAGE_CHECK(active_after.regions[0].ref == "@v1");
        MIRAGE_CHECK(!registry.resolve("@vs1").has_value());
        MIRAGE_CHECK(registry.resolve("@v1").has_value());

        // ---- after stop: fail closed / degrade, never pretend ------------
        pipeline.stop();
        MIRAGE_CHECK(!pipeline.running());
        const auto stopped_capabilities = binding.capabilities();
        MIRAGE_CHECK(!stopped_capabilities.screen_capture);
        MIRAGE_CHECK(stopped_capabilities.perception_sources == 0);

        // Required perception after stop: the honest capability report is
        // the first line of defense — a stopped pipeline no longer declares
        // perception_sources, so the gate refuses the request before any
        // capture.
        mira::ObservationRequest perception_required;
        perception_required.required.perception = 1;
        const auto perception_failed =
            binding.observe(perception_required, mira::make_control_context());
        MIRAGE_CHECK(!perception_failed.has_value());
        if (!perception_failed.has_value()) {
            MIRAGE_CHECK(perception_failed.error().code == mira::ErrorCode::UnsupportedCapability);
            MIRAGE_CHECK(perception_failed.error().safe_message.find("perception") !=
                         std::string::npos);
        }

        // Optional perception after stop: the observation still succeeds,
        // with an explicit degradation carrying the session's stable
        // invalid-state error — never silence.
        mira::ObservationRequest perception_optional;
        perception_optional.optional.perception = 1;
        const auto perception_degraded =
            binding.observe(perception_optional, mira::make_control_context());
        MIRAGE_CHECK(perception_degraded.has_value());
        if (perception_degraded.has_value()) {
            MIRAGE_CHECK(perception_degraded.value().perception.empty());
            MIRAGE_CHECK(perception_degraded.value().quality.overall ==
                         mira::ComponentQuality::Degraded);
            bool invalid_state_note = false;
            for (const auto &note : perception_degraded.value().quality.degradations) {
                invalid_state_note =
                    invalid_state_note || (note.rfind("perception unavailable", 0) == 0 &&
                                           note.find("invalid_state") != std::string::npos);
            }
            MIRAGE_CHECK(invalid_state_note);
        }

        // The capture stage does not depend on the stopped analysis session:
        // an optional screen request still delivers a real frame.
        mira::ObservationRequest screen_optional;
        screen_optional.optional.screen = true;
        const auto screen_after_stop =
            binding.observe(screen_optional, mira::make_control_context());
        MIRAGE_CHECK(screen_after_stop.has_value());
        if (screen_after_stop.has_value()) {
            MIRAGE_CHECK(screen_after_stop.value().screen.has_value());
            MIRAGE_CHECK(screen_after_stop.value().quality.overall == mira::ComponentQuality::Good);
        }

        // Close the fixture's X connection only now: closing it destroys the
        // windows it created, and the ASAN build fails the process on the
        // otherwise harmless leak.
        executor.shutdown(true);
        XCloseDisplay(xdisplay);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "visual observation e2e setup failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("visual_observation_e2e");
}
