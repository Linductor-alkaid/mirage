#include <mirage/integration/workflow_event_bridge.hpp>

#include <mira/workflow_events.hpp>
#include <mira/workflow_run.hpp>

#include <exception>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace mirage::integration {
namespace {

using mira::AppendReceipt;
using mira::AppendRequest;
using mira::EventPayload;
using mira::MemoryEventStore;

/// Decodes one run-level workflow payload into the pinned-free view;
/// nullopt for every other event type or a payload that fails the pinned
/// parser (store content drift — counted by the caller, never fatal).
std::optional<WorkflowRunEventView> decode_run_event(const EventPayload &payload) {
    if (payload.type == "WorkflowRunStarted") {
        const auto parsed = mira::parse_workflow_run_started(payload);
        if (!parsed) {
            return std::nullopt;
        }
        WorkflowRunEventView view;
        view.run_id = parsed.value().run_id.to_string();
        view.workflow_id = parsed.value().workflow_id.to_string();
        view.state = mira::workflow_run_state_name(mira::WorkflowRunState::Running);
        view.run_epoch = 0;
        return view;
    }
    if (payload.type == "WorkflowRunSettled") {
        const auto parsed = mira::parse_workflow_run_settled(payload);
        if (!parsed) {
            return std::nullopt;
        }
        WorkflowRunEventView view;
        view.run_id = parsed.value().run_id.to_string();
        view.state = mira::workflow_run_state_name(parsed.value().terminal_state);
        view.run_epoch = parsed.value().run_epoch;
        view.summary = parsed.value().safe_summary;
        return view;
    }
    return std::nullopt;
}

} // namespace

struct WorkflowEventBridge::Impl {
    explicit Impl(std::size_t max_events) : store(max_events) {}

    /// Delivers one stored event to the sink. Called after the store accepted
    /// the append, outside any lock the store may hold; a throwing sink is
    /// isolated into a counter.
    void deliver(const EventPayload &payload) {
        WorkflowRunEventSink sink;
        {
            std::lock_guard lock(mutex);
            sink = sink_;
        }
        if (!sink) {
            return;
        }
        const auto view = decode_run_event(payload);
        if (!view) {
            if (mira::is_workflow_event_type(payload.type)) {
                std::lock_guard lock(mutex);
                ++decode_failures_;
            }
            return;
        }
        try {
            sink(*view);
            std::lock_guard lock(mutex);
            ++delivered_;
        } catch (const std::exception &) {
            std::lock_guard lock(mutex);
            ++sink_failures_;
        }
    }

    mutable std::mutex mutex;
    WorkflowRunEventSink sink_;
    std::uint64_t delivered_ = 0;
    std::uint64_t decode_failures_ = 0;
    std::uint64_t sink_failures_ = 0;
    /// Internally synchronized; the mutex above guards only the sink handle
    /// and the counters.
    MemoryEventStore store;
};

WorkflowEventBridge::WorkflowEventBridge(std::size_t max_events)
    : impl_(std::make_unique<Impl>(max_events)) {}

WorkflowEventBridge::~WorkflowEventBridge() = default;

void WorkflowEventBridge::set_sink(WorkflowRunEventSink sink) {
    std::lock_guard lock(impl_->mutex);
    impl_->sink_ = std::move(sink);
}

std::uint64_t WorkflowEventBridge::delivered_events() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->delivered_;
}

std::uint64_t WorkflowEventBridge::decode_failures() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->decode_failures_;
}

std::uint64_t WorkflowEventBridge::sink_failures() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->sink_failures_;
}

mira::Result<AppendReceipt> WorkflowEventBridge::append(const AppendRequest &request) {
    auto receipt = impl_->store.append(request);
    if (receipt) {
        impl_->deliver(request.payload);
    }
    return receipt;
}

mira::Result<std::vector<AppendReceipt>>
WorkflowEventBridge::append_batch(std::span<const AppendRequest> requests) {
    auto receipts = impl_->store.append_batch(requests);
    if (receipts) {
        for (const auto &request : requests) {
            impl_->deliver(request.payload);
        }
    }
    return receipts;
}

mira::Result<mira::EventPage> WorkflowEventBridge::read(const mira::EventQuery &query) const {
    return impl_->store.read(query);
}

mira::Result<mira::StoreRecoveryReport>
WorkflowEventBridge::recover(const mira::RecoveryOptions &options) {
    return impl_->store.recover(options);
}

mira::Result<void> WorkflowEventBridge::flush(mira::Durability durability) {
    return impl_->store.flush(durability);
}

} // namespace mirage::integration
