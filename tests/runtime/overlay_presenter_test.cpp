// M5-09 Desktop Overlay presenter verification (independent verification
// pass, DEC-029). Drives the service-internal OverlayPresenter against a
// scripted carrier on the test thread (the carrier contract runs the
// surface on the calling thread, so no test-side threads are needed):
// action/confirmation/observation composition, the event-driven task
// banner (Active re-arms, terminal retires), latest-state-wins dedup, the
// RULE-07 budgets, the hub-probe confirmation revalidation (one clearing
// publish per request, re-show after a clear, null probe disables) and the
// stop/cleanup paths.

#include "../support/test.hpp"

#include "event_hub.hpp"
#include "overlay_presenter.hpp"

#include <mirage/desktop/semantic_snapshot.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace desktop = mirage::desktop;
namespace ipc = mirage::runtime::ipc;
using mirage::runtime::detail::OverlayPresenter;
using OverlayPresenterDependencies = mirage::runtime::detail::OverlayPresenter::Dependencies;

/// Carrier whose run() executes a fixed number of deterministic pump
/// iterations on the calling thread: per iteration the test hook fires
/// first (producer side), then the tick and frame load (consumer side),
/// then queued clicks are delivered. Mirrors the production loop's
/// ordering (events → tick → frame) without threads or real waits.
class ScriptedCarrier final : public desktop::OverlayCarrier {
  public:
    std::function<void(std::size_t)> on_iteration;
    std::size_t iterations = 1;
    RunReport report{};
    std::vector<desktop::OverlayClick> deliver;

    std::vector<desktop::OverlaySurfaceFrame> presented;
    std::vector<bool> observed_stop;
    bool entered = false;
    int wakeups = 0;

    ScriptedCarrier() { report.clean = true; } // default exit is the clean one

    RunReport run(const desktop::OverlayCarrierContext &context,
                  const std::function<bool()> &stop_requested) override {
        entered = true;
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            if (stop_requested()) {
                observed_stop.push_back(true);
                return report;
            }
            observed_stop.push_back(false);
            if (on_iteration) {
                on_iteration(iteration);
            }
            if (context.on_tick) {
                context.on_tick();
            }
            desktop::OverlaySurfaceFrame frame;
            if (context.load_frame && context.load_frame(frame)) {
                presented.push_back(std::move(frame));
            }
            const std::vector<desktop::OverlayClick> batch = std::move(deliver);
            deliver.clear();
            for (const desktop::OverlayClick &click : batch) {
                if (context.on_click) {
                    context.on_click(click);
                }
            }
        }
        return report;
    }

    void wakeup() override { ++wakeups; }
};

OverlayPresenterDependencies
base_dependencies(ScriptedCarrier &carrier,
                  kairo::comm::TopicSubscription<ipc::EventPayload> events) {
    OverlayPresenterDependencies dependencies;
    dependencies.carrier = &carrier;
    dependencies.events = std::move(events);
    return dependencies;
}

desktop::OverlayHighlight highlight(int x, int y, int width, int height, std::string label) {
    return desktop::OverlayHighlight{mirage::desktop::WindowGeometry{x, y, width, height},
                                     std::move(label)};
}

void scenario_null_carrier_is_a_dark_no_op() {
    OverlayPresenterDependencies dependencies;
    dependencies.carrier = nullptr;
    OverlayPresenter presenter(std::move(dependencies));
    presenter.show_action("activating \"Save\"", {highlight(0, 0, 10, 10, "Save")});
    kairo::StopSource source;
    presenter.run(source.get_token()); // must return, nothing to drive
    presenter.wakeup();                // must be a safe no-op
    presenter.clear_action();
}

void scenario_stop_before_run_enters_but_never_presents() {
    ScriptedCarrier carrier;
    carrier.iterations = 5;
    OverlayPresenter presenter(base_dependencies(carrier, {}));
    kairo::StopSource source;
    source.request_stop();
    presenter.run(source.get_token());
    MIRAGE_CHECK(carrier.entered);
    MIRAGE_CHECK(!carrier.observed_stop.empty());
    MIRAGE_CHECK(carrier.observed_stop.front());
    MIRAGE_CHECK(carrier.presented.empty());
}

