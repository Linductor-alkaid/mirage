#include <mirage/runtime/permission/confirmation_hub.hpp>

#include <algorithm>
#include <utility>

namespace mirage::runtime::permission {
namespace {

/// Wait-poll slice: bounds how long a cancel or a shutdown signal can go
/// unnoticed inside one confirm() wait. Small enough for responsive
/// cancellation, large enough to keep the polling cost negligible.
constexpr std::chrono::milliseconds kWaitSlice{25};

/// Zero-wait probe used at the deadline edge to prefer an answer that just
/// arrived over a timeout.
constexpr std::chrono::milliseconds kNoWait{0};

} // namespace

/// Pending entries carry ids that must not collide with task ids on the
/// wire: the "perm-" prefix plus a monotonic counter (process-local
/// uniqueness is all the contract requires; requests die with the service).
static std::string make_request_id(std::uint64_t counter) {
    return "perm-" + std::to_string(counter);
}

AsyncConfirmationHub::AsyncConfirmationHub(std::chrono::milliseconds wait_budget,
                                           std::size_t max_pending)
    : wait_budget_(wait_budget), max_pending_(max_pending == 0 ? 1 : max_pending) {}

void AsyncConfirmationHub::set_publish_hook(PublishHook hook) {
    std::lock_guard lock(mutex_);
    publish_hook_ = std::move(hook);
}

PendingConfirmation AsyncConfirmationHub::view_of(const std::string &request_id,
                                                  const Entry &entry) {
    PendingConfirmation view;
    view.request_id = request_id;
    view.capability = capability_name(entry.request.capability);
    view.resource = entry.request.resource;
    view.task_id = entry.request.task_id;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.deadline - std::chrono::steady_clock::now());
    // Clamp so a just-expiring entry still satisfies the positive wire
    // constraint; the wait itself converges TimedOut independently.
    view.timeout_ms = std::max<std::int64_t>(1, remaining.count());
    return view;
}

ConfirmationResult AsyncConfirmationHub::confirm(const PermissionRequest &request,
                                                 const CancelProbe &cancelled) {
    // Probe-first: an already-cancelled caller never raises a request.
    if (cancelled && cancelled()) {
        return ConfirmationResult::Cancelled;
    }

    std::string request_id;
    std::future<bool> answer;
    std::chrono::steady_clock::time_point deadline;
    PendingConfirmation view;
    PublishHook publish;
    {
        std::lock_guard lock(mutex_);
        if (pending_.size() >= max_pending_) {
            // Explicit bounded-surface rejection, surfaced as a stable
            // fail-closed reason instead of queueing (RULE-07, DEC-020).
            return ConfirmationResult::Unresolved;
        }
        request_id = make_request_id(++next_id_);
        Entry entry;
        entry.deadline = std::chrono::steady_clock::now() + wait_budget_;
        entry.request = request;
        answer = entry.ready.get_future();
        deadline = entry.deadline;
        pending_.emplace(request_id, std::move(entry));
        view = view_of(request_id, pending_.at(request_id));
        // Copy under the lock, invoke outside it: the hook may publish
        // cross-thread and must never run while the pending map is held.
        publish = publish_hook_;
    }
    if (publish) {
        publish(view);
    }

    for (;;) {
        if (answer.wait_for(kWaitSlice) == std::future_status::ready) {
            // resolve() erased the entry; the promise's shared state lives
            // in this future.
            return answer.get() ? ConfirmationResult::Approved : ConfirmationResult::Rejected;
        }
        if (cancelled && cancelled()) {
            erase_request(request_id);
            return ConfirmationResult::Cancelled;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            // Deadline edge: prefer an answer that just arrived over a
            // timeout (either outcome is a permitted convergence, DEC-020).
            if (answer.wait_for(kNoWait) == std::future_status::ready) {
                return answer.get() ? ConfirmationResult::Approved : ConfirmationResult::Rejected;
            }
            erase_request(request_id);
            return ConfirmationResult::TimedOut;
        }
    }
}

bool AsyncConfirmationHub::resolve(const std::string &request_id, bool approved) {
    std::lock_guard lock(mutex_);
    const auto entry = pending_.find(request_id);
    if (entry == pending_.end()) {
        // Unknown, already decided or expired: not a winner (DEC-020
        // first-response-wins).
        return false;
    }
    entry->second.ready.set_value(approved);
    pending_.erase(entry);
    return true;
}

std::vector<PendingConfirmation> AsyncConfirmationHub::pending() const {
    std::lock_guard lock(mutex_);
    std::vector<PendingConfirmation> views;
    views.reserve(pending_.size());
    for (const auto &[id, entry] : pending_) {
        views.push_back(view_of(id, entry));
    }
    return views;
}

void AsyncConfirmationHub::erase_request(const std::string &request_id) {
    std::lock_guard lock(mutex_);
    pending_.erase(request_id);
}

} // namespace mirage::runtime::permission
