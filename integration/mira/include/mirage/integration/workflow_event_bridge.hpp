#pragma once

#include <mira/event_store.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace mirage::integration {

/// One decoded run-level workflow event (DEC-023): the pinned-free projection
/// the service broadcasts as `workflow.run_updated`. `state` is the pinned
/// WorkflowRunState stable lowercase name ("running" for run-started events,
/// the terminal name for run-settled events); `summary` carries the settled
/// safe_summary and is empty for started events.
struct WorkflowRunEventView final {
    std::string run_id;
    std::string workflow_id;
    std::string state;
    std::uint64_t run_epoch = 0;
    std::string summary;
};

/// Sink invoked from the appending (drive) thread when a run-level workflow
/// event lands. Must be cheap and must not throw: an escaping exception is
/// counted as a sink failure and never touches the stored event.
using WorkflowRunEventSink = std::function<void(const WorkflowRunEventView &)>;

/// Event-store bridge over the hosted pinned workflow runtime (DEC-023):
/// appends everything the runtime emits into an internal bounded
/// MemoryEventStore (the authoritative record, RULE-07) and, on the way in,
/// decodes the pinned `mira.workflow.*` vocabulary to deliver run-level
/// events to the sink. The bridge is the single translation point from the
/// pinned workflow event stream to the product event surface. Thread-safe:
/// appends may come from several drive threads concurrently; the sink sees
/// each event in the appending thread's order with no cross-thread ordering
/// guarantee beyond what the store sequence numbers express.
class WorkflowEventBridge final : public mira::IEventStore {
  public:
    explicit WorkflowEventBridge(std::size_t max_events = 10000);
    ~WorkflowEventBridge() override;
    WorkflowEventBridge(const WorkflowEventBridge &) = delete;
    WorkflowEventBridge &operator=(const WorkflowEventBridge &) = delete;

    /// Installs the run-event sink; replaceable at any time. The service sets
    /// it before attaching the bridge to the runtime so no event is lost.
    void set_sink(WorkflowRunEventSink sink);

    /// Run-level events delivered to the sink.
    [[nodiscard]] std::uint64_t delivered_events() const;
    /// Decodes of a stored workflow event payload that failed (store content
    /// drift); the append itself is unaffected.
    [[nodiscard]] std::uint64_t decode_failures() const;
    /// Sink invocations that escaped with an exception; the stored event and
    /// the append result are unaffected.
    [[nodiscard]] std::uint64_t sink_failures() const;

    // mira::IEventStore
    [[nodiscard]] mira::Result<mira::AppendReceipt>
    append(const mira::AppendRequest &request) override;
    [[nodiscard]] mira::Result<std::vector<mira::AppendReceipt>>
    append_batch(std::span<const mira::AppendRequest> requests) override;
    [[nodiscard]] mira::Result<mira::EventPage> read(const mira::EventQuery &query) const override;
    [[nodiscard]] mira::Result<mira::StoreRecoveryReport>
    recover(const mira::RecoveryOptions &options) override;
    [[nodiscard]] mira::Result<void> flush(mira::Durability durability) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::integration
