#include "../support/test.hpp"
#include "workflow_model.hpp"

#include <iostream>

namespace {
using Model = mirage::native_ui::WorkflowModel;
namespace ipc = mirage::runtime::ipc;
const std::string workflow(32, '1'), run_id(32, '2'), other(32, '3'), digest(64, 'a');
template <class T> ipc::Response response(T value) {
    ipc::Response r;
    r.ok = true;
    r.payload = std::move(value);
    return r;
}
ipc::WorkflowSummary item() { return {workflow, "发票归档", digest, "dry_run_passed", true, 1}; }
void snapshot(Model &model, const std::string &state = "waiting_agent", bool enabled = true) {
    const auto requests = model.refresh();
    MIRAGE_CHECK(requests.size() == 3 && model.busy());
    if (requests.size() != 3)
        return;
    MIRAGE_CHECK(model.accept(requests[1].tag, requests[1].revision,
                              response(ipc::WorkflowRunList{{{run_id, workflow, state, 1, 1}}})));
    MIRAGE_CHECK(model.busy());
    MIRAGE_CHECK(
        model.accept(requests[0].tag, requests[0].revision, response(ipc::WorkflowList{{item()}})));
    MIRAGE_CHECK(model.accept(
        requests[2].tag, requests[2].revision,
        response(enabled ? ipc::WorkflowLearningStatus{true, 4096, 1U << 20, 65536, 4096}
                         : ipc::WorkflowLearningStatus{})));
    MIRAGE_CHECK(!model.busy() && model.ready());
}
void snapshot_identity_and_bounds() {
    Model model;
    MIRAGE_CHECK(!model.run(workflow) && !model.allowed("start"));
    snapshot(model);
    MIRAGE_CHECK(model.select(run_id));
    const auto request = model.run(workflow);
    MIRAGE_CHECK(request && std::holds_alternative<ipc::WorkflowRunRequest>(request->body));
    if (!request)
        return;
    const auto &body = std::get<ipc::WorkflowRunRequest>(request->body);
    MIRAGE_CHECK(body.digest == digest && body.parameters_json == "{}" && body.policy.empty());
    MIRAGE_CHECK(!model.select(other) && !model.run(workflow) && model.refresh().empty());
    model.invalidate("连接已断开");
    MIRAGE_CHECK(
        !model.accept(request->tag, request->revision, response(ipc::WorkflowRunStarted{other})));
    MIRAGE_CHECK(model.selected_run == run_id && !model.ready());
    MIRAGE_CHECK(!model.allowed("start") && !model.control("status"));
    const auto reads = model.refresh();
    MIRAGE_CHECK(
        !model.accept("workflow.catalog", reads[0].revision - 1, response(ipc::WorkflowList{})));
    MIRAGE_CHECK(
        model.accept(reads[0].tag, reads[0].revision,
                     response(ipc::WorkflowList{std::vector<ipc::WorkflowSummary>(129, item())})));
    MIRAGE_CHECK(!model.catalog_fresh && model.catalog.size() == 1);
    MIRAGE_CHECK(!model.accept(reads[0].tag, reads[0].revision, response(ipc::WorkflowList{})));
    MIRAGE_CHECK(model.accept(
        reads[1].tag, reads[1].revision,
        response(ipc::WorkflowRunList{{{run_id, workflow, "guessed-success", 1, 1}}})));
    MIRAGE_CHECK(!model.runs_fresh && model.runs.size() == 1);
    MIRAGE_CHECK(model.accept(reads[2].tag, reads[2].revision, response(ipc::WorkflowList{})));
    MIRAGE_CHECK(!model.busy() && !model.learning && !model.notice.empty());
    for (const bool duplicate : {false, true}) {
        const auto batch = model.refresh();
        auto entries = std::vector<ipc::WorkflowRunSummary>(duplicate ? 2 : 257,
                                                            {run_id, workflow, "completed", 1, 1});
        MIRAGE_CHECK(
            model.accept(batch[1].tag, batch[1].revision, response(ipc::WorkflowRunList{entries})));
        MIRAGE_CHECK(!model.runs_fresh);
        model.invalidate("重新读取");
    }
    snapshot(model);
    const auto next = model.refresh();
    ipc::Response failed;
    failed.error = {"unavailable", "读取失败"};
    for (const auto &entry : next)
        MIRAGE_CHECK(model.accept(entry.tag, entry.revision, failed));
    MIRAGE_CHECK(!model.ready() && model.runs.size() == 1 && !model.notice.empty());
}
void recovery_and_learning_are_separate() {
    Model model;
    snapshot(model);
    MIRAGE_CHECK(model.select(run_id) && model.allowed("start"));
    auto start = model.control("start");
    MIRAGE_CHECK(start && !model.allowed("status"));
    if (!start)
        return;
    ipc::WorkflowRecoveryStatus value;
    value.run_id = other;
    value.action = "start";
    MIRAGE_CHECK(model.accept(start->tag, start->revision, response(value)));
    MIRAGE_CHECK(!model.recovery && !model.notice.empty() && !model.runs_fresh);
    MIRAGE_CHECK(!model.control("start"));
    snapshot(model);
    start = model.control("start");
    ipc::Response rejected;
    rejected.error = {"invalid_state", "本次运行已使用恢复机会，请查询结果或停止运行。"};
    MIRAGE_CHECK(start && model.accept(start->tag, start->revision, rejected));
    MIRAGE_CHECK(!model.allowed("status") && model.notice.find("先刷新") != std::string::npos);
    snapshot(model);
    start = model.control("start");
    value.run_id = run_id;
    MIRAGE_CHECK(start && model.accept(start->tag, start->revision, response(value)));
    MIRAGE_CHECK(model.recovery && !model.allowed("start") && model.allowed("cancel"));
    const auto cancel = model.control("cancel");
    value.action = "cancel";
    MIRAGE_CHECK(cancel && model.accept(cancel->tag, cancel->revision, response(value)));
    MIRAGE_CHECK(model.selected()->state == "waiting_agent" && !model.recovery->settled);
    const auto query = model.control("status");
    value.action = "status";
    value.settled = true;
    value.ordinal = 1;
    value.workflow_id = workflow;
    value.outcome = "patched-and-resumed";
    MIRAGE_CHECK(query && model.accept(query->tag, query->revision, response(value)));
    MIRAGE_CHECK(!model.allowed("lesson") && !model.allowed("cancel"));
    snapshot(model, "completed");
    MIRAGE_CHECK(model.allowed("lesson") && !model.allowed("stop"));
    const auto lesson = model.control("lesson");
    value = {};
    value.run_id = run_id;
    value.action = "lesson";
    value.lesson_id = "lesson";
    value.lesson_digest = digest;
    MIRAGE_CHECK(lesson && model.accept(lesson->tag, lesson->revision, response(value)));
    MIRAGE_CHECK(model.notice == "成功恢复经验已保存。");
    MIRAGE_CHECK(model.recovery && model.recovery->action == "status");
    snapshot(model, "completed", false);
    MIRAGE_CHECK(!model.allowed("lesson"));
    model.changed();
    MIRAGE_CHECK(!model.ready() && !model.allowed("status") && !model.recovery);
}
void mutation_ack_does_not_settle_run() {
    Model model;
    snapshot(model);
    MIRAGE_CHECK(model.select(run_id));
    const auto stop = model.control("stop");
    MIRAGE_CHECK(stop && model.accept(stop->tag, stop->revision,
                                      response(ipc::WorkflowRunCancelled{other, "cancelled"})));
    MIRAGE_CHECK(model.selected()->state == "waiting_agent" && !model.runs_fresh);
    snapshot(model);
    const auto request = model.run(workflow);
    MIRAGE_CHECK(request && model.accept(request->tag, request->revision,
                                         response(ipc::WorkflowRunStarted{other})));
    MIRAGE_CHECK(model.selected_run == other && !model.runs_fresh && model.runs.size() == 1);
    snapshot(model, "failed");
    MIRAGE_CHECK(model.selected_run.empty() && !model.selected());
    MIRAGE_CHECK(!model.control("start") && !model.run("unknown"));
}
} // namespace
int main() {
    snapshot_identity_and_bounds();
    recovery_and_learning_are_separate();
    mutation_ack_does_not_settle_run();
    return mirage::testing::finish("native_workflow_model_test");
}
