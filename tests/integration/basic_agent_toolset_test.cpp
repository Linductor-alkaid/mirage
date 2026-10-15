// M10-07 (DEC-095) basic agent toolset verification (independent verification
// pass). Covers the registration honesty matrix (disabled group, a provider
// that delivers nothing, a read-only filesystem, exec enablement), the
// session observation record (window folding, version resets, full
// observations, LRU bound), read/write/edit argument resolution and the
// not-observed discipline, the permission gate seam (authorize preferred,
// null gate denies with zero side effects), and real exec dispatches
// (workspace cwd, output facts, timeout) through the pinned registry
// dispatch path.

#include "../support/test.hpp"

#include <mirage/integration/basic_agent_toolset.hpp>
#include <mirage/integration/desktop_tool_context.hpp>

#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include "../support/fake_desktop_environment.hpp"

#include <mira/json.hpp>
#include <mira/model_contracts.hpp>
#include <mira/tool_executor.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

namespace integration = mirage::integration;
namespace desktop = mirage::desktop;
namespace linux_backend = mirage::platform::linux_backend;

using mira::ExposedToolSpec;
using mira::JsonValue;

// --- fixtures ------------------------------------------------------------------

class TempWorkspace {
  public:
    TempWorkspace() {
        std::error_code ec;
        root_ = std::filesystem::temp_directory_path(ec) /
                ("mirage-m10-tools-" + std::to_string(::getpid()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_, ec);
    }
    ~TempWorkspace() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    TempWorkspace(const TempWorkspace &) = delete;
    TempWorkspace &operator=(const TempWorkspace &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

void spew(const std::filesystem::path &path, const std::string &content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
    MIRAGE_CHECK(stream.good());
}

std::string slurp(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

/// Records every permission judgement and answers from a fixed verdict.
struct AuthLog {
    std::vector<std::pair<std::string, std::string>> judged;
    bool verdict = true;
};

/// A permissive, recording authorize seam (preferred over the plain gate).
integration::DesktopToolContext
recording_context(std::shared_ptr<desktop::DesktopEnvironment> environment, AuthLog &auth) {
    integration::DesktopToolContext context;
    context.environment = std::move(environment);
    context.authorize =
        [&auth](const std::string &capability, const std::string &resource,
                const integration::AtomCancelProbe &, const std::string &, const std::string &) {
            auth.judged.emplace_back(capability, resource);
            return auth.verdict;
        };
    return context;
}

JsonValue arguments(std::initializer_list<std::pair<const char *, JsonValue>> members) {
    JsonValue::Object object;
    for (const auto &member : members) {
        object.emplace_back(member.first, member.second);
    }
    return JsonValue{std::move(object)};
}

/// Dispatches one proposal through the registry the way the pinned workflow
/// runtime does: identity comes from the exposed snapshot, the operation id
/// is fresh (at-most-once dispatch), the arguments are schema-shaped.
mira::Result<mira::ToolExecutionRecord> dispatch(mira::BuiltinToolRegistry &registry,
                                                 const std::string &wire_name,
                                                 JsonValue tool_arguments) {
    static int call_sequence = 0;
    const auto exposed = registry.exposed_tools();
    const auto tool =
        std::find_if(exposed.begin(), exposed.end(),
                     [&](const ExposedToolSpec &entry) { return entry.wire_name == wire_name; });
    if (tool == exposed.end()) {
        std::fprintf(stderr, "[basic_agent_toolset_test] dispatch: tool not exposed: %s\n",
                     wire_name.c_str());
        ++::mirage::testing::failure_count();
        return mira::Error{};
    }
    mira::ToolProposal proposal;
    proposal.provider_call_id = mira::ProviderToolCallId{"m10-" + std::to_string(++call_sequence)};
    proposal.tool_id = tool->tool_id;
    proposal.wire_name = tool->wire_name;
    proposal.tool_version = tool->version;
    proposal.arguments = tool_arguments;
    proposal.arguments_digest = mira::canonical_json_digest(tool_arguments);
    proposal.operation_id = mira::OperationId::generate();
    proposal.has_side_effects = tool->has_side_effects;
    return registry.execute(proposal, mira::make_control_context());
}

/// Prints a failed record's summary so a check failure stays actionable.
void report_failure(const char *what, const mira::ToolExecutionRecord &record) {
    if (record.failed) {
        std::fprintf(stderr, "[basic_agent_toolset_test] %s failed record: %s\n", what,
                     record.safe_error_summary.c_str());
    }
}

mira::ToolExecutionRecord run_tool(mira::BuiltinToolRegistry &registry, const std::string &name,
                                   JsonValue tool_arguments, const char *what) {
    auto outcome = dispatch(registry, name, std::move(tool_arguments));
    MIRAGE_CHECK(outcome.has_value());
    if (!outcome.has_value()) {
        std::fprintf(stderr, "[basic_agent_toolset_test] %s dispatch error: %s\n", what,
                     outcome.error().safe_message.c_str());
        return mira::ToolExecutionRecord{};
    }
    report_failure(what, outcome.value());
    return std::move(outcome).value();
}

std::string string_member(const JsonValue &object, const char *key) {
    const auto *value = object.find(key);
    return value != nullptr && value->is_string() ? *value->as_string() : std::string();
}

std::int64_t int_member(const JsonValue &object, const char *key) {
    const auto *value = object.find(key);
    return value != nullptr && value->is_integer() ? *value->as_integer() : -1;
}

bool bool_member(const JsonValue &object, const char *key) {
    const auto *value = object.find(key);
    return value != nullptr && value->is_boolean() ? *value->as_boolean() : false;
}

bool exposed_contains(const mira::BuiltinToolRegistry &registry, const std::string &wire_name) {
    const auto tools = registry.exposed_tools();
    return std::any_of(tools.begin(), tools.end(),
                       [&](const ExposedToolSpec &tool) { return tool.wire_name == wire_name; });
}

integration::BasicAgentToolsOptions
enabled_options(const std::filesystem::path &workspace, bool exec_enabled = true) {
    integration::BasicAgentToolsOptions options;
    options.enabled = true;
    options.exec_enabled = exec_enabled;
    options.workspace = workspace;
    return options;
}

std::shared_ptr<linux_backend::LinuxDesktopEnvironment>
linux_env(const std::vector<std::filesystem::path> &read_roots,
          const std::vector<std::filesystem::path> &write_roots) {
    return std::make_shared<linux_backend::LinuxDesktopEnvironment>(
        read_roots, linux_backend::X11Options{}, linux_backend::AtspiOptions{},
        linux_backend::ApplicationOptions{}, linux_backend::NotificationsOptions{},
        linux_backend::FilesystemWriteOptions{write_roots});
}

// --- registration honesty ---------------------------------------------------------

integration::Decorator identity_decorator() {
    return [](const std::string &, mira::BuiltinToolHandler handler) { return handler; };
}

void scenario_disabled_group_registers_nothing(const std::filesystem::path &root) {
    auto environment = linux_env({root}, {root});
    AuthLog auth;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root);
    context.basic_tools.enabled = false; // the product default
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());
    MIRAGE_CHECK(registry.size() == 0);
    MIRAGE_CHECK(registry.exposed_tools().empty());
}

void scenario_provider_without_capabilities_registers_nothing() {
    // The pre-M10 fake delivers no windowed read, no write/edit capability
    // and no working-directory support: nothing may be registered, and the
    // tools must not surface as per-call permission failures either.
    auto fake_storage = std::make_unique<mirage::testing::FakeDesktopEnvironment>();
    integration::DesktopToolContext context;
    // Alias without owning: the unique_ptr keeps the environment alive while
    // the handlers run.
    context.environment =
        std::shared_ptr<desktop::DesktopEnvironment>(fake_storage.get(),
                                                     [](desktop::DesktopEnvironment *) {});
    context.basic_tools = enabled_options("/tmp");
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());
    MIRAGE_CHECK(registry.size() == 0);
    MIRAGE_CHECK(registry.exposed_tools().empty());
}

void scenario_read_only_environment_registers_read_only(const std::filesystem::path &root) {
    // An empty write scope reports no write/edit capability: registering only
    // what the provider delivers. Exec rides its own enablement switches.
    auto environment = linux_env({root}, {});
    AuthLog auth;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root, /*exec_enabled=*/true);
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());
    const auto tools = registry.exposed_tools();
    MIRAGE_CHECK(tools.size() == 2);
    MIRAGE_CHECK(exposed_contains(registry, "read"));
    MIRAGE_CHECK(exposed_contains(registry, "exec"));
    MIRAGE_CHECK(!exposed_contains(registry, "write"));
    MIRAGE_CHECK(!exposed_contains(registry, "edit"));
    for (const auto &tool : tools) {
        MIRAGE_CHECK(tool.has_side_effects == (tool.wire_name == "exec"));
    }

    // exec has a second enablement switch and needs a workspace.
    mira::BuiltinToolRegistry without_exec;
    context.basic_tools.exec_enabled = false;
    integration::BasicToolSessionState session_two;
    integration::register_basic_agent_tools(without_exec, context, session_two,
                                            context.basic_tools, identity_decorator());
    MIRAGE_CHECK(without_exec.size() == 1);
    MIRAGE_CHECK(exposed_contains(without_exec, "read"));
    MIRAGE_CHECK(!exposed_contains(without_exec, "exec"));

    context.basic_tools.exec_enabled = true;
    context.basic_tools.workspace.clear();
    mira::BuiltinToolRegistry without_workspace;
    integration::BasicToolSessionState session_three;
    integration::register_basic_agent_tools(without_workspace, context, session_three,
                                            context.basic_tools, identity_decorator());
    MIRAGE_CHECK(without_workspace.size() == 1);
    MIRAGE_CHECK(!exposed_contains(without_workspace, "exec"));
}

