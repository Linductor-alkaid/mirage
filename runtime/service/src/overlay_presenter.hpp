#pragma once

// Internal service module surface (not installed, never included from
// public headers): the Desktop Overlay presentation path (M5-09, DEC-029).
// The service hosts the overlay because its task execution flow produces
// everything the overlay shows; the presenter wires that flow onto the
// platform carrier without touching the IPC wire.
//
// Communication discipline (DEC-029 decision 5/6): flow-side producers
// (task drivers, the confirmation hub hook) publish into LatestMailboxes —
// latest state wins, bounded, never blocking. The presentation loop runs as
// an Executor blocking worker (via the OverlayPumpWorker adapter), drains
// the event Topic subscription (task banner state) and the mailboxes, and
// composes the newest OverlaySurfaceFrame for the carrier. All overlay
// state lives on the pump thread.

#include <kairo/blocking_io.hpp>
#include <kairo/comm/mailbox.hpp>
#include <kairo/comm/topic.hpp>
#include <kairo/stop_token.hpp>
#include <kairo/types.hpp>

#include <mirage/desktop/overlay_carrier.hpp>
#include <mirage/desktop/overlay_surface.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>
#include <mirage/runtime/ipc/protocol.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mirage::runtime::detail {

/// Presents the Desktop Overlay surface for the service (M5-09, DEC-029).
/// Owns no threads; run()/wakeup() are driven by the owning executor's
/// blocking worker through OverlayPumpWorker.
class OverlayPresenter final : public kairo::IBlockingIoWorker {
  public:
    struct Dependencies {
        /// The platform carrier (not owned; must outlive run()).
        mirage::desktop::OverlayCarrier *carrier = nullptr;
        /// Service event subscription (task / host / permission events):
        /// drained by the pump for the banner state, same bounded
        /// drop-oldest mechanism as an IPC connection's queue.
        kairo::comm::TopicSubscription<mirage::runtime::ipc::EventPayload> events;
        /// Click delivery, invoked on the pump thread; the service routes
        /// it onto its serial domain (wired by RuntimeService).
        std::function<void(const mirage::desktop::OverlayClick &)> on_click;
        /// Confirmation liveness probe (the hub's pending set stays the
        /// fact source, DEC-020); null disables the revalidation.
        std::function<bool(const std::string &request_id)> is_pending;
        /// When false, observation debug boxes are never composed (the
        /// debug face is an explicit product decision, DEC-029 decision 7).
        bool show_debug = false;
    };

    explicit OverlayPresenter(Dependencies dependencies);
    ~OverlayPresenter() override;
    OverlayPresenter(const OverlayPresenter &) = delete;
    OverlayPresenter &operator=(const OverlayPresenter &) = delete;

    /// Flow side: the upcoming desktop action ("即将执行操作提示" + target
    /// highlights, DEC-029 decision 5). Thread-safe, bounded, never blocks;
    /// publishing an empty update clears the action face.
    void show_action(std::string hint, std::vector<mirage::desktop::OverlayHighlight> highlights);
    void clear_action();

    /// Flow side: one pending permission confirmation (hub publish hook).
    void show_confirmation(mirage::desktop::OverlayConfirmation confirmation);

    /// Flow side: the Observation debug face (DEC-026 semantic snapshot
    /// projected as boxes; DEC-029 decision 5). Ignored unless the
    /// dependency enabled the debug face.
    void show_observation(const mirage::desktop::SemanticSnapshot &snapshot);

    // kairo::IBlockingIoWorker
    void run(kairo::StopToken stop_token) override;
    void wakeup() noexcept override;

  private:
    struct ActionUpdate {
        std::string hint;
        std::vector<mirage::desktop::OverlayHighlight> highlights;
    };
    struct ObservationUpdate {
        std::vector<mirage::desktop::OverlayHighlight> boxes;
    };

    /// Pump thread: drops a confirmation the hub no longer reports pending
    /// (the snapshot stays the fact source, DEC-020) and publishes exactly
    /// one clearing update. Separate from compose_frame because the
    /// carrier's tick hook must revalidate without recomposing.
    void revalidate_confirmation();

    /// Pump thread: drains the event subscription and the mailboxes,
    /// revalidates the confirmation against the hub snapshot and composes
    /// the newest frame. False when the composed frame is unchanged.
    bool compose_frame(mirage::desktop::OverlaySurfaceFrame &frame);

    Dependencies dependencies_;
    kairo::comm::LatestMailbox<ActionUpdate> action_;
    kairo::comm::LatestMailbox<std::optional<mirage::desktop::OverlayConfirmation>> confirmation_;
    kairo::comm::LatestMailbox<ObservationUpdate> observation_;
    std::uint64_t action_seq_ = 0;
    std::uint64_t confirmation_seq_ = 0;
    std::uint64_t observation_seq_ = 0;

    /// Presentation caches (pump-thread only, latest state per slot).
    ActionUpdate action_cache_{};
    std::optional<mirage::desktop::OverlayConfirmation> confirmation_cache_{};
    std::vector<mirage::desktop::OverlayHighlight> observation_cache_{};
    std::string banner_goal_;
    std::string banner_progress_;
    bool task_terminal_ = false;
    /// The last confirmation auto-cleared by the hub probe, so a resolved
    /// request publishes exactly one clearing update.
    std::string cleared_confirmation_;
    /// The last composed frame, so unchanged compositions do not repaint.
    mirage::desktop::OverlaySurfaceFrame last_frame_{};
};

/// The Executor blocking-worker adapter around the presenter's loop: the
/// executor owns the adapter, the RuntimeService owns the presenter (so the
/// hub publish hook and the atom feed can hold a stable raw pointer),
/// teardown joins the worker before the presenter is destroyed (DEC-029
/// decision 6).
class OverlayPumpWorker final : public kairo::IBlockingIoWorker {
  public:
    explicit OverlayPumpWorker(OverlayPresenter *presenter) : presenter_(presenter) {}

    void run(kairo::StopToken stop_token) override {
        if (presenter_ != nullptr) {
            presenter_->run(std::move(stop_token));
        }
    }

    void wakeup() noexcept override {
        if (presenter_ != nullptr) {
            presenter_->wakeup();
        }
    }

  private:
    OverlayPresenter *presenter_;
};

} // namespace mirage::runtime::detail