void scenario_action_publishes_once_and_dedups() {
    ScriptedCarrier carrier;
    carrier.iterations = 3;
    mirage::runtime::detail::EventHub hub;
    OverlayPresenter presenter(base_dependencies(carrier, hub.subscribe(8)));
    carrier.on_iteration = [&presenter, &hub](std::size_t iteration) {
        if (iteration == 0) {
            // The action face rides a tracked task (the Active event
            // precedes the atom mirrors in production, DEC-029 decision 5).
            hub.publish_task_update({"t1", "Deploy", "Active", false, false});
            presenter.show_action("activating \"Save\"", {highlight(1, 2, 30, 40, "Save")});
        }
        if (iteration == 1) {
            // The identical update arrives again: latest-state-wins must
            // compose it as no change (no repaint without a change).
            presenter.show_action("activating \"Save\"", {highlight(1, 2, 30, 40, "Save")});
        }
    };
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 1);
    if (carrier.presented.size() == 1) {
        const desktop::OverlaySurfaceFrame &frame = carrier.presented.front();
        MIRAGE_CHECK(frame.visible);
        MIRAGE_CHECK(frame.hint == "activating \"Save\"");
        MIRAGE_CHECK(frame.highlights.size() == 1);
        if (frame.highlights.size() == 1) {
            MIRAGE_CHECK(frame.highlights.front().rect.x == 1);
            MIRAGE_CHECK(frame.highlights.front().rect.height == 40);
            MIRAGE_CHECK(frame.highlights.front().label == "Save");
        }
    }
}

void scenario_clear_action_hides_the_face() {
    ScriptedCarrier carrier;
    carrier.iterations = 3;
    mirage::runtime::detail::EventHub hub;
    OverlayPresenter presenter(base_dependencies(carrier, hub.subscribe(8)));
    carrier.on_iteration = [&presenter, &hub](std::size_t iteration) {
        if (iteration == 0) {
            hub.publish_task_update({"t1", "Deploy", "Active", false, false});
            presenter.show_action("typing into \"Editor\"", {});
        }
        if (iteration == 1) {
            presenter.clear_action();
        }
    };
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 2);
    if (carrier.presented.size() == 2) {
        // With the action cleared the banner is the remaining face.
        MIRAGE_CHECK(carrier.presented[0].visible);
        MIRAGE_CHECK(carrier.presented[0].hint == "typing into \"Editor\"");
        MIRAGE_CHECK(carrier.presented[1].visible);
        MIRAGE_CHECK(carrier.presented[1].hint == "Task: Deploy — Active");
        MIRAGE_CHECK(carrier.presented[1].highlights.empty());
    }
}

void scenario_action_budgets_are_enforced() {
    ScriptedCarrier carrier;
    carrier.iterations = 1;
    mirage::runtime::detail::EventHub hub;
    OverlayPresenter presenter(base_dependencies(carrier, hub.subscribe(8)));
    std::vector<desktop::OverlayHighlight> oversized;
    for (int index = 0; index < 20; ++index) {
        oversized.push_back(highlight(index, 0, 10, 10, "w" + std::to_string(index)));
    }
    hub.publish_task_update({"t1", "Deploy", "Active", false, false});
    presenter.show_action(std::string(400, 'h'), std::move(oversized));
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 1);
    if (carrier.presented.size() == 1) {
        const desktop::OverlaySurfaceFrame &frame = carrier.presented.front();
        MIRAGE_CHECK(frame.hint.size() == desktop::OverlaySurfaceFrame::kMaxTextBytes);
        MIRAGE_CHECK(frame.highlights.size() == desktop::OverlaySurfaceFrame::kMaxHighlights);
        if (frame.highlights.size() == desktop::OverlaySurfaceFrame::kMaxHighlights) {
            // The first 16 survive the truncation, in order.
            MIRAGE_CHECK(frame.highlights.front().label == "w0");
            MIRAGE_CHECK(frame.highlights.back().label == "w15");
        }
    }
}

void scenario_task_banner_tracks_events_and_terminal_retires() {
    ScriptedCarrier carrier;
    carrier.iterations = 4;
    mirage::runtime::detail::EventHub hub;
    OverlayPresenter presenter(base_dependencies(carrier, hub.subscribe(8)));
    carrier.on_iteration = [&presenter, &hub](std::size_t iteration) {
        if (iteration == 0) {
            hub.publish_task_update({"t1", "Deploy", "Running", false, false});
        }
        if (iteration == 1) {
            hub.publish_task_update({"t1", "Deploy", "Completed", true, true});
        }
        if (iteration == 2) {
            // A straggler action after the terminal event stays suppressed;
            // only a following Active event re-arms the faces.
            presenter.show_action("activating \"Save\"", {});
        }
        if (iteration == 3) {
            hub.publish_task_update({"t1", "Deploy", "Active", false, false});
        }
    };
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 3);
    if (carrier.presented.size() == 3) {
        MIRAGE_CHECK(carrier.presented[0].visible);
        MIRAGE_CHECK(carrier.presented[0].hint == "Task: Deploy — Running");
        // The settled task retires the banner and the faces: dark frame.
        MIRAGE_CHECK(!carrier.presented[1].visible);
        MIRAGE_CHECK(carrier.presented[1].hint.empty());
        // The next Active event re-arms: the straggler action face becomes
        // visible again (its hint also wins over the re-armed banner).
        MIRAGE_CHECK(carrier.presented[2].visible);
        MIRAGE_CHECK(carrier.presented[2].hint == "activating \"Save\"");
    }
}

