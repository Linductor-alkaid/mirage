#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mirage::integration {
// Standard-library-only editor projection. Mira owns IR parsing and validation.
struct WorkflowCondition {
    bool enabled = false, edited = false;
    std::string signal, op = "eq", value = "true";
};
struct WorkflowNode {
    WorkflowCondition condition, validation;
    std::string id, name, kind, arguments, precondition, verification, recovery;
    std::string jump_to, called_workflow_id, fallback_to;
    bool loop_head = false;
    unsigned max_iterations = 1, max_attempts = 1;
};
struct WorkflowDocument {
    std::string id, name, summary, json, digest;
    std::vector<WorkflowNode> nodes;
};
struct WorkflowDocumentResult {
    WorkflowDocument document;
    std::string error;
    explicit operator bool() const { return error.empty(); }
};
WorkflowDocumentResult read_workflow_document(std::string_view json);
WorkflowDocumentResult new_workflow_document();
WorkflowDocumentResult edit_workflow_node(const WorkflowDocument &, std::size_t,
                                          const WorkflowNode &);
WorkflowDocumentResult rename_workflow_document(const WorkflowDocument &, const std::string &,
                                                const std::string &);
WorkflowDocumentResult append_workflow_atom(const WorkflowDocument &, const std::string &wire,
                                            const std::string &name);
WorkflowDocumentResult extract_workflow_segment(const WorkflowDocument &, std::size_t first,
                                                std::size_t last, const std::string &name);
} // namespace mirage::integration