void scenario_full_environment_registers_four_tools(const std::filesystem::path &root) {
    auto environment = linux_env({root}, {root});
    AuthLog auth;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root);
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());
    MIRAGE_CHECK(registry.size() == 4);
    const auto tools = registry.exposed_tools();
    MIRAGE_CHECK(tools.size() == 4);
    // Wire names are the short model-facing names, deterministically sorted.
    MIRAGE_CHECK(tools[0].wire_name == "edit");
    MIRAGE_CHECK(tools[1].wire_name == "exec");
    MIRAGE_CHECK(tools[2].wire_name == "read");
    MIRAGE_CHECK(tools[3].wire_name == "write");
    for (const auto &tool : tools) {
        MIRAGE_CHECK(!tool.description.empty());
        MIRAGE_CHECK(tool.has_side_effects == (tool.wire_name != "read"));
    }
    // Not a hosted provider tool name; the pinned check accepts them.
    for (const auto &tool : tools) {
        MIRAGE_CHECK(!mira::is_known_hosted_tool_name(tool.wire_name));
    }
}

// --- read tool --------------------------------------------------------------------

void scenario_read_paths_and_facts(const std::filesystem::path &root) {
    auto environment = linux_env({root}, {root});
    AuthLog auth;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root);
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());

    const auto path = root / "notes.txt";
    spew(path, "alpha\nbeta\n");

    // Relative path resolves against the workspace.
    const auto relative = run_tool(registry, "read", arguments({{"path", JsonValue{"notes.txt"}}}),
                                  "read relative");
    MIRAGE_CHECK(!relative.failed);
    const JsonValue &result = relative.result;
    MIRAGE_CHECK(result.is_object());
    MIRAGE_CHECK(string_member(result, "path") == path.string());
    MIRAGE_CHECK(string_member(result, "version").size() == 16); // digest prefix hint
    MIRAGE_CHECK(int_member(result, "file_size") == 11); // "alpha\nbeta\n"
    MIRAGE_CHECK(bool_member(result, "eof"));
    MIRAGE_CHECK(int_member(result, "next_offset") == 2);
    MIRAGE_CHECK(int_member(result, "total_lines") == 2);
    MIRAGE_CHECK(!bool_member(result, "has_bom"));
    MIRAGE_CHECK(!bool_member(result, "has_crlf"));
    const auto *lines = result.find("lines");
    MIRAGE_CHECK(lines != nullptr && lines->is_array());
    if (lines != nullptr && lines->is_array()) {
        const auto &array = *lines->as_array();
        MIRAGE_CHECK(array.size() == 2);
        MIRAGE_CHECK(int_member(array[0], "number") == 1);
        MIRAGE_CHECK(string_member(array[0], "text") == "alpha");
        MIRAGE_CHECK(int_member(array[1], "number") == 2);
        MIRAGE_CHECK(string_member(array[1], "text") == "beta");
    }

    // offset skips lines; the continuation facts follow.
    const auto second = run_tool(
        registry, "read",
        arguments({{"path", JsonValue{"notes.txt"}}, {"offset", JsonValue{std::int64_t{1}}}}),
        "read offset");
    MIRAGE_CHECK(!second.failed);
    const auto *second_lines = second.result.find("lines");
    MIRAGE_CHECK(second_lines != nullptr && second_lines->is_array());
    if (second_lines != nullptr && second_lines->is_array()) {
        const auto &array = *second_lines->as_array();
        MIRAGE_CHECK(array.size() == 1);
        MIRAGE_CHECK(int_member(array[0], "number") == 2);
        MIRAGE_CHECK(string_member(array[0], "text") == "beta");
    }
    MIRAGE_CHECK(bool_member(second.result, "eof"));

    // Absolute paths pass through.
    const auto absolute = run_tool(registry, "read", arguments({{"path", JsonValue{path.string()}}}),
                                   "read absolute");
    MIRAGE_CHECK(!absolute.failed);
    MIRAGE_CHECK(string_member(absolute.result, "path") == path.string());

    // The read judged its capability under the recorded resource path.
    MIRAGE_CHECK(!auth.judged.empty());
    MIRAGE_CHECK(auth.judged.front().first == "filesystem.read");
    MIRAGE_CHECK(auth.judged.front().second == path.string());

    // The window is folded into the session observation: the eof window at
    // offset zero of a digestable file authorizes a full replace.
    const auto observation = session.observation_of(path);
    MIRAGE_CHECK(observation.has_value());
    MIRAGE_CHECK(observation->full);
    MIRAGE_CHECK(observation->version.digest_available());
}

