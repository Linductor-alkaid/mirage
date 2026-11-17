#include <algorithm>
#include <functional>
#include <mira/json.hpp>
#include <mirage/integration/workflow_document.hpp>
#include <mirage/runtime/persistence/workflow_catalog.hpp>
#include <set>

namespace mirage::runtime::persistence {
namespace {
using Json = mira::JsonValue;
WorkflowCatalogLoad fail(const std::string &error) { return {false, {}, {}, error}; }
bool keys(const Json &json, const std::set<std::string> &allowed) {
    if (!json.as_object())
        return false;
    for (const auto &[key, value] : *json.as_object()) {
        (void)value;
        if (!allowed.contains(key))
            return false;
    }
    return true;
}
std::string text(const Json &json, const char *key) {
    const auto *value = json.find(key);
    return value && value->as_string() ? *value->as_string() : std::string{};
}
} // namespace
std::string encode_workflow_catalog(const WorkflowCatalog &entries) {
    if (entries.size() > kWorkflowCatalogMaxEntries)
        return {};
    Json::Array list;
    for (const auto &[id, record] : entries) {
        if (id.size() != 32 || record.head_json.size() > 256 * 1024 ||
            record.published_json.size() > 256 * 1024 || record.head_digest.size() != 64 ||
            (!record.published_digest.empty() && record.published_digest.size() != 64))
            return {};
        const auto head = mira::parse_json(record.head_json);
        const auto published = record.published_json.empty()
                                   ? mira::Result<Json>{Json{}}
                                   : mira::parse_json(record.published_json);
        if (!head || !published)
            return {};
        list.emplace_back(Json::Object{{"id", id},
                                       {"head", record.head_json},
                                       {"head_digest", record.head_digest},
                                       {"published", published.value()},
                                       {"published_digest", record.published_digest},
                                       {"head_published", record.head_published},
                                       {"callable", record.callable},
                                       {"updated_at_ms", record.updated_at_ms}});
    }
    return mira::canonical_json_string(
        Json{Json::Object{{"schema", 1}, {"entries", std::move(list)}}});
}
WorkflowCatalogLoad decode_workflow_catalog(const std::string &body, std::size_t capacity) {
    if (body.size() > kWorkflowCatalogMaxBytes)
        return fail("workflow catalog exceeds 72 MiB byte budget");
    mira::JsonLimits limits;
    limits.max_document_bytes = kWorkflowCatalogMaxBytes;
    const auto parsed = mira::parse_json(body, limits);
    if (!parsed || !keys(parsed.value(), {"schema", "entries"}))
        return fail("invalid workflow catalog JSON");
    const auto &root = parsed.value();
    const auto *schema = root.find("schema"), *entries = root.find("entries");
    if (!schema || schema->as_integer() != 1 || !entries || !entries->as_array())
        return fail("unsupported workflow catalog schema");
    if (entries->as_array()->size() > std::min(capacity, kWorkflowCatalogMaxEntries))
        return fail("workflow catalog capacity exceeded");
    WorkflowCatalogLoad result;
    std::map<std::string, std::vector<std::string>> calls;
    for (const auto &item : *entries->as_array()) {
        if (!keys(item, {"id", "head", "head_digest", "published", "published_digest",
                         "head_published", "callable", "updated_at_ms"}))
            return fail("invalid workflow catalog entry fields");
        const auto id = text(item, "id");
        const auto *head = item.find("head"), *published = item.find("published"),
                   *head_published = item.find("head_published"), *callable = item.find("callable"),
                   *updated = item.find("updated_at_ms");
        if (!head || !published || !head_published || !head_published->as_boolean() || !callable ||
            !callable->as_boolean() || !updated || !updated->as_integer() ||
            *updated->as_integer() < 0)
            return fail("invalid workflow catalog entry values");
        const auto head_doc = integration::read_workflow_document(
            head->as_string() ? *head->as_string() : std::string{});
        if (!head_doc || head_doc.document.id != id ||
            head_doc.document.digest != text(item, "head_digest"))
            return fail("workflow catalog head identity mismatch");
        StoredWorkflow record;
        record.head_json = *head->as_string();
        record.head_digest = head_doc.document.digest;
        record.head_published = *head_published->as_boolean();
        record.callable = *callable->as_boolean();
        record.updated_at_ms = *updated->as_integer();
        if (!published->is_null()) {
            const auto doc =
                integration::read_workflow_document(mira::canonical_json_string(*published));
            if (!doc || doc.document.id != id ||
                doc.document.digest != text(item, "published_digest"))
                return fail("workflow catalog publication identity mismatch");
            record.published_json = doc.document.json;
            record.published_digest = doc.document.digest;
            for (const auto &node : doc.document.nodes)
                if (node.kind == "tool_call" && !node.called_workflow_id.empty())
                    calls[id].push_back(node.called_workflow_id);
        } else if (!text(item, "published_digest").empty() || record.head_published ||
                   record.callable)
            return fail("workflow publication is missing");
        if (record.head_published && record.head_digest != record.published_digest)
            return fail("published head differs from publication");
        if (!result.entries.emplace(id, std::move(record)).second)
            return fail("duplicate workflow identity");
    }
    std::map<std::string, unsigned> colors;
    std::function<bool(const std::string &)> visit = [&](const std::string &id) {
        if (colors[id] == 1)
            return false;
        if (colors[id] == 2)
            return true;
        colors[id] = 1;
        for (const auto &target : calls[id]) {
            const auto found = result.entries.find(target);
            if (found == result.entries.end() || !found->second.callable ||
                found->second.published_json.empty() || !visit(target))
                return false;
        }
        colors[id] = 2;
        result.publish_order.push_back(id);
        return true;
    };
    for (const auto &[id, record] : result.entries)
        if (!record.published_json.empty() && !visit(id))
            return fail("workflow catalog has a cycle or missing callable target");
    result.ok = true;
    return result;
}
WorkflowCatalogStore::WorkflowCatalogStore(std::filesystem::path directory, std::size_t capacity)
    : store_(std::move(directory), "workflow-catalog.json", kWorkflowCatalogMaxBytes),
      capacity_(capacity) {}
WorkflowCatalogLoad WorkflowCatalogStore::load() const {
    const auto loaded = store_.load();
    if (loaded.status == LoadStatus::Absent)
        return {true, {}, {}, {}};
    if (loaded.status == LoadStatus::TooLarge)
        return fail("workflow catalog exceeds 72 MiB byte budget");
    if (loaded.status != LoadStatus::Loaded)
        return fail("cannot load workflow catalog: " + loaded.error);
    return decode_workflow_catalog(loaded.body, capacity_);
}
SaveResult WorkflowCatalogStore::save(const WorkflowCatalog &entries) const {
    if (entries.size() > std::min(capacity_, kWorkflowCatalogMaxEntries))
        return {false, "workflow catalog capacity exceeded"};
    const auto body = encode_workflow_catalog(entries);
    if (body.empty())
        return {false, "cannot encode workflow catalog"};
    const auto checked = decode_workflow_catalog(body, capacity_);
    if (!checked.ok)
        return {false, checked.error};
    return store_.save(body);
}
} // namespace mirage::runtime::persistence