void scenario_action_hint_wins_over_the_banner() {
    ScriptedCarrier carrier;
    carrier.iterations = 2;
    mirage::runtime::detail::EventHub hub;
    OverlayPresenter presenter(base_dependencies(carrier, hub.subscribe(8)));
    carrier.on_iteration = [&presenter, &hub](std::size_t iteration) {
        if (iteration == 0) {
            hub.publish_task_update({"t1", "Deploy", "Running", false, false});
        }
        if (iteration == 1) {
            presenter.show_action("typing into \"Editor\"", {});
        }
    };
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 2);
    if (carrier.presented.size() == 2) {
        MIRAGE_CHECK(carrier.presented[0].hint == "Task: Deploy — Running");
        MIRAGE_CHECK(carrier.presented[1].hint == "typing into \"Editor\"");
    }
}

void scenario_banner_text_is_clamped() {
    ScriptedCarrier carrier;
    carrier.iterations = 1;
    mirage::runtime::detail::EventHub hub;
    OverlayPresenter presenter(base_dependencies(carrier, hub.subscribe(8)));
    carrier.on_iteration = [&hub](std::size_t) {
        hub.publish_task_update({"t1", std::string(400, 'g'), "Running", false, false});
    };
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 1);
    if (carrier.presented.size() == 1) {
        const std::string &hint = carrier.presented.front().hint;
        MIRAGE_CHECK(hint.size() <= desktop::OverlaySurfaceFrame::kMaxTextBytes);
        MIRAGE_CHECK(hint.rfind("Task: ", 0) == 0);
    }
}

void scenario_confirmation_clears_once_through_the_hub_probe() {
    ScriptedCarrier carrier;
    carrier.iterations = 4;
    std::map<std::string, bool> pending{{"req-1", true}};
    OverlayPresenterDependencies dependencies = base_dependencies(carrier, {});
    dependencies.is_pending = [&pending](const std::string &request_id) {
        const auto found = pending.find(request_id);
        return found != pending.end() && found->second;
    };
    OverlayPresenter presenter(std::move(dependencies));
    carrier.on_iteration = [&presenter, &pending](std::size_t iteration) {
        if (iteration == 0) {
            presenter.show_confirmation({"req-1", "filesystem.read", "/tmp/goal.txt", 5000});
        }
        if (iteration == 2) {
            // An IPC client resolved the request between ticks: the hub is
            // the fact source, the presenter must drop the face exactly once.
            pending["req-1"] = false;
        }
    };
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 2);
    if (carrier.presented.size() == 2) {
        MIRAGE_CHECK(carrier.presented[0].confirmation.has_value());
        if (carrier.presented[0].confirmation.has_value()) {
            MIRAGE_CHECK(carrier.presented[0].confirmation->request_id == "req-1");
            MIRAGE_CHECK(carrier.presented[0].confirmation->capability == "filesystem.read");
            MIRAGE_CHECK(carrier.presented[0].confirmation->resource == "/tmp/goal.txt");
            MIRAGE_CHECK(carrier.presented[0].confirmation->timeout_ms == 5000);
        }
        MIRAGE_CHECK(!carrier.presented[1].confirmation.has_value());
        MIRAGE_CHECK(!carrier.presented[1].visible);
    }
}

void scenario_confirmation_without_probe_stays() {
    ScriptedCarrier carrier;
    carrier.iterations = 3;
    OverlayPresenterDependencies dependencies = base_dependencies(carrier, {});
    dependencies.is_pending = nullptr; // no hub wired: no revalidation
    OverlayPresenter presenter(std::move(dependencies));
    carrier.on_iteration = [&presenter](std::size_t iteration) {
        if (iteration == 0) {
            presenter.show_confirmation({"req-1", "input.inject", "hello", 5000});
        }
    };
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 1);
    if (carrier.presented.size() == 1) {
        MIRAGE_CHECK(carrier.presented.front().confirmation.has_value());
    }
}