void scenario_read_requires_workspace_for_relative_paths(const std::filesystem::path &root) {
    auto environment = linux_env({root}, {});
    AuthLog auth;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root);
    context.basic_tools.workspace.clear(); // no workspace: no relative paths, no exec
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());

    const auto relative = run_tool(registry, "read", arguments({{"path", JsonValue{"notes.txt"}}}),
                                   "read without workspace");
    MIRAGE_CHECK(relative.failed);
    MIRAGE_CHECK(relative.safe_error_summary.find("workspace_required") != std::string::npos);
    // The refusal happened before the permission judgement and before any
    // provider call.
    MIRAGE_CHECK(auth.judged.empty());

    // An absolute path still works without a workspace.
    spew(root / "direct.txt", "content\n");
    const auto absolute = run_tool(registry, "read",
                                   arguments({{"path", JsonValue{(root / "direct.txt").string()}}}),
                                   "read absolute without workspace");
    MIRAGE_CHECK(!absolute.failed);
}

// --- write / edit discipline ------------------------------------------------------

void scenario_write_not_observed_discipline(const std::filesystem::path &root) {
    auto environment = linux_env({root}, {root});
    AuthLog auth;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root);
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());

    const auto path = root / "existing.txt";
    spew(path, "old content\n");

    // An unread file is never overwritten: the create refusal becomes an
    // explicit not_observed hint pointing at read-first.
    const auto blind = run_tool(
        registry, "write", arguments({{"path", JsonValue{"existing.txt"}}, {"content", JsonValue{"new\n"}}}),
        "write unread existing");
    MIRAGE_CHECK(blind.failed);
    MIRAGE_CHECK(blind.safe_error_summary.find("not_observed") != std::string::npos);
    MIRAGE_CHECK(blind.safe_error_summary.find("read it first") != std::string::npos);
    MIRAGE_CHECK(slurp(path) == "old content\n");

    // Read, then write: the recorded full observation authorizes the replace.
    const auto read = run_tool(registry, "read", arguments({{"path", JsonValue{"existing.txt"}}}),
                               "read before write");
    MIRAGE_CHECK(!read.failed);
    const auto replace = run_tool(
        registry, "write",
        arguments({{"path", JsonValue{"existing.txt"}}, {"content", JsonValue{"new content\n"}}}),
        "write replace after read");
    MIRAGE_CHECK(!replace.failed);
    MIRAGE_CHECK(bool_member(replace.result, "applied"));
    MIRAGE_CHECK(bool_member(replace.result, "replaced"));
    MIRAGE_CHECK(!bool_member(replace.result, "created"));
    MIRAGE_CHECK(int_member(replace.result, "bytes_written") == 12);
    MIRAGE_CHECK(slurp(path) == "new content\n");

    // The successful write refreshed the session observation: a follow-up
    // edit works without re-reading.
    const auto edit = run_tool(
        registry, "edit",
        arguments({{"path", JsonValue{"existing.txt"}},
                   {"old_string", JsonValue{"new content"}},
                   {"new_string", JsonValue{"final content"}}}),
        "edit after write");
    MIRAGE_CHECK(!edit.failed);
    MIRAGE_CHECK(bool_member(edit.result, "applied"));
    MIRAGE_CHECK(int_member(edit.result, "replacements") == 1);
    MIRAGE_CHECK(slurp(path) == "final content\n");

    // Writing a path that does not exist stays create-if-absent.
    const auto create = run_tool(
        registry, "write",
        arguments({{"path", JsonValue{"fresh.txt"}}, {"content", JsonValue{"created\n"}}}),
        "write create");
    MIRAGE_CHECK(!create.failed);
    MIRAGE_CHECK(bool_member(create.result, "created"));
    MIRAGE_CHECK(bool_member(create.result, "applied"));
    MIRAGE_CHECK(!bool_member(create.result, "replaced"));
    MIRAGE_CHECK(slurp(root / "fresh.txt") == "created\n");
}

