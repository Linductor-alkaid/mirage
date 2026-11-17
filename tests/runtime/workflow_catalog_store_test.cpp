#include "../support/ipc_io.hpp"
#include "../support/test.hpp"
#include <fstream>
#include <mira/json.hpp>
#include <mirage/integration/workflow_document.hpp>
#include <mirage/runtime/persistence/workflow_catalog.hpp>

namespace p = mirage::runtime::persistence;
namespace i = mirage::integration;
namespace {
p::StoredWorkflow record(const i::WorkflowDocument &doc) {
    return {doc.json, doc.digest, doc.json, doc.digest, true, true, 1};
}
std::string read_file(const std::filesystem::path &file) {
    std::ifstream input(file);
    return {std::istreambuf_iterator<char>(input), {}};
}
} // namespace
int main() {
    mirage::testing::TempDir dir;
    p::WorkflowCatalogStore store(dir.root() / "catalog");
    MIRAGE_CHECK(store.load().ok && store.load().entries.empty());
    auto leaf = i::new_workflow_document();
    auto source = i::new_workflow_document();
    MIRAGE_CHECK(leaf && source);
    source = i::append_workflow_atom(source.document, "workflow." + leaf.document.id, "call");
    p::WorkflowCatalog catalog{{leaf.document.id, record(leaf.document)},
                               {source.document.id, record(source.document)}};
    MIRAGE_CHECK(store.save(catalog).ok);
    auto loaded = store.load();
    MIRAGE_CHECK(loaded.ok && loaded.entries.size() == 2 && loaded.publish_order.size() == 2);
    MIRAGE_CHECK(loaded.publish_order[0] == leaf.document.id &&
                 loaded.publish_order[1] == source.document.id);
    const auto file = dir.root() / "catalog/workflow-catalog.json";
    const auto original = read_file(file);
    auto bad = catalog;
    bad.begin()->second.head_digest = std::string(64, 'f');
    MIRAGE_CHECK(!store.save(bad).ok && read_file(file) == original);
    auto circular_leaf =
        i::append_workflow_atom(leaf.document, "workflow." + source.document.id, "cycle");
    bad = catalog;
    bad[leaf.document.id] = record(circular_leaf.document);
    MIRAGE_CHECK(!store.save(bad).ok && read_file(file) == original);
    bad = catalog;
    bad.erase(leaf.document.id);
    MIRAGE_CHECK(!store.save(bad).ok && read_file(file) == original);
    p::WorkflowCatalogStore small(dir.root() / "catalog", 1);
    MIRAGE_CHECK(!small.load().ok && !small.save(catalog).ok && read_file(file) == original);
    auto json = mira::parse_json(original).value();
    json.set("schema", 2);
    MIRAGE_CHECK(!p::decode_workflow_catalog(mira::canonical_json_string(json)).ok);
    json = mira::parse_json(original).value();
    json.set("runs", mira::JsonValue::Array{});
    MIRAGE_CHECK(!p::decode_workflow_catalog(mira::canonical_json_string(json)).ok);
    json = mira::parse_json(original).value();
    auto entries = *json.find("entries")->as_array();
    entries.push_back(entries.front());
    json.set("entries", entries);
    MIRAGE_CHECK(!p::decode_workflow_catalog(mira::canonical_json_string(json)).ok);
    // An unvalidated edit may contain a call cycle; the committed publication remains acyclic.
    auto draft =
        i::append_workflow_atom(source.document, "workflow." + source.document.id, "draft cycle");
    catalog[source.document.id].head_json = draft.document.json;
    catalog[source.document.id].head_digest = draft.document.digest;
    catalog[source.document.id].head_published = false;
    MIRAGE_CHECK(store.save(catalog).ok && store.load().ok);
    MIRAGE_CHECK(store.load().entries.at(source.document.id).published_digest ==
                 source.document.digest);
    return mirage::testing::finish("workflow_catalog_store_test");
}
