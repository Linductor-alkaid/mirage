#include "overlay_presenter.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace mirage::runtime::detail {
namespace {

/// Upper bound for one composed text (matches the frame contract's budget;
/// clipping happens here so carriers never see over-budget text, RULE-07).
void clamp_text(std::string &text) { mirage::desktop::clamp_overlay_text(text); }

/// The banner picks the action hint when one is current, else the tracked
/// task's line ("Task: <goal> — <progress>"); nothing without either.
std::string compose_hint(const std::string &action_hint, const std::string &goal,
                         const std::string &progress, bool task_terminal) {
    if (!action_hint.empty()) {
        return action_hint;
    }
    if (!goal.empty() && !task_terminal) {
        std::string line = "Task: ";
        line += goal;
        line += " — ";
        line += progress;
        clamp_text(line);
        return line;
    }
    return {};
}

/// Field equality for the frame's highlight vectors (WindowGeometry is a
/// frozen contract type without a comparison; composition diffing stays
/// local to the presenter).
bool same_highlights(const std::vector<mirage::desktop::OverlayHighlight> &left,
                     const std::vector<mirage::desktop::OverlayHighlight> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto &a = left[i];
        const auto &b = right[i];
        if (a.rect.x != b.rect.x || a.rect.y != b.rect.y || a.rect.width != b.rect.width ||
            a.rect.height != b.rect.height || a.label != b.label) {
            return false;
        }
    }
    return true;
}

} // namespace