void scenario_edit_requires_prior_read(const std::filesystem::path &root) {
    auto environment = linux_env({root}, {root});
    AuthLog auth;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root);
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());

    const auto path = root / "code.txt";
    spew(path, "alpha beta\n");

    // Editing an unread file is refused as not_observed and changes nothing.
    const auto blind = run_tool(
        registry, "edit",
        arguments({{"path", JsonValue{"code.txt"}},
                   {"old_string", JsonValue{"beta"}},
                   {"new_string", JsonValue{"BETA"}}}),
        "edit without read");
    MIRAGE_CHECK(blind.failed);
    MIRAGE_CHECK(blind.safe_error_summary.find("not_observed") != std::string::npos);
    MIRAGE_CHECK(slurp(path) == "alpha beta\n");

    // Read once, then edit: the session record authorizes the edit without a
    // mechanical re-read.
    const auto read = run_tool(registry, "read", arguments({{"path", JsonValue{"code.txt"}}}),
                               "read before edit");
    MIRAGE_CHECK(!read.failed);
    const auto judged_before_edit = auth.judged.size();
    const auto edit = run_tool(
        registry, "edit",
        arguments({{"path", JsonValue{"code.txt"}},
                   {"old_string", JsonValue{"beta"}},
                   {"new_string", JsonValue{"BETA"}}}),
        "edit after read");
    MIRAGE_CHECK(!edit.failed);
    MIRAGE_CHECK(bool_member(edit.result, "applied"));
    MIRAGE_CHECK(slurp(path) == "alpha BETA\n");
    // The only permission judgement since the read was the edit's own.
    MIRAGE_CHECK(auth.judged.size() == judged_before_edit + 1);
    MIRAGE_CHECK(auth.judged.back().first == "filesystem.write");
    MIRAGE_CHECK(auth.judged.back().second == path.string());
}

