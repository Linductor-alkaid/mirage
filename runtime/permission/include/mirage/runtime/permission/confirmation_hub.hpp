#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <mirage/runtime/permission/permission.hpp>

namespace mirage::runtime::permission {

/// Snapshot view of one pending confirmation (DEC-020). `timeout_ms` is the
/// remaining wait budget at snapshot time (positive). Deliberately a local
/// struct: the hub stays independent of the IPC module, which maps it onto
/// the wire shape.
struct PendingConfirmation {
    std::string request_id;
    std::string capability;
    std::string resource;
    std::string task_id;
    std::int64_t timeout_ms = 0;
};

/// The Local IPC async confirmation surface (DEC-020): raises each Confirm
/// request as an observable pending entry, waits a bounded budget for an
/// external answer and converges fail closed on silence. Pure std — a
/// mutex-guarded pending map plus one promise per request; no threads, no
/// scheduling, no executor types. The caller (the task driver, running as an
/// executor task) does the waiting through the sliced poll inside
/// confirm(); `resolve()` is the IPC reply path and may be called from any
/// thread.
///
/// Convergence discipline (DEC-020): exactly one of Approved / Rejected /
/// TimedOut / Cancelled per confirm() call; Unresolved only when the
/// pending set is at capacity. The first state transition wins — a response
/// racing the deadline or the probe is resolved by the mutex order, and the
/// loser sees an empty pending set afterwards (resolve returns false).
class AsyncConfirmationHub final : public ConfirmationHandler {
  public:
    /// `wait_budget` bounds every confirmation wait (positive; the service
    /// validates before wiring the hub). `max_pending` caps the pending set
    /// (RULE-07); requests raised while at capacity fail closed as
    /// Unresolved instead of queueing.
    AsyncConfirmationHub(std::chrono::milliseconds wait_budget, std::size_t max_pending = 64);

    AsyncConfirmationHub(const AsyncConfirmationHub &) = delete;
    AsyncConfirmationHub &operator=(const AsyncConfirmationHub &) = delete;

    /// Observer invoked once per raised request, before the wait begins, on
    /// the calling thread. The service wires it to the event hub (the
    /// permission.request publish path, serial-domain best effort). Must be
    /// bounded and must not throw; the hub stays answerable without a hook.
    using PublishHook = std::function<void(const PendingConfirmation &request)>;
    void set_publish_hook(PublishHook hook);

    /// ConfirmationHandler (DEC-020): raises the request, publishes it, then
    /// polls in slices until an answer arrives, the probe fires, or the
    /// budget elapses.
    ConfirmationResult confirm(const PermissionRequest &request,
                               const CancelProbe &cancelled) override;

    /// IPC reply path: resolves the pending request. True when this call
    /// moved the request out of pending (first-response-wins); false when
    /// the id is unknown, already decided or expired.
    bool resolve(const std::string &request_id, bool approved);

    /// The configured wait budget; the service validates positivity before
    /// wiring the hub (start() fails closed on a non-positive budget).
    std::chrono::milliseconds wait_budget() const { return wait_budget_; }

    /// Snapshot of the pending set ordered by request id (the permission.list
    /// face); `timeout_ms` clamps to a positive value so a just-expiring
    /// entry still validates on the wire.
    std::vector<PendingConfirmation> pending() const;

  private:
    struct Entry {
        std::promise<bool> ready;
        std::chrono::steady_clock::time_point deadline;
        PermissionRequest request;
    };

    static PendingConfirmation view_of(const std::string &request_id, const Entry &entry);
    void erase_request(const std::string &request_id);

    mutable std::mutex mutex_;
    std::map<std::string, Entry> pending_;
    std::uint64_t next_id_ = 0;
    std::chrono::milliseconds wait_budget_;
    std::size_t max_pending_;
    PublishHook publish_hook_;
};

} // namespace mirage::runtime::permission