void scenario_reshown_confirmation_publishes_again() {
    ScriptedCarrier carrier;
    carrier.iterations = 4;
    std::map<std::string, bool> pending{{"req-1", true}};
    OverlayPresenterDependencies dependencies = base_dependencies(carrier, {});
    dependencies.is_pending = [&pending](const std::string &request_id) {
        const auto found = pending.find(request_id);
        return found != pending.end() && found->second;
    };
    OverlayPresenter presenter(std::move(dependencies));
    carrier.on_iteration = [&presenter, &pending](std::size_t iteration) {
        if (iteration == 0 || iteration == 2) {
            // The hub hook raised the same request twice (a retry): each
            // publish must reach the surface again even after the previous
            // one was auto-cleared, and each resolution must publish
            // exactly one clearing update. A raise implies the request is
            // pending again (the hub publishes before the wait).
            pending["req-1"] = true;
            presenter.show_confirmation({"req-1", "input.inject", "hello", 5000});
        }
        if (iteration == 1 || iteration == 3) {
            pending["req-1"] = false; // resolved between the raises
        }
    };
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 4);
    if (carrier.presented.size() == 4) {
        MIRAGE_CHECK(carrier.presented[0].confirmation.has_value());
        MIRAGE_CHECK(!carrier.presented[1].confirmation.has_value());
        MIRAGE_CHECK(carrier.presented[2].confirmation.has_value());
        if (carrier.presented[2].confirmation.has_value()) {
            MIRAGE_CHECK(carrier.presented[2].confirmation->request_id == "req-1");
        }
        MIRAGE_CHECK(!carrier.presented[3].confirmation.has_value());
    }
}

void scenario_observation_face_is_explicit_and_bounded() {
    ScriptedCarrier carrier;
    carrier.iterations = 1;
    mirage::runtime::detail::EventHub hub;
    OverlayPresenterDependencies dependencies = base_dependencies(carrier, hub.subscribe(8));
    dependencies.show_debug = true;
    OverlayPresenter presenter(std::move(dependencies));

    // The debug face rides the tracked task like the action face.
    hub.publish_task_update({"t1", "Deploy", "Active", false, false});
    mirage::desktop::SemanticSnapshot snapshot;
    snapshot.application = "Terminal";
    auto node = [](const char *ref, const char *name, int x, int y, int w, int h) {
        mirage::desktop::SemanticNode entry;
        entry.ref = ref;
        entry.role = "button";
        entry.name = name;
        entry.geometry = mirage::desktop::WindowGeometry{x, y, w, h};
        return entry;
    };
    snapshot.nodes.push_back(node("@e1", "Run", 5, 6, 100, 50));
    snapshot.nodes.push_back(node("@e2", "", 0, 0, 0, 0)); // no geometry: skipped
    snapshot.nodes.push_back(node("@e3", "Save", 7, 8, 20, 20));
    presenter.show_observation(snapshot);
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 1);
    if (carrier.presented.size() == 1) {
        const desktop::OverlaySurfaceFrame &frame = carrier.presented.front();
        MIRAGE_CHECK(frame.visible);
        MIRAGE_CHECK(frame.debug_boxes.size() == 2);
        if (frame.debug_boxes.size() == 2) {
            MIRAGE_CHECK(frame.debug_boxes[0].label == "@e1 Run");
            MIRAGE_CHECK(frame.debug_boxes[1].label == "@e3 Save");
            MIRAGE_CHECK(frame.debug_boxes[0].rect.width == 100);
        }
    }

    // Without the debug face enabled the snapshot is never composed (the
    // explicit product decision, DEC-029 decision 7).
    ScriptedCarrier dark_carrier;
    dark_carrier.iterations = 1;
    OverlayPresenter dark(base_dependencies(dark_carrier, {}));
    dark.show_observation(snapshot);
    kairo::StopSource dark_source;
    dark.run(dark_source.get_token());
    MIRAGE_CHECK(dark_carrier.presented.empty());

    // The debug face is bounded at 64 boxes (RULE-07): node 65+ never grows
    // the frame.
    ScriptedCarrier bounded_carrier;
    bounded_carrier.iterations = 1;
    mirage::runtime::detail::EventHub bounded_hub;
    OverlayPresenterDependencies bounded_dependencies =
        base_dependencies(bounded_carrier, bounded_hub.subscribe(8));
    bounded_dependencies.show_debug = true;
    OverlayPresenter bounded(std::move(bounded_dependencies));
    bounded_hub.publish_task_update({"t1", "Deploy", "Active", false, false});
    mirage::desktop::SemanticSnapshot oversized;
    for (int index = 0; index < 70; ++index) {
        oversized.nodes.push_back(node("@e1", "", index, 0, 10, 10));
    }
    bounded.show_observation(oversized);
    kairo::StopSource bounded_source;
    bounded.run(bounded_source.get_token());
    MIRAGE_CHECK(bounded_carrier.presented.size() == 1);
    if (bounded_carrier.presented.size() == 1) {
        MIRAGE_CHECK(bounded_carrier.presented.front().debug_boxes.size() ==
                     desktop::OverlaySurfaceFrame::kMaxDebugBoxes);
    }
}