// --- permission gate --------------------------------------------------------------

void scenario_denied_gate_zero_side_effects(const std::filesystem::path &root) {
    auto environment = linux_env({root}, {root});
    AuthLog auth;
    auth.verdict = false;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root);
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());

    const auto path = root / "guarded.txt";
    spew(path, "untouched\n");

    const auto read = run_tool(registry, "read", arguments({{"path", JsonValue{"guarded.txt"}}}),
                               "denied read");
    MIRAGE_CHECK(read.failed);
    MIRAGE_CHECK(read.safe_error_summary.find("filesystem.read") != std::string::npos);

    const auto write = run_tool(
        registry, "write", arguments({{"path", JsonValue{"guarded.txt"}}, {"content", JsonValue{"x"}}}),
        "denied write");
    MIRAGE_CHECK(write.failed);
    MIRAGE_CHECK(write.safe_error_summary.find("filesystem.write") != std::string::npos);
    MIRAGE_CHECK(slurp(path) == "untouched\n");

    const auto edit = run_tool(
        registry, "edit",
        arguments({{"path", JsonValue{"guarded.txt"}},
                   {"old_string", JsonValue{"untouched"}},
                   {"new_string", JsonValue{"changed"}}}),
        "denied edit");
    MIRAGE_CHECK(edit.failed);
    MIRAGE_CHECK(edit.safe_error_summary.find("filesystem.write") != std::string::npos);
    MIRAGE_CHECK(slurp(path) == "untouched\n");

    // The exec refusal must not have created a process: the command would
    // have left a marker file behind.
    const auto marker = root / "exec-marker.txt";
    const auto exec = run_tool(registry, "exec",
                               arguments({{"command", JsonValue{"printf ran > " + marker.string()}}}),
                               "denied exec");
    MIRAGE_CHECK(exec.failed);
    MIRAGE_CHECK(exec.safe_error_summary.find("process.execute") != std::string::npos);
    MIRAGE_CHECK(!std::filesystem::exists(marker));

    // The gate saw exactly the four capability judgements with the resolved
    // resource identifiers.
    MIRAGE_CHECK(auth.judged.size() == 4);
    MIRAGE_CHECK(auth.judged[0].first == "filesystem.read");
    MIRAGE_CHECK(auth.judged[1].first == "filesystem.write");
    MIRAGE_CHECK(auth.judged[2].first == "filesystem.write");
    MIRAGE_CHECK(auth.judged[3].first == "process.execute");
    MIRAGE_CHECK(auth.judged[3].second == "printf ran > " + marker.string());
}

