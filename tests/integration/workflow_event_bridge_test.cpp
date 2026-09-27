#include "../support/test.hpp"

#include <mirage/integration/workflow_event_bridge.hpp>

#include <mira/core_contracts.hpp>
#include <mira/workflow_events.hpp>
#include <mira/workflow_run.hpp>

#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace integration = mirage::integration;

/// A well-formed non-nil workflow id (pinned Id128 hex form); the bridge
/// decodes pinned payloads back onto the pinned contract, so every scenario
/// needs parseable identities.
const std::string kWorkflow = [] {
    const mira::WorkflowId id = mira::WorkflowId::generate();
    return id.to_string();
}();

/// Builds the append request the pinned workflow runtime would emit for one
/// run-started event, bound to a fresh run identity.
mira::AppendRequest run_started_request(mira::WorkflowRunId &run_out) {
    run_out = mira::WorkflowRunId::generate();
    const mira::WorkflowRunStartedEvent event{
        run_out, mira::WorkflowId::parse(kWorkflow).value(), {}, {}, mira::WorkflowPolicy::Strict};
    mira::AppendRequest request;
    request.event_id = mira::EventId::generate();
    request.runtime_id = mira::RuntimeId::generate();
    request.session_id = mira::SessionId::generate();
    request.payload = mira::to_event_payload(event);
    return request;
}

/// Builds the append request for the same run's settled event.
mira::AppendRequest run_settled_request(const mira::WorkflowRunId &run,
                                        mira::WorkflowRunState state, std::uint64_t epoch) {
    const mira::WorkflowRunSettledEvent event{run, state, epoch, "gate run settled"};
    mira::AppendRequest request;
    request.event_id = mira::EventId::generate();
    request.runtime_id = mira::RuntimeId::generate();
    request.session_id = mira::SessionId::generate();
    request.payload = mira::to_event_payload(event);
    return request;
}

void scenario_started_and_settled_deliver() {
    integration::WorkflowEventBridge bridge;
    std::vector<integration::WorkflowRunEventView> delivered;
    bridge.set_sink(
        [&](const integration::WorkflowRunEventView &view) { delivered.push_back(view); });

    mira::WorkflowRunId run;
    const auto started = bridge.append(run_started_request(run));
    MIRAGE_CHECK(started);
    const auto settled =
        bridge.append(run_settled_request(run, mira::WorkflowRunState::Completed, 3));
    MIRAGE_CHECK(settled);

    MIRAGE_CHECK(delivered.size() == 2);
    if (delivered.size() == 2) {
        MIRAGE_CHECK(delivered[0].run_id == run.to_string());
        MIRAGE_CHECK(delivered[0].workflow_id == kWorkflow);
        MIRAGE_CHECK(delivered[0].state == "running");
        MIRAGE_CHECK(delivered[0].run_epoch == 0);
        MIRAGE_CHECK(delivered[0].summary.empty());
        MIRAGE_CHECK(delivered[1].run_id == run.to_string());
        MIRAGE_CHECK(delivered[1].state == "completed");
        MIRAGE_CHECK(delivered[1].run_epoch == 3);
        MIRAGE_CHECK(delivered[1].summary == "gate run settled");
    }
    MIRAGE_CHECK(bridge.delivered_events() == 2);
    MIRAGE_CHECK(bridge.decode_failures() == 0);
    MIRAGE_CHECK(bridge.sink_failures() == 0);
}

void scenario_store_remains_the_authority() {
    integration::WorkflowEventBridge bridge(64);
    mira::WorkflowRunId run;
    const auto request = run_started_request(run);
    const auto appended = bridge.append(request);
    MIRAGE_CHECK(appended);

    // The bridge is an event store: everything the runtime emits must stay
    // readable through the pinned read path (RULE-07), not just broadcast.
    const mira::EventQuery query{request.session_id};
    const auto page = bridge.read(query);
    MIRAGE_CHECK(page);
    MIRAGE_CHECK(page.value().events.size() == 1);
    if (!page.value().events.empty()) {
        MIRAGE_CHECK(page.value().events[0].payload.type == "WorkflowRunStarted");
    }
    MIRAGE_CHECK(bridge.flush(mira::Durability::Buffered));
    const auto recovered = bridge.recover(mira::RecoveryOptions{});
    MIRAGE_CHECK(recovered);
    MIRAGE_CHECK(recovered.value().status == mira::RecoveryStatus::Clean);
}

