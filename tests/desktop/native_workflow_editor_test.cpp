#include "../support/test.hpp"
#include "../support/workflow_graph_fixture.hpp"
#include "workflow_editor_model.hpp"
#include <iostream>
using mirage::native_ui::WorkflowEditorModel;
namespace i = mirage::integration;
namespace ipc = mirage::runtime::ipc;
namespace {
ipc::Response reply(ipc::ResponsePayload payload) {
    ipc::Response response;
    response.ok = true;
    response.payload = std::move(payload);
    return response;
}
} // namespace
int main() {
    auto created = i::new_workflow_document();
    MIRAGE_CHECK(created);
    if (!created) {
        std::cerr << created.error << '\n';
        return mirage::testing::finish("native_workflow_editor_test");
    }
    auto child = created;
    MIRAGE_CHECK(child && child.document.nodes.size() == 1);
    if (!child)
        return mirage::testing::finish("native_workflow_editor_test");
    auto node = child.document.nodes[0];
    node.arguments = R"({"tool":"desktop.filesystem.read_text","path":"/fixture/good"})";
    auto edited = i::edit_workflow_node(child.document, 0, node);
    MIRAGE_CHECK(edited && edited.document.digest != child.document.digest);
    node.precondition = R"({"signal":"run_parameter:ready","op":"eq","value":true})";
    auto condition = i::edit_workflow_node(child.document, 0, node);
    MIRAGE_CHECK(condition && !condition.document.nodes[0].precondition.empty());
    node.condition = {true, true, "run_parameter:ready", "eq", "true"};
    const auto structured = i::edit_workflow_node(child.document, 0, node);
    MIRAGE_CHECK(structured &&
                 structured.document.nodes[0].condition.signal == "run_parameter:ready");
    node.condition.value = "invalid JSON";
    MIRAGE_CHECK(!i::edit_workflow_node(child.document, 0, node));
    node.condition.edited = false;
    node.precondition = "{invalid}";
    MIRAGE_CHECK(!i::edit_workflow_node(child.document, 0, node));
    node.precondition = R"({"signal":"private:unknown","op":"eq","value":true})";
    MIRAGE_CHECK(!i::edit_workflow_node(child.document, 0, node));
    auto segment = i::extract_workflow_segment(edited.document, 0, 0, "可复用任务段");
    MIRAGE_CHECK(segment && segment.document.id != edited.document.id &&
                 segment.document.nodes.size() == 1);
    MIRAGE_CHECK(!i::extract_workflow_segment(edited.document, 1, 0, "bad"));
    auto parent =
        i::append_workflow_atom(created.document, "workflow." + segment.document.id, "归档子流程");
    MIRAGE_CHECK(parent && parent.document.nodes.back().called_workflow_id == segment.document.id);
    WorkflowEditorModel model;
    MIRAGE_CHECK(!model.capability_confirmed && !model.durable);
    auto request = model.open(edited.document.id, edited.document.digest);
    MIRAGE_CHECK(request && model.pending && !model.open(edited.document.id));
    MIRAGE_CHECK(
        !model.accept(request->tag, request->revision - 1, reply(ipc::WorkflowDefinitionView{})));
    MIRAGE_CHECK(
        model.accept(request->tag, request->revision,
                     reply(ipc::WorkflowDefinitionView{edited.document.id, edited.document.digest,
                                                       edited.document.json})));
    MIRAGE_CHECK(!model.dirty() && !model.pending && model.document.id == edited.document.id);
    MIRAGE_CHECK(!model.save(false)); // No mutation against an unadvertised/old service.
    auto capability = model.tools();
    MIRAGE_CHECK(
        model.accept(capability->tag, capability->revision, reply(ipc::WorkflowAtomCatalog{})));
    MIRAGE_CHECK(model.capability_confirmed && !model.supported && !model.durable);
    capability = model.tools();
    MIRAGE_CHECK(model.accept(capability->tag, capability->revision,
                              reply(ipc::WorkflowAtomCatalog{{}, true, true})));
    MIRAGE_CHECK(model.capability_confirmed && model.supported && model.durable);
    model.name = "新的工作流名称";
    model.node.name = "新的原子任务名称";
    model.node_changed = true;
    MIRAGE_CHECK(model.dirty() && !model.open(created.document.id));
    auto save = model.save(true);
    MIRAGE_CHECK(save && model.pending);
    const auto &publish = std::get<ipc::WorkflowPublishRequest>(save->body);
    MIRAGE_CHECK(publish.reusable && publish.expected_head == edited.document.digest);
    const auto parsed = i::read_workflow_document(publish.definition_json);
    MIRAGE_CHECK(parsed && parsed.document.name == "新的工作流名称" &&
                 parsed.document.nodes[0].name == "新的原子任务名称");
    ipc::Response failure;
    failure.error = {"conflict", "head changed"};
    MIRAGE_CHECK(model.accept(save->tag, save->revision, failure) && model.dirty());
    save = model.save(false);
    MIRAGE_CHECK(save);
    MIRAGE_CHECK(model.accept(save->tag, save->revision,
                              reply(ipc::WorkflowSaved{model.document.id, model.document.digest})));
    MIRAGE_CHECK(!model.dirty());
    MIRAGE_CHECK(model.notice.find("草稿已保存") != std::string::npos);
    const auto stale = model.open(created.document.id);
    model.invalidate();
    MIRAGE_CHECK(!model.capability_confirmed && !model.durable && !model.supported);
    MIRAGE_CHECK(!model.accept(stale->tag, stale->revision, reply(ipc::WorkflowDefinitionView{})) &&
                 model.stale);
    MIRAGE_CHECK(!model.save(true));
    // Additive wire fields preserve their explicit false/empty semantics.
    const auto encoded = ipc::encode_request(
        7, ipc::WorkflowPublishRequest{edited.document.json, true, std::string{}});
    const auto decoded = ipc::decode_request(encoded);
    MIRAGE_CHECK(decoded.ok);
    const auto &wire = std::get<ipc::WorkflowPublishRequest>(decoded.body);
    MIRAGE_CHECK(wire.reusable && wire.expected_head && wire.expected_head->empty());
    MIRAGE_CHECK(!ipc::decode_request(
                      R"({"id":1,"op":"workflow.publish","definition":{},"reusable":"true"})")
                      .ok);
    MIRAGE_CHECK(
        !ipc::decode_request(R"({"id":1,"op":"workflow.save","definition":{},"expected_head":42})")
             .ok);
    WorkflowEditorModel nested;
    auto enter = nested.open(parent.document.id, parent.document.digest);
    MIRAGE_CHECK(
        nested.accept(enter->tag, enter->revision,
                      reply(ipc::WorkflowDefinitionView{parent.document.id, parent.document.digest,
                                                        parent.document.json})));
    nested.segment_marked = true;
    nested.segment_first = 1;
    enter = nested.open(segment.document.id, segment.document.digest, true);
    MIRAGE_CHECK(!nested.segment_marked && nested.segment_first == 0);
    MIRAGE_CHECK(
        nested.accept(enter->tag, enter->revision,
                      reply(ipc::WorkflowDefinitionView{
                          segment.document.id, segment.document.digest, segment.document.json})));
    MIRAGE_CHECK(nested.return_path.size() == 1 && nested.document.id == segment.document.id);
    nested.segment_marked = true;
    nested.segment_first = 0;
    nested.back();
    MIRAGE_CHECK(!nested.segment_marked);
    MIRAGE_CHECK(nested.document.id == parent.document.id && nested.return_path.empty() &&
                 !nested.stale);
    enter = nested.open(segment.document.id, std::string(64, 'f'), true);
    MIRAGE_CHECK(
        nested.accept(enter->tag, enter->revision,
                      reply(ipc::WorkflowDefinitionView{
                          segment.document.id, segment.document.digest, segment.document.json})));
    MIRAGE_CHECK(nested.document.id == parent.document.id && nested.return_path.empty());
    nested.segment_marked = true;
    nested.segment_first = 1;
    nested.discard();
    MIRAGE_CHECK(!nested.segment_marked && nested.segment_first == 0);
    nested.segment_marked = true;
    nested.create();
    MIRAGE_CHECK(!nested.segment_marked && nested.document.id != parent.document.id);
    nested.segment_marked = true;
    nested.invalidate();
    MIRAGE_CHECK(!nested.segment_marked);
    auto loop = i::read_workflow_document(mirage::testing::workflow_loop_ir);
    MIRAGE_CHECK(loop && loop.document.nodes[2].max_iterations == 4);
    auto control = loop.document.nodes[2];
    control.max_iterations = 8;
    auto updated_loop = i::edit_workflow_node(loop.document, 2, control);
    MIRAGE_CHECK(updated_loop && updated_loop.document.nodes[2].max_iterations == 8);
    control.max_iterations = 65;
    MIRAGE_CHECK(!i::edit_workflow_node(loop.document, 2, control));
    return mirage::testing::finish("native_workflow_editor_test");
}