void scenario_null_gate_denies_everything(const std::filesystem::path &root) {
    // No authorize seam, no gate: fail closed, zero side effects, and no
    // judgement callback at all.
    integration::DesktopToolContext context;
    context.environment = linux_env({root}, {root});
    context.basic_tools = enabled_options(root);
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());

    const auto path = root / "gateless.txt";
    spew(path, "stable\n");
    const auto marker = root / "null-gate-marker.txt";

    const auto read = run_tool(registry, "read", arguments({{"path", JsonValue{path.string()}}}),
                               "null gate read");
    MIRAGE_CHECK(read.failed);
    MIRAGE_CHECK(read.safe_error_summary.find("permission denied") != std::string::npos);

    const auto write = run_tool(
        registry, "write", arguments({{"path", JsonValue{path.string()}}, {"content", JsonValue{"x"}}}),
        "null gate write");
    MIRAGE_CHECK(write.failed);
    MIRAGE_CHECK(slurp(path) == "stable\n");

    const auto exec = run_tool(registry, "exec",
                               arguments({{"command", JsonValue{"printf ran > " + marker.string()}}}),
                               "null gate exec");
    MIRAGE_CHECK(exec.failed);
    MIRAGE_CHECK(!std::filesystem::exists(marker));
}

// --- exec tool --------------------------------------------------------------------