void scenario_non_workflow_events_are_silent() {
    integration::WorkflowEventBridge bridge;
    std::atomic<int> deliveries{0};
    bridge.set_sink([&](const integration::WorkflowRunEventView &) { ++deliveries; });

    mira::AppendRequest request;
    request.event_id = mira::EventId::generate();
    request.runtime_id = mira::RuntimeId::generate();
    request.session_id = mira::SessionId::generate();
    request.payload.type = "UserMessageInjected";
    request.payload.data = "{}";
    const auto appended = bridge.append(request);
    MIRAGE_CHECK(appended);
    MIRAGE_CHECK(deliveries.load() == 0);
    MIRAGE_CHECK(bridge.delivered_events() == 0);
    MIRAGE_CHECK(bridge.decode_failures() == 0);
}

void scenario_sink_exception_is_isolated() {
    integration::WorkflowEventBridge bridge;
    bridge.set_sink([](const integration::WorkflowRunEventView &) {
        throw std::runtime_error("sink exploded");
    });

    mira::WorkflowRunId run;
    const auto appended = bridge.append(run_started_request(run));
    // The stored event and the append result are unaffected by the sink.
    MIRAGE_CHECK(appended);
    MIRAGE_CHECK(bridge.delivered_events() == 0);
    MIRAGE_CHECK(bridge.sink_failures() == 1);
}

void scenario_batch_append_delivers_each_event() {
    integration::WorkflowEventBridge bridge;
    std::vector<integration::WorkflowRunEventView> delivered;
    bridge.set_sink(
        [&](const integration::WorkflowRunEventView &view) { delivered.push_back(view); });

    mira::WorkflowRunId run;
    std::vector<mira::AppendRequest> batch;
    batch.push_back(run_started_request(run));
    batch.push_back(run_settled_request(run, mira::WorkflowRunState::Failed, 2));
    const auto receipts = bridge.append_batch(batch);
    MIRAGE_CHECK(receipts);
    MIRAGE_CHECK(receipts.value().size() == 2);
    MIRAGE_CHECK(delivered.size() == 2);
    if (delivered.size() == 2) {
        MIRAGE_CHECK(delivered[0].state == "running");
        MIRAGE_CHECK(delivered[1].state == "failed");
    }
}

void scenario_detached_sink_delivers_nothing() {
    integration::WorkflowEventBridge bridge;
    mira::WorkflowRunId run;
    const auto appended = bridge.append(run_started_request(run));
    MIRAGE_CHECK(appended);
    MIRAGE_CHECK(bridge.delivered_events() == 0);
    MIRAGE_CHECK(bridge.sink_failures() == 0);

    // A malformed workflow payload (schema mismatch) counts as a decode
    // failure, never as a delivery.
    integration::WorkflowEventBridge broken;
    std::atomic<int> deliveries{0};
    broken.set_sink([&](const integration::WorkflowRunEventView &) { ++deliveries; });
    mira::AppendRequest malformed;
    malformed.event_id = mira::EventId::generate();
    malformed.runtime_id = mira::RuntimeId::generate();
    malformed.session_id = mira::SessionId::generate();
    malformed.payload.type = "WorkflowRunStarted";
    malformed.payload.data = "{\"schema\":\"mira.workflow.run-started.v1\"}";
    MIRAGE_CHECK(broken.append(malformed));
    MIRAGE_CHECK(deliveries.load() == 0);
    MIRAGE_CHECK(broken.decode_failures() == 1);
}

} // namespace

int main() {
    scenario_started_and_settled_deliver();
    scenario_store_remains_the_authority();
    scenario_non_workflow_events_are_silent();
    scenario_sink_exception_is_isolated();
    scenario_batch_append_delivers_each_event();
    scenario_detached_sink_delivers_nothing();
    return mirage::testing::finish("workflow_event_bridge_test");
}