void scenario_observation_labels_are_clamped() {
    ScriptedCarrier carrier;
    carrier.iterations = 1;
    mirage::runtime::detail::EventHub hub;
    OverlayPresenterDependencies dependencies = base_dependencies(carrier, hub.subscribe(8));
    dependencies.show_debug = true;
    OverlayPresenter presenter(std::move(dependencies));
    hub.publish_task_update({"t1", "Deploy", "Active", false, false});
    mirage::desktop::SemanticSnapshot snapshot;
    mirage::desktop::SemanticNode node;
    node.ref = std::string(300, 'r');
    node.name = std::string(300, 'n');
    node.geometry = mirage::desktop::WindowGeometry{0, 0, 10, 10};
    snapshot.nodes.push_back(node);
    presenter.show_observation(snapshot);
    kairo::StopSource source;
    presenter.run(source.get_token());

    MIRAGE_CHECK(carrier.presented.size() == 1);
    if (carrier.presented.size() == 1) {
        const std::string &label = carrier.presented.front().debug_boxes.front().label;
        MIRAGE_CHECK(label.size() <= desktop::OverlaySurfaceFrame::kMaxTextBytes);
    }
}

void scenario_clicks_forward_to_the_service_route() {
    ScriptedCarrier carrier;
    carrier.iterations = 1;
    std::vector<desktop::OverlayClick> routed;
    OverlayPresenterDependencies dependencies = base_dependencies(carrier, {});
    dependencies.on_click = [&routed](const desktop::OverlayClick &click) {
        routed.push_back(click);
    };
    OverlayPresenter presenter(std::move(dependencies));
    carrier.deliver.push_back(desktop::OverlayClick{"req-9", true});
    kairo::StopSource source;
    presenter.run(source.get_token());

    // Click routing is independent of any composed frame: the queued click
    // reached the service route even with the surface dark.
    MIRAGE_CHECK(routed.size() == 1);
    if (routed.size() == 1) {
        MIRAGE_CHECK(routed.front().request_id == "req-9");
        MIRAGE_CHECK(routed.front().approved);
    }
}

void scenario_wakeup_forwards_and_failure_degrades() {
    ScriptedCarrier carrier;
    OverlayPresenter presenter(base_dependencies(carrier, {}));
    presenter.wakeup();
    MIRAGE_CHECK(carrier.wakeups == 1);

    // A carrier whose surface broke on its own: run() returns without
    // throwing and the diagnostic reaches the loud-degradation path.
    ScriptedCarrier broken;
    broken.iterations = 0;
    broken.report = desktop::OverlayCarrier::RunReport{false, "surface refused in test"};
    OverlayPresenter broken_presenter(base_dependencies(broken, {}));
    kairo::StopSource source;
    broken_presenter.run(source.get_token()); // prints the diagnostic loudly
    MIRAGE_CHECK(broken.entered);
}

} // namespace

int main() {
    scenario_null_carrier_is_a_dark_no_op();
    scenario_stop_before_run_enters_but_never_presents();
    scenario_action_publishes_once_and_dedups();
    scenario_clear_action_hides_the_face();
    scenario_action_budgets_are_enforced();
    scenario_task_banner_tracks_events_and_terminal_retires();
    scenario_action_hint_wins_over_the_banner();
    scenario_banner_text_is_clamped();
    scenario_confirmation_clears_once_through_the_hub_probe();
    scenario_confirmation_without_probe_stays();
    scenario_reshown_confirmation_publishes_again();
    scenario_observation_face_is_explicit_and_bounded();
    scenario_observation_labels_are_clamped();
    scenario_clicks_forward_to_the_service_route();
    scenario_wakeup_forwards_and_failure_degrades();
    return mirage::testing::finish("overlay_presenter_test");
}