void scenario_exec_tool_facts(const std::filesystem::path &root) {
    auto environment = linux_env({root}, {root});
    AuthLog auth;
    auto context = recording_context(environment, auth);
    context.basic_tools = enabled_options(root);
    mira::BuiltinToolRegistry registry;
    integration::BasicToolSessionState session;
    integration::register_basic_agent_tools(registry, context, session, context.basic_tools,
                                            identity_decorator());

    // pwd proves the workspace anchoring; the result carries the full fact
    // set.
    const auto pwd = run_tool(registry, "exec", arguments({{"command", JsonValue{"pwd"}}}),
                              "exec pwd");
    MIRAGE_CHECK(!pwd.failed);
    MIRAGE_CHECK(string_member(pwd.result, "cwd") == root.string());
    MIRAGE_CHECK(string_member(pwd.result, "shell") == "sh");
    MIRAGE_CHECK(string_member(pwd.result, "terminated") == "exit");
    MIRAGE_CHECK(int_member(pwd.result, "exit_code") == 0);
    MIRAGE_CHECK(string_member(pwd.result, "stdout") == root.string() + "\n");
    MIRAGE_CHECK(string_member(pwd.result, "stderr").empty());
    MIRAGE_CHECK(!bool_member(pwd.result, "output_truncated"));

    const auto printf_out = run_tool(registry, "exec",
                                     arguments({{"command", JsonValue{"printf hello-world"}}}),
                                     "exec printf");
    MIRAGE_CHECK(!printf_out.failed);
    MIRAGE_CHECK(string_member(printf_out.result, "stdout") == "hello-world");

    // A relative workdir resolves against the workspace; spaces survive.
    MIRAGE_CHECK(std::filesystem::create_directories(root / "sub dir"));
    const auto subdir = run_tool(
        registry, "exec",
        arguments({{"command", JsonValue{"pwd"}}, {"workdir", JsonValue{"sub dir"}}}),
        "exec workdir");
    MIRAGE_CHECK(!subdir.failed);
    MIRAGE_CHECK(string_member(subdir.result, "cwd") == (root / "sub dir").string());
    MIRAGE_CHECK(string_member(subdir.result, "stdout") == (root / "sub dir").string() + "\n");

    // A non-zero exit code is a result, not a failure.
    const auto exit_code = run_tool(registry, "exec", arguments({{"command", JsonValue{"exit 3"}}}),
                                    "exec exit code");
    MIRAGE_CHECK(!exit_code.failed);
    MIRAGE_CHECK(string_member(exit_code.result, "terminated") == "exit");
    MIRAGE_CHECK(int_member(exit_code.result, "exit_code") == 3);

    // stderr is carried separately.
    const auto stderr_run = run_tool(registry, "exec",
                                     arguments({{"command", JsonValue{"printf err-text >&2"}}}),
                                     "exec stderr");
    MIRAGE_CHECK(!stderr_run.failed);
    MIRAGE_CHECK(string_member(stderr_run.result, "stderr") == "err-text");
    MIRAGE_CHECK(string_member(stderr_run.result, "stdout").empty());

    // timeout_ms clamps up to one second; a timeout is a result carrying the
    // output so far and the whole group is torn down.
    const auto start = std::chrono::steady_clock::now();
    const auto timeout = run_tool(
        registry, "exec",
        arguments({{"command", JsonValue{"printf early; sleep 30"}},
                   {"timeout_ms", JsonValue{std::int64_t{1000}}}}),
        "exec timeout");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    MIRAGE_CHECK(!timeout.failed);
    MIRAGE_CHECK(string_member(timeout.result, "terminated") == "timeout");
    MIRAGE_CHECK(int_member(timeout.result, "exit_code") == -1);
    MIRAGE_CHECK(string_member(timeout.result, "stdout") == "early");
    MIRAGE_CHECK(elapsed.count() >= 950 && elapsed.count() < 15000);

    // A missing command name is the shell's 127 result, not a failed record.
    const auto missing = run_tool(registry, "exec",
                                  arguments({{"command", JsonValue{"mirage-no-such-binary-xyz"}}}),
                                  "exec missing binary");
    MIRAGE_CHECK(!missing.failed);
    MIRAGE_CHECK(int_member(missing.result, "exit_code") == 127);

    // Preview truncation reports the fact.
    context.basic_tools.exec_preview_bytes = 4;
    mira::BuiltinToolRegistry tight_registry;
    integration::BasicToolSessionState tight_session;
    integration::register_basic_agent_tools(tight_registry, context, tight_session,
                                            context.basic_tools, identity_decorator());
    const auto truncated = run_tool(
        tight_registry, "exec", arguments({{"command", JsonValue{"printf ABCDEFGHIJ"}}}),
        "exec truncated preview");
    MIRAGE_CHECK(!truncated.failed);
    MIRAGE_CHECK(bool_member(truncated.result, "output_truncated"));
    MIRAGE_CHECK(string_member(truncated.result, "stdout") == "ABCD");
}

// --- session observation record ---------------------------------------------------

