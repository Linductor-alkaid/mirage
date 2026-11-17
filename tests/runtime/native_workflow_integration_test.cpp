#include "../support/workflow_recovery_service_fixture.hpp"
#include "runtime_bridge.hpp"
#include "workflow_editor_model.hpp"
#include "workflow_model.hpp"

namespace {
using mirage::native_ui::RuntimeBridge;
using mirage::native_ui::RuntimeMessage;
using mirage::native_ui::WorkflowModel;
struct NativeFixture {
    ServiceFixture service;
    WorkflowModel model;
    mirage::native_ui::WorkflowEditorModel editor;
    RuntimeBridge bridge{[] {}, service.config.socket_path};
    NativeFixture() {
        until([&] { return bridge.connected(); });
    }
    void drain() {
        RuntimeMessage message;
        for (unsigned i = 0; i < 128 && bridge.receive(message); ++i) {
            if (message.kind == RuntimeMessage::Kind::Response &&
                message.tag.starts_with("workflow.")) {
                if (message.tag.starts_with("workflow.editor."))
                    editor.accept(message.tag, message.local_id, message.response);
                else
                    model.accept(message.tag, message.local_id, message.response);
            }
            if (message.kind == RuntimeMessage::Kind::Lost)
                model.invalidate("连接已中断");
        }
        if (bridge.take_gap())
            model.invalidate("事件缺口");
    }
    void send(WorkflowModel::Request request) {
        MIRAGE_CHECK(bridge.call(std::move(request.body), request.tag, request.revision));
    }
    void wait() {
        until([&] {
            drain();
            return !model.busy() && !editor.pending;
        });
    }
    void refresh() {
        for (auto &request : model.refresh())
            send(std::move(request));
        wait();
        MIRAGE_CHECK(model.ready() && model.catalog_fresh && model.learning);
    }
    void control(const std::string &action) {
        const auto request = model.control(action);
        MIRAGE_CHECK(request);
        if (request)
            send(*request);
        wait();
    }
};
void native_control_chain() {
    NativeFixture f;
    const auto run = f.service.waiting();
    f.refresh();
    MIRAGE_CHECK(f.model.catalog.size() == 1 &&
                 f.model.runs.size() == 1); // only product-admitted Runs are listed
    MIRAGE_CHECK(f.model.select(run.id));
    f.service.provider->decision = patch(run.step);
    f.service.provider->held = true;
    // A WaitingAgent view may precede drive settlement; only this explicit
    // fixture retries the public transient rejection, never the product UI.
    until([&] {
        f.control("start");
        if (f.model.recovery)
            return true;
        if (f.model.notice.find("workflow drive is still settling") == std::string::npos)
            throw std::runtime_error("native admission failed: " + f.model.notice);
        f.refresh();
        return false;
    });
    until([&] { return f.service.provider->entered.load(); });
    MIRAGE_CHECK(!f.model.allowed("start") && f.model.allowed("cancel"));
    f.control("status");
    MIRAGE_CHECK(f.model.recovery && !f.model.recovery->settled);
    f.service.provider->held = false;
    f.service.state(run.id, "completed");
    f.refresh();
    MIRAGE_CHECK(f.model.selected() && f.model.selected()->state == "completed");
    // Completion of the Run alone cannot authorize an experience write.
    MIRAGE_CHECK(!f.model.allowed("lesson"));
    until([&] {
        f.control("status");
        return f.model.recovery && f.model.recovery->settled;
    });
    MIRAGE_CHECK(f.model.recovery->outcome == "patched-and-resumed");
    MIRAGE_CHECK(f.model.allowed("lesson"));
    f.control("lesson");
    MIRAGE_CHECK(f.model.notice == "成功恢复经验已保存。");
    MIRAGE_CHECK(f.service.files->successes == 1 && f.service.provider->calls == 1);
    // Starting a new Run is explicit and uses the acknowledged catalog head.
    const auto next = f.model.run(run.workflow);
    MIRAGE_CHECK(next);
    if (next)
        f.send(*next);
    f.wait();
    const auto next_id = f.model.selected_run;
    MIRAGE_CHECK(next_id != run.id && !next_id.empty() && !f.model.runs_fresh);
    f.service.state(next_id, "waiting_agent");
    f.refresh();
    MIRAGE_CHECK(f.model.select(next_id));
    f.control("stop");
    f.service.state(next_id, "cancelled");
    f.refresh();
    MIRAGE_CHECK(f.model.selected()->state == "cancelled" && !f.model.allowed("start"));
    MIRAGE_CHECK(f.service.provider->calls == 1); // no automatic recovery
    f.bridge.shutdown();
}
void native_cancel_and_late_response() {
    NativeFixture f;
    const auto run = f.service.waiting();
    f.refresh();
    MIRAGE_CHECK(f.model.select(run.id));
    f.service.provider->held = true;
    f.service.provider->decision = decision("need_user");
    until([&] {
        f.control("start");
        if (f.model.recovery)
            return true;
        if (f.model.notice.find("workflow drive is still settling") == std::string::npos)
            throw std::runtime_error("native admission failed");
        f.refresh();
        return false;
    });
    until([&] { return f.service.provider->entered.load(); });
    f.control("cancel");
    until([&] {
        f.control("status");
        return f.model.recovery && f.model.recovery->settled;
    });
    MIRAGE_CHECK(f.model.recovery->outcome == "aborted");
    f.refresh();
    MIRAGE_CHECK(f.model.selected()->state == "waiting_agent" && !f.model.allowed("lesson"));
    const auto old = f.model.control("status");
    MIRAGE_CHECK(old);
    if (old)
        f.send(*old);
    f.model.invalidate("重新连接");
    // The actual asynchronously delivered reply cannot restore old claims.
    f.bridge.shutdown();
    f.drain();
    MIRAGE_CHECK(!f.model.ready() && !f.model.recovery && !f.model.allowed("start"));
    MIRAGE_CHECK(f.service.provider->calls == 1 && f.service.files->successes == 0);
}
void native_editor_chain() {
    NativeFixture f;
    const auto source = f.service.waiting();
    auto open = f.editor.open(source.workflow, source.digest);
    MIRAGE_CHECK(open);
    f.send(*open);
    f.wait();
    MIRAGE_CHECK(f.editor.document.id == source.workflow && !f.editor.dirty());
    auto tools = f.editor.tools();
    MIRAGE_CHECK(tools);
    f.send(*tools);
    f.wait();
    MIRAGE_CHECK(!f.editor.atoms.empty());
    f.editor.name = "通过原生桥编辑的工作流";
    f.editor.summary = "通过原生检查器编辑并发布的可复用工作流";
    auto save = f.editor.save(false);
    MIRAGE_CHECK(save);
    f.send(*save);
    f.wait();
    MIRAGE_CHECK(!f.editor.dirty() && f.editor.document.name == "通过原生桥编辑的工作流");
    f.editor.node.arguments = R"({"tool":"desktop.filesystem.read_text","path":"/fixture/good"})";
    f.editor.node_changed = true;
    auto publish = f.editor.save(true);
    MIRAGE_CHECK(publish);
    if (!publish)
        throw std::runtime_error("native publish draft: " + f.editor.notice);
    f.send(*publish);
    f.wait();
    MIRAGE_CHECK(!f.editor.dirty() && f.editor.notice.find("已发布") != std::string::npos);
    f.refresh();
    auto run = f.model.run(source.workflow);
    MIRAGE_CHECK(run);
    f.send(*run);
    f.wait();
    MIRAGE_CHECK(!f.model.selected_run.empty());
    f.service.state(f.model.selected_run, "completed");
}

} // namespace
int main() {
    native_editor_chain();
    native_control_chain();
    native_cancel_and_late_response();
    return mirage::testing::finish("native_workflow_integration_test");
}