OverlayPresenter::OverlayPresenter(Dependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

OverlayPresenter::~OverlayPresenter() = default;

void OverlayPresenter::show_action(std::string hint,
                                   std::vector<mirage::desktop::OverlayHighlight> highlights) {
    clamp_text(hint);
    highlights.resize(
        std::min(highlights.size(), mirage::desktop::OverlaySurfaceFrame::kMaxHighlights));
    (void)action_.try_publish(ActionUpdate{std::move(hint), std::move(highlights)});
}

void OverlayPresenter::clear_action() { (void)action_.try_publish(ActionUpdate{}); }

void OverlayPresenter::show_confirmation(mirage::desktop::OverlayConfirmation confirmation) {
    clamp_text(confirmation.capability);
    clamp_text(confirmation.resource);
    cleared_confirmation_.clear();
    (void)confirmation_.try_publish(
        std::optional<mirage::desktop::OverlayConfirmation>{std::move(confirmation)});
}

void OverlayPresenter::show_observation(const mirage::desktop::SemanticSnapshot &snapshot) {
    if (!dependencies_.show_debug) {
        return; // the debug face is an explicit product decision (DEC-029)
    }
    std::vector<mirage::desktop::OverlayHighlight> boxes;
    const std::size_t budget = mirage::desktop::OverlaySurfaceFrame::kMaxDebugBoxes;
    for (const auto &node : snapshot.nodes) {
        if (boxes.size() >= budget) {
            break; // RULE-07: the debug face is bounded, never grown
        }
        if (node.geometry.width <= 0 || node.geometry.height <= 0) {
            continue;
        }
        std::string label = node.ref;
        if (!node.name.empty()) {
            label += " ";
            label += node.name;
        }
        clamp_text(label);
        boxes.push_back({node.geometry, std::move(label)});
    }
    (void)observation_.try_publish(ObservationUpdate{std::move(boxes)});
}

void OverlayPresenter::revalidate_confirmation() {
    // The hub's pending set stays the fact source (DEC-020): a request
    // resolved by an IPC client (or expired) leaves the surface on the next
    // tick. One clearing publish per request id.
    if (confirmation_cache_.has_value() && dependencies_.is_pending != nullptr &&
        cleared_confirmation_ != confirmation_cache_->request_id &&
        !dependencies_.is_pending(confirmation_cache_->request_id)) {
        cleared_confirmation_ = confirmation_cache_->request_id;
        confirmation_cache_.reset();
        (void)confirmation_.try_publish(std::nullopt);
    }
}

bool OverlayPresenter::compose_frame(mirage::desktop::OverlaySurfaceFrame &frame) {
    // Newest service events first: the tracked task's banner state. The
    // subscription is drop-oldest, so a busy stream converges to the
    // newest snapshot per task instead of growing (DEC-012).
    mirage::runtime::ipc::EventPayload payload;
    while (dependencies_.events.try_receive(payload)) {
        if (auto *task = std::get_if<mirage::runtime::ipc::TaskUpdatedEvent>(&payload)) {
            banner_goal_ = task->goal;
            banner_progress_ = task->progress;
            task_terminal_ = task->progress == "Completed" || task->progress == "Failed" ||
                             task->progress == "Cancelled";
            if (task_terminal_) {
                // A settled task retires its action and debug faces; the
                // next Active event re-arms them.
                action_cache_.hint.clear();
                action_cache_.highlights.clear();
                observation_cache_.clear();
            }
        }
        // Host status and permission broadcast events do not drive the
        // banner; the confirmation face arrives through the hub hook.
    }

    // Newest flow-side updates second (latest state wins per slot).
    ActionUpdate action_in;
    if (action_.try_load_newer_than(action_seq_, action_in, action_seq_)) {
        action_cache_ = std::move(action_in);
    }
    std::optional<mirage::desktop::OverlayConfirmation> confirmation_in;
    if (confirmation_.try_load_newer_than(confirmation_seq_, confirmation_in, confirmation_seq_)) {
        confirmation_cache_ = std::move(confirmation_in);
    }
    ObservationUpdate observation_in;
    if (observation_.try_load_newer_than(observation_seq_, observation_in, observation_seq_)) {
        observation_cache_ = std::move(observation_in.boxes);
    }

    // Confirmation revalidation runs before the mailbox drain so the tick's
    // clearing publish is picked up in the same composition.
    revalidate_confirmation();

    // Compose the frame. A terminal (or absent) task suppresses the action
    // and debug faces; the hint picks the action line over the banner.
    const bool task_active = !task_terminal_ && !banner_goal_.empty();
    frame.hint = task_active ? compose_hint(action_cache_.hint, banner_goal_, banner_progress_,
                                            task_terminal_)
                             : std::string{};
    frame.highlights =
        task_active ? action_cache_.highlights : std::vector<mirage::desktop::OverlayHighlight>{};
    frame.confirmation = confirmation_cache_;
    frame.debug_boxes = task_active && dependencies_.show_debug
                            ? observation_cache_
                            : std::vector<mirage::desktop::OverlayHighlight>{};
    frame.visible = !frame.hint.empty() || !frame.highlights.empty() ||
                    frame.confirmation.has_value() || !frame.debug_boxes.empty();

    // Only report change when the composed frame actually differs.
    const bool same_confirmation =
        frame.confirmation.has_value() == last_frame_.confirmation.has_value() &&
        (!frame.confirmation.has_value() ||
         (frame.confirmation->request_id == last_frame_.confirmation->request_id &&
          frame.confirmation->capability == last_frame_.confirmation->capability &&
          frame.confirmation->resource == last_frame_.confirmation->resource &&
          frame.confirmation->timeout_ms == last_frame_.confirmation->timeout_ms));
    if (frame.visible == last_frame_.visible && frame.hint == last_frame_.hint &&
        same_confirmation && same_highlights(frame.highlights, last_frame_.highlights) &&
        same_highlights(frame.debug_boxes, last_frame_.debug_boxes)) {
        return false;
    }
    last_frame_ = frame;
    return true;
}

void OverlayPresenter::run(executor::StopToken stop_token) {
    if (dependencies_.carrier == nullptr) {
        return; // wired null: the owner decided against a surface
    }
    const auto stop_requested = [&stop_token] { return stop_token.stop_requested(); };
    mirage::desktop::OverlayCarrierContext context;
    context.load_frame = [this](mirage::desktop::OverlaySurfaceFrame &frame) {
        return compose_frame(frame);
    };
    context.on_tick = [this]() {
        // Time-based revalidation rides the carrier's loop: without a task
        // event to clear a resolved confirmation, the hub probe does. The
        // clearing publish is composed by the load_frame call that follows.
        revalidate_confirmation();
    };
    context.on_click = [this](const mirage::desktop::OverlayClick &click) {
        if (dependencies_.on_click != nullptr) {
            dependencies_.on_click(click);
        }
    };

    const mirage::desktop::OverlayCarrier::RunReport report =
        dependencies_.carrier->run(context, stop_requested);
    if (!report.clean && !stop_requested()) {
        // A surface that broke on its own (creation refused, presentation
        // failure) degrades loudly; the executor stop path is the clean
        // exit and reports nothing (DEC-029 decision 6).
        std::cerr << "mirage-service: overlay carrier exited: " << report.diagnostic << '\n';
    }
}

void OverlayPresenter::wakeup() noexcept {
    if (dependencies_.carrier != nullptr) {
        dependencies_.carrier->wakeup();
    }
}

} // namespace mirage::runtime::detail
