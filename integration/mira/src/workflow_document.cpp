#include <algorithm>
#include <mira/workflow_ir.hpp>
#include <mirage/integration/workflow_document.hpp>

namespace mirage::integration {
namespace {
using Json = mira::JsonValue;
WorkflowDocumentResult failure(const std::string &message) { return {{}, message}; }
WorkflowDocumentResult project(const mira::WorkflowDefinition &definition) {
    const auto valid = mira::validate_workflow_definition(definition);
    if (!valid)
        return failure(valid.error().safe_message);
    WorkflowDocument doc;
    doc.id = definition.workflow_id.to_string();
    doc.name = definition.name;
    doc.summary = definition.summary;
    const auto json = mira::workflow_definition_to_json(definition);
    doc.json = mira::canonical_json_string(json);
    doc.digest = mira::workflow_definition_digest(definition).to_string();
    const auto &steps = *json.find("steps")->as_array();
    for (std::size_t i = 0; i < definition.steps.size(); ++i) {
        const auto &step = definition.steps[i];
        const auto &encoded = steps[i];
        auto field = [&](const char *key) {
            const auto *value = encoded.find(key);
            return value ? mira::canonical_json_string(*value) : std::string{};
        };
        WorkflowNode node;
        node.id = step.id.to_string();
        node.name = step.name;
        node.kind = mira::workflow_step_kind_name(step.kind);
        node.arguments = mira::canonical_json_string(step.arguments);
        auto predicate = [](const auto &source) {
            WorkflowCondition result;
            if (source) {
                result.enabled = true;
                result.signal = source->signal;
                result.op = mira::workflow_predicate_op_name(source->op);
                result.value = mira::canonical_json_string(source->value);
            }
            return result;
        };
        node.condition = predicate(step.precondition);
        node.validation = predicate(step.verification);
        node.precondition = field("precondition");
        node.verification = field("verification");
        node.recovery = field("recovery");
        if (step.recovery && step.recovery->fallback_step)
            node.fallback_to = step.recovery->fallback_step->to_string();
        node.loop_head = step.loop_head;
        node.max_attempts = step.max_attempts;
        node.max_iterations = step.max_iterations;
        if (step.jump_to)
            node.jump_to = step.jump_to->to_string();
        const auto *tool = step.arguments.find("tool");
        if (tool && tool->as_string()) {
            const auto &wire = *tool->as_string();
            if (wire.starts_with("workflow.") && wire.size() == 41) {
                const auto id = wire.substr(9);
                if (id.find_first_not_of("0123456789abcdef") == std::string::npos)
                    node.called_workflow_id = id;
            }
        }
        doc.nodes.push_back(std::move(node));
    }
    return {std::move(doc), {}};
}
void set(Json::Object &object, std::string key, Json value) {
    std::erase_if(object, [&](const auto &item) { return item.first == key; });
    object.emplace_back(std::move(key), std::move(value));
}
} // namespace
WorkflowDocumentResult read_workflow_document(std::string_view json) {
    const auto parsed = mira::parse_workflow_definition(json);
    return parsed ? project(parsed.value()) : failure(parsed.error().safe_message);
}
WorkflowDocumentResult new_workflow_document() {
    mira::WorkflowDefinition definition;
    definition.workflow_id = mira::WorkflowId::generate();
    definition.name = "未命名工作流";
    definition.summary = "可复用的任务流程";
    definition.allowed_policies = {mira::WorkflowPolicy::Strict, mira::WorkflowPolicy::DryRun};
    mira::WorkflowStep placeholder;
    placeholder.id = mira::StepId::generate();
    placeholder.name = "待配置任务";
    placeholder.arguments = Json::Object{};
    definition.steps.push_back(std::move(placeholder));
    return project(definition);
}
WorkflowDocumentResult edit_workflow_node(const WorkflowDocument &doc, std::size_t index,
                                          const WorkflowNode &node) {
    const auto parsed = mira::parse_workflow_definition(doc.json);
    if (!parsed || index >= parsed.value().steps.size())
        return failure("节点不存在，请重新选择。");
    auto definition = mira::workflow_definition_to_json(parsed.value());
    auto root = *definition.as_object();
    auto steps = *definition.find("steps")->as_array();
    auto step = *steps[index].as_object();
    set(step, "max_attempts", Json{static_cast<std::int64_t>(node.max_attempts)});
    if (node.kind == "control") {
        set(step, "max_iterations", Json{static_cast<std::int64_t>(node.max_iterations)});
        set(step, "jump_to", node.jump_to);
    }
    if (node.name.empty())
        std::erase_if(step, [](const auto &item) { return item.first == "name"; });
    else
        set(step, "name", node.name);
    auto predicate_text = [](const WorkflowCondition &condition, const std::string &original) {
        if (!condition.edited)
            return original;
        if (!condition.enabled)
            return std::string{};
        auto value = mira::parse_json(condition.value);
        if (!value)
            return std::string{"{invalid}"}; // Strict IR decode below reports the failure.
        return mira::canonical_json_string(
            Json{Json::Object{{"signal", condition.signal},
                              {"op", condition.op},
                              {"value", condition.op == "exists" ? Json{} : value.value()}}});
    };
    for (const auto &[key, value] : std::vector<std::pair<std::string, std::string>>{
             {"arguments", node.arguments},
             {"precondition", predicate_text(node.condition, node.precondition)},
             {"verification", predicate_text(node.validation, node.verification)},
             {"recovery", node.recovery}}) {
        std::erase_if(step, [&](const auto &item) { return item.first == key; });
        if (!value.empty()) {
            const auto json = mira::parse_json(value);
            if (!json)
                return failure(key + "：" + json.error().safe_message);
            if (key == "arguments" && node.kind != "tool_call" && json.value().is_null())
                continue;
            step.emplace_back(key, json.value());
        }
    }
    steps[index] = Json{std::move(step)};
    set(root, "steps", Json{std::move(steps)});
    return read_workflow_document(mira::canonical_json_string(Json{std::move(root)}));
}
WorkflowDocumentResult rename_workflow_document(const WorkflowDocument &doc,
                                                const std::string &name,
                                                const std::string &summary) {
    auto parsed = mira::parse_workflow_definition(doc.json);
    if (!parsed)
        return failure(parsed.error().safe_message);
    parsed.value().name = name;
    parsed.value().summary = summary;
    return project(parsed.value());
}
WorkflowDocumentResult append_workflow_atom(const WorkflowDocument &doc, const std::string &wire,
                                            const std::string &name) {
    auto parsed = mira::parse_workflow_definition(doc.json);
    if (!parsed)
        return failure(parsed.error().safe_message);
    mira::WorkflowStep step;
    step.id = mira::StepId::generate();
    step.name = name;
    step.arguments = Json::Object{{"tool", wire}};
    if (wire.starts_with("workflow."))
        step.verification = mira::WorkflowPredicate{"step_result:" + step.id.to_string(),
                                                    mira::WorkflowPredicateOp::Eq, Json{true}};
    parsed.value().steps.push_back(std::move(step));
    return project(parsed.value());
}
WorkflowDocumentResult extract_workflow_segment(const WorkflowDocument &doc, std::size_t first,
                                                std::size_t last, const std::string &name) {
    auto parsed = mira::parse_workflow_definition(doc.json);
    if (!parsed || first > last || last >= parsed.value().steps.size())
        return failure("请选择连续的有效节点。");
    auto &definition = parsed.value();
    definition.workflow_id = mira::WorkflowId::generate();
    definition.name = name;
    definition.summary = "从「" + doc.name + "」提取的可复用任务段";
    definition.steps = {definition.steps.begin() + static_cast<std::ptrdiff_t>(first),
                        definition.steps.begin() + static_cast<std::ptrdiff_t>(last + 1)};
    // External step-result references would silently change semantics on extraction.
    for (const auto &step : definition.steps) {
        for (const auto *predicate : {&step.precondition, &step.verification}) {
            if (*predicate && (*predicate)->signal.starts_with("step_result:")) {
                const auto id = (*predicate)->signal.substr(12, 32);
                if (std::none_of(definition.steps.begin(), definition.steps.end(),
                                 [&](const auto &other) { return other.id.to_string() == id; }))
                    return failure("任务段依赖外部节点的结果，请同时选择依赖节点。");
            }
        }
    }
    return project(definition);
}
} // namespace mirage::integration