void scenario_session_state_unit_semantics(const std::filesystem::path &root) {
    using desktop::FileObservation;
    using desktop::FileVersion;
    using desktop::ObservedByteRange;

    integration::BasicToolSessionState session;
    MIRAGE_CHECK(!session.observation_of(root / "absent.txt").has_value());

    FileVersion v1;
    v1.device = 1;
    v1.file_index = 2;
    v1.size = 16;
    v1.content_digest = "digest-1";
    FileVersion v2 = v1;
    v2.content_digest = "digest-2";

    // Windows fold into a partial observation; an oversized (undigested)
    // version records nothing.
    session.record_window(root / "a.txt", v1, false, ObservedByteRange{0, 6});
    auto observation = session.observation_of(root / "a.txt");
    MIRAGE_CHECK(observation.has_value());
    MIRAGE_CHECK(!observation->full);
    MIRAGE_CHECK(observation->ranges.size() == 1);
    session.record_window(root / "a.txt", v1, false, ObservedByteRange{10, 6});
    observation = session.observation_of(root / "a.txt");
    MIRAGE_CHECK(observation.has_value());
    MIRAGE_CHECK(observation->ranges.size() == 2);

    FileVersion undigested;
    undigested.size = 9999;
    session.record_window(root / "oversize.txt", undigested, false, ObservedByteRange{0, 1});
    MIRAGE_CHECK(!session.observation_of(root / "oversize.txt").has_value());

    // A version change resets the ranges: the file was replaced underneath.
    session.record_window(root / "a.txt", v2, false, ObservedByteRange{30, 2});
    observation = session.observation_of(root / "a.txt");
    MIRAGE_CHECK(observation.has_value());
    MIRAGE_CHECK(observation->version == v2);
    MIRAGE_CHECK(observation->ranges.size() == 1);
    MIRAGE_CHECK(observation->ranges.front().offset == 30);

    // A full window clears the range list.
    session.record_window(root / "a.txt", v2, true, ObservedByteRange{0, 999});
    observation = session.observation_of(root / "a.txt");
    MIRAGE_CHECK(observation.has_value());
    MIRAGE_CHECK(observation->full);
    MIRAGE_CHECK(observation->ranges.empty());

    // remember_full overwrites whatever was recorded.
    session.record_window(root / "b.txt", v1, false, ObservedByteRange{0, 1});
    session.remember_full(root / "b.txt", v2);
    observation = session.observation_of(root / "b.txt");
    MIRAGE_CHECK(observation.has_value());
    MIRAGE_CHECK(observation->full);
    MIRAGE_CHECK(observation->ranges.empty());
    MIRAGE_CHECK(observation->version == v2);

    // The record is bounded: the LRU evicts the oldest entries beyond 128.
    for (std::size_t i = 0; i < 200; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "lru-%03zu.txt", i);
        session.remember_full(root / name, v1);
    }
    MIRAGE_CHECK(!session.observation_of(root / "lru-000.txt").has_value());
    MIRAGE_CHECK(!session.observation_of(root / "lru-071.txt").has_value());
    MIRAGE_CHECK(session.observation_of(root / "lru-072.txt").has_value());
    MIRAGE_CHECK(session.observation_of(root / "lru-199.txt").has_value());

    // observation_of refreshes the LRU clock: a queried survivor outlives a
    // wave of newer inserts.
    MIRAGE_CHECK(session.observation_of(root / "lru-072.txt").has_value());
    for (std::size_t i = 200; i < 260; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "lru-%03zu.txt", i);
        session.remember_full(root / name, v1);
    }
    MIRAGE_CHECK(session.observation_of(root / "lru-072.txt").has_value());
    MIRAGE_CHECK(!session.observation_of(root / "lru-073.txt").has_value());
}

} // namespace

int main() {
    TempWorkspace workspace;
    scenario_disabled_group_registers_nothing(workspace.root());
    scenario_provider_without_capabilities_registers_nothing();
    scenario_read_only_environment_registers_read_only(workspace.root());
    scenario_full_environment_registers_four_tools(workspace.root());
    scenario_read_paths_and_facts(workspace.root());
    scenario_read_requires_workspace_for_relative_paths(workspace.root());
    scenario_write_not_observed_discipline(workspace.root());
    scenario_edit_requires_prior_read(workspace.root());
    scenario_denied_gate_zero_side_effects(workspace.root());
    scenario_null_gate_denies_everything(workspace.root());
    scenario_exec_tool_facts(workspace.root());
    scenario_session_state_unit_semantics(workspace.root());
    return mirage::testing::finish("basic_agent_toolset_test");
}
