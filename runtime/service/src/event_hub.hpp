#pragma once

// Internal service module surface (not installed, never included from
// public headers): the process-wide event broadcast point for the IPC event
// subscription (DEC-012 decision 5, EXEC-02). Pinned executor types are
// confined to the internal headers and their translation units.

#include <executor/comm/mailbox.hpp>
#include <executor/comm/topic.hpp>
#include <executor/comm/types.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <variant>

#include <mirage/runtime/ipc/protocol.hpp>

namespace mirage::runtime::detail {

/// Multi-subscriber broadcast point for service events. The Topic fans every
/// published event out to each subscriber's own bounded drop-oldest
/// MpscChannel (the per-connection queue); the LatestMailbox holds only the
/// newest host status so a subscription seeded at attach time starts from
/// the current state, not a stale one. All operations are bounded and
/// thread-safe; the hub owns no threads and never blocks its callers.
class EventHub {
  public:
    EventHub() : topic_("mirage.service.events"), host_status_("mirage.service.host-status") {}

    EventHub(const EventHub &) = delete;
    EventHub &operator=(const EventHub &) = delete;

    void publish_task_update(ipc::TaskUpdatedEvent event) {
        topic_.publish(ipc::EventPayload{std::move(event)});
    }

    /// LatestMailbox semantics for the seed: only the newest status is
    /// kept. The change itself also enters the same Topic publish path.
    void publish_host_status(ipc::HostStatusEvent event) {
        (void)host_status_.try_publish(event);
        topic_.publish(ipc::EventPayload{std::move(event)});
    }

    /// Newest host status, engaged once the hosted runtime reported its
    /// first transition; nullopt before that.
    std::optional<ipc::HostStatusEvent> current_host_status() const {
        ipc::EventPayload payload;
        if (!host_status_.try_load(payload)) {
            return std::nullopt;
        }
        if (auto *status = std::get_if<ipc::HostStatusEvent>(&payload)) {
            return *status;
        }
        return std::nullopt;
    }

    /// One bounded drop-oldest per-connection queue. The caller (the IPC
    /// loop) drains it and assigns per-connection `seq` values at
    /// write-out; drops surface as `events.overflow` markers there.
    executor::comm::TopicSubscription<ipc::EventPayload> subscribe(std::size_t capacity) {
        executor::comm::TopicSubscriptionOptions options;
        options.capacity = capacity == 0 ? 1 : capacity;
        options.drop_policy = executor::comm::DropPolicy::DropOldest;
        options.enable_stats = true;
        options.name = "mirage.ipc.events";
        return topic_.subscribe(std::move(options));
    }

  private:
    executor::comm::Topic<ipc::EventPayload> topic_;
    executor::comm::LatestMailbox<ipc::EventPayload> host_status_;
};

} // namespace mirage::runtime::detail
