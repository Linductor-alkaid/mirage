#pragma once
#include "workflow_model.hpp"
#include <mirage/integration/workflow_document.hpp>
#include <optional>

namespace mirage::native_ui {
class WorkflowEditorModel {
  public:
    using Request = WorkflowModel::Request;
    using Document = mirage::integration::WorkflowDocument;
    using Node = mirage::integration::WorkflowNode;
    Document document;
    Node node;
    std::vector<workflow_ipc::ExposedTool> atoms;
    struct Frame {
        Document document;
        std::string original, basis, name, summary;
    };
    std::vector<Frame> return_path;
    std::string name, summary, notice, basis;
    std::size_t selected = 0, segment_first = 0;
    bool segment_marked = false, pending = false, stale = false, node_changed = false,
         supported = false;
    bool dirty() const {
        return !document.id.empty() && (document.json != original_ || node_changed ||
                                        name != document.name || summary != document.summary);
    }
    void invalidate() {
        ++revision_;
        reset_segment();
        pending = false;
        stale = true;
        supported = false;
        notice = "连接或事件记录已中断，草稿保留；请丢弃修改并重新打开以确认服务状态。";
    }
    std::optional<Request> open(const std::string &id, const std::string &digest = {},
                                bool call = false) {
        if (pending || dirty()) {
            notice = "请先保存或丢弃当前修改，再切换工作流。";
            return {};
        }
        if (return_path.size() >= 16) {
            notice = "已达到导航层级上限。";
            return {};
        }
        reset_segment();
        target_ = id;
        expected_ = digest;
        opening_call_ = call;
        return begin("open", workflow_ipc::WorkflowGetRequest{id});
    }
    std::optional<Request> tools() {
        if (pending)
            return {};
        return begin("atoms", workflow_ipc::WorkflowAtomCatalogRequest{});
    }
    bool fresh_document(mirage::integration::WorkflowDocumentResult result) {
        if (!result) {
            notice = result.error.starts_with("arguments")
                         ? "执行参数格式有误，请检查 JSON 的引号、括号和键值。"
                         : "节点配置未通过校验：" + result.error;
            return false;
        }
        if (document.id != result.document.id)
            reset_segment();
        document = std::move(result.document);
        name = document.name;
        summary = document.summary;
        node_changed = false;
        selected = std::min(selected, document.nodes.empty() ? 0 : document.nodes.size() - 1);
        node = document.nodes.empty() ? Node{} : document.nodes[selected];
        notice.clear();
        return true;
    }
    bool select(std::size_t index) {
        if (pending || stale || index >= document.nodes.size())
            return false;
        if (node_changed && !apply_node())
            return false;
        selected = index;
        node = document.nodes[index];
        return true;
    }
    bool apply_node() {
        if (pending)
            return false;
        if (!node_changed)
            return true;
        const auto saved_name = name, saved_summary = summary;
        const bool applied =
            fresh_document(mirage::integration::edit_workflow_node(document, selected, node));
        name = saved_name;
        summary = saved_summary;
        return applied;
    }
    void create() {
        if (pending || dirty()) {
            notice = "请先保存或丢弃当前修改。";
            return;
        }
        if (fresh_document(mirage::integration::new_workflow_document())) {
            original_.clear();
            basis.clear();
            return_path.clear();
            stale = false;
        }
    }
    void discard() {
        if (pending)
            return;
        reset_segment();
        if (original_.empty()) {
            document = {};
            node = {};
            name.clear();
            summary.clear();
            node_changed = false;
        } else
            fresh_document(mirage::integration::read_workflow_document(original_));
    }
    void append(const workflow_ipc::ExposedTool &tool) {
        if (pending || stale || !apply_node())
            return;
        const auto saved_name = name, saved_summary = summary;
        const auto next = mirage::integration::append_workflow_atom(
            document, tool.wire_name,
            tool.wire_name.starts_with("workflow.") ? "被调用工作流" : tool.description);
        if (fresh_document(next) && !document.nodes.empty())
            select(document.nodes.size() - 1);
        name = saved_name;
        summary = saved_summary;
    }
    void replace(const workflow_ipc::ExposedTool &tool) {
        if (pending || stale || node.kind != "tool_call")
            return;
        const auto temporary =
            mirage::integration::append_workflow_atom(document, tool.wire_name, tool.description);
        if (!temporary) {
            notice = temporary.error;
            return;
        }
        node.arguments = temporary.document.nodes.back().arguments;
        node.name = tool.wire_name.starts_with("workflow.") ? "被调用工作流" : tool.description;
        if (tool.wire_name.starts_with("workflow.") && node.verification.empty())
            node.validation = {true, true, "step_result:" + node.id, "eq", "true"};
        node_changed = true;
        apply_node();
    }
    void extract() {
        if (pending || stale || return_path.size() >= 16 || !segment_marked || !apply_node())
            return;
        const auto result = mirage::integration::extract_workflow_segment(
            document, std::min(segment_first, selected), std::max(segment_first, selected),
            document.name + " · 任务段");
        if (!result) {
            notice = result.error;
            return;
        }
        return_path.push_back({document, original_, basis, name, summary});
        fresh_document(result);
        basis.clear();
        original_.clear();
        segment_marked = false;
        notice = "已提取为独立草稿；验证并发布后，可从原子任务目录插入调用来源工作流。";
    }
    void back() {
        if (pending || return_path.empty() || dirty()) {
            notice = "请先保存或丢弃当前修改，再返回。";
            return;
        }
        reset_segment();
        const auto parent = return_path.back();
        return_path.pop_back();
        fresh_document(mirage::integration::read_workflow_document(parent.document.json));
        original_ = parent.original;
        basis = parent.basis;
        name = parent.name;
        summary = parent.summary;
        stale = false;
        notice = "已返回调用来源草稿；保存时会校验服务中的当前版本。";
    }
    std::optional<Request> save(bool publish) {
        if (!supported) {
            notice = "当前服务不支持工作流图编辑与动态调用，请更新并重启服务。";
            return {};
        }
        if (pending || stale || document.id.empty() || !apply_node())
            return {};
        if (!fresh_document(mirage::integration::rename_workflow_document(document, name, summary)))
            return {};
        target_ = document.id;
        submitted_ = document.json;
        return publish
                   ? begin("publish", workflow_ipc::WorkflowPublishRequest{submitted_, true, basis})
                   : begin("save", workflow_ipc::WorkflowSaveRequest{submitted_, basis});
    }
    bool accept(const std::string &tag, std::uint64_t revision,
                const workflow_ipc::Response &response) {
        if (!pending || revision != revision_ || tag != operation_)
            return false;
        pending = false;
        if (!response.ok) {
            notice = "操作未完成，草稿已保留：" + response.error.message.substr(0, 2048);
            return true;
        }
        if (tag == "workflow.editor.open") {
            const auto *view = std::get_if<workflow_ipc::WorkflowDefinitionView>(&response.payload);
            if (view && view->workflow_id == target_) {
                auto parsed = mirage::integration::read_workflow_document(view->definition_json);
                if (parsed && parsed.document.id == target_ &&
                    parsed.document.digest == view->digest &&
                    (expected_.empty() || view->digest == expected_)) {
                    if (opening_call_ && !document.id.empty())
                        return_path.push_back({document, original_, basis, name, summary});
                    else if (!opening_call_)
                        return_path.clear();
                    fresh_document(std::move(parsed));
                    original_ = document.json;
                    basis = view->digest;
                    stale = false;
                    return true;
                }
                notice = "版本已变化或定义无效，未打开；请从目录重新选择当前版本。";
                return true;
            }
        } else if (tag == "workflow.editor.atoms") {
            const auto *catalog = std::get_if<workflow_ipc::WorkflowAtomCatalog>(&response.payload);
            if (catalog && catalog->tools.size() <= 512) {
                atoms = catalog->tools;
                supported = catalog->safe_workflow_editor;
                if (!supported)
                    notice = "当前服务版本仅支持查看，请更新并重启服务后编辑。";
                return true;
            }
        } else {
            std::string id, digest;
            if (const auto *saved = std::get_if<workflow_ipc::WorkflowSaved>(&response.payload)) {
                if (tag == "workflow.editor.save") {
                    id = saved->workflow_id;
                    digest = saved->digest;
                }
            }
            if (const auto *published =
                    std::get_if<workflow_ipc::WorkflowPublished>(&response.payload)) {
                if (tag == "workflow.editor.publish") {
                    id = published->workflow_id;
                    digest = published->digest;
                }
            }
            if (id == target_ && digest == document.digest) {
                original_ = submitted_;
                basis = digest;
                notice = tag.ends_with("publish") ? "验证通过并已发布，可作为被调用工作流复用。"
                                                  : "草稿已同步到当前服务；验证发布后才可运行。";
                return true;
            }
        }
        notice = "响应与请求不匹配，请重新读取；草稿已保留。";
        stale = true;
        return true;
    }

  private:
    void reset_segment() {
        segment_marked = false;
        segment_first = 0;
    }
    std::uint64_t revision_ = 0;
    std::string original_, submitted_, target_, expected_, operation_;
    bool opening_call_ = false;
    Request begin(const std::string &action, workflow_ipc::Request body) {
        pending = true;
        notice.clear();
        operation_ = "workflow.editor." + action;
        return {operation_, ++revision_, std::move(body)};
    }
};
} // namespace mirage::native_ui
