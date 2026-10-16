// M10-07 (DEC-095): the exec/read/write/edit session toolset. Thin on
// purpose — contracts and budgets live in the desktop layer, commit and
// execution mechanics in the platform backend; this file maps tool
// arguments onto the provider calls, holds the session observation record
// the host owns on the model's behalf, and maps provider failures onto the
// pinned error vocabulary. Handlers never throw and never touch the OS
// directly (integration does not call OS file APIs).

#include <mirage/integration/basic_agent_toolset.hpp>

#include <mira/json.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace mirage::integration {
namespace {

using mira::Error;
using mira::ErrorCode;
using mira::JsonValue;
using mira::Result;

using mirage::desktop::CancelToken;
using mirage::desktop::FileEditOutcome;
using mirage::desktop::FileModifyLimits;
using mirage::desktop::FileObservation;
using mirage::desktop::FilesystemCapabilities;
using mirage::desktop::FilesystemProvider;
using mirage::desktop::FileVersion;
using mirage::desktop::ObservedByteRange;
using mirage::desktop::ProcessLimits;
using mirage::desktop::ProcessOutcome;
using mirage::desktop::ProcessProvider;
using mirage::desktop::ProcessRequest;
using mirage::desktop::ProviderError;
using mirage::desktop::TextWindowLimits;
using mirage::desktop::TextWindowOutcome;

/// Service authorize seam (DesktopToolContext::authorize), aliased for the
/// value-captured handler state.
using AuthorizeFn = std::function<bool(const std::string &, const std::string &,
                                       const AtomCancelProbe &, const std::string &,
                                       const std::string &)>;

/// Frozen DEC-095 budget table (M10-02): read windows and exec previews.
constexpr std::size_t kReadWindowBytes = 50u << 10;
constexpr std::size_t kReadWindowLines = 2000;
constexpr std::int64_t kReadScanBytes = 16ll << 20;
constexpr std::int64_t kReadScanSeconds = 5;
constexpr std::size_t kModifyContentBytes = 1u << 20;
constexpr std::size_t kPreviewLines = 2000;

Error basic_error(ErrorCode code, std::string message) {
    Error error;
    error.code = code;
    error.domain = "mirage.tool";
    error.safe_message = std::move(message);
    return error;
}

/// Provider failures keep their stable code in the safe message so a failed
/// tool record stays actionable. The M10 codes join the desktop-atom
/// mapping table (DEC-095 decision 9).
Error provider_failure(const ProviderError &error) {
    ErrorCode code = ErrorCode::PlatformError;
    if (error.code == "cancelled") {
        code = ErrorCode::Cancelled;
    } else if (error.code == "invalid_argument" || error.code == "workspace_required" ||
               error.code == "not_found" || error.code == "not_observed" ||
               error.code == "stale_file" || error.code == "match_not_found" ||
               error.code == "ambiguous_match" || error.code == "line_too_long" ||
               error.code == "already_exists") {
        code = ErrorCode::InvalidArgument;
    } else if (error.code == "file_too_large" || error.code == "busy" ||
               error.code == "capacity_exceeded" || error.code == "scan_budget_exceeded" ||
               error.code == "result_too_large") {
        code = ErrorCode::ResourceExhausted;
    } else if (error.code == "not_regular_file" || error.code == "unsupported_encoding" ||
               error.code == "unsupported_platform" || error.code == "output_expired") {
        code = ErrorCode::UnsupportedCapability;
    } else if (error.code == "permission_denied") {
        code = ErrorCode::PermissionDenied;
    } else if (error.code == "deadline_exceeded") {
        code = ErrorCode::DeadlineExceeded;
    }
    return basic_error(code, error.code + ": " + error.message);
}

/// Judges the capability through the value-captured gate seam (null denies,
/// fail closed — the desktop atom precedent).
bool authorized(const AtomPermissionGate &gate, const AuthorizeFn &authorize,
                const char *capability, const std::string &resource,
                const mira::OperationContext &context) {
    const auto probe = [&context] {
        return context.cancelled() || context.cancelled_or_expired(mira::Timestamp::now());
    };
    if (authorize) {
        return authorize(capability, resource, probe, context.task.to_string(),
                         context.operation.to_string());
    }
    if (!gate) {
        return false;
    }
    return gate(capability, resource, probe);
}

Error permission_denied(const char *capability) {
    return basic_error(ErrorCode::PermissionDenied,
                       std::string("permission denied by policy: ") + capability);
}

JsonValue json_string(std::string value) { return JsonValue{std::move(value)}; }
JsonValue json_bool(bool value) { return JsonValue{value}; }
JsonValue json_int(std::int64_t value) { return JsonValue{value}; }

JsonValue string_schema(const char *description) {
    JsonValue::Object object;
    object.emplace_back("type", json_string("string"));
    object.emplace_back("description", json_string(description));
    return JsonValue{std::move(object)};
}

JsonValue integer_schema(std::int64_t minimum, std::int64_t maximum, const char *description) {
    JsonValue::Object object;
    object.emplace_back("type", json_string("integer"));
    object.emplace_back("minimum", json_int(minimum));
    object.emplace_back("maximum", json_int(maximum));
    object.emplace_back("description", json_string(description));
    return JsonValue{std::move(object)};
}

JsonValue boolean_schema(const char *description) {
    JsonValue::Object object;
    object.emplace_back("type", json_string("boolean"));
    object.emplace_back("description", json_string(description));
    return JsonValue{std::move(object)};
}

JsonValue object_schema(JsonValue::Object properties, std::vector<std::string> required) {
    JsonValue::Object object;
    object.emplace_back("type", json_string("object"));
    object.emplace_back("properties", JsonValue{std::move(properties)});
    JsonValue::Array required_array;
    for (auto &name : required) {
        required_array.push_back(json_string(std::move(name)));
    }
    object.emplace_back("required", JsonValue{std::move(required_array)});
    return JsonValue{std::move(object)};
}

std::string string_member(const JsonValue &arguments, const char *name) {
    const auto *value = arguments.find(name);
    return value != nullptr && value->is_string() ? *value->as_string() : std::string();
}

std::optional<std::int64_t> int_member(const JsonValue &arguments, const char *name) {
    const auto *value = arguments.find(name);
    if (value == nullptr || !value->is_integer()) {
        return std::nullopt;
    }
    return *value->as_integer();
}

bool bool_member(const JsonValue &arguments, const char *name, bool fallback) {
    const auto *value = arguments.find(name);
    return value != nullptr && value->is_boolean() ? *value->as_boolean() : fallback;
}

/// Resolves the tool path argument: absolute paths pass through; relative
/// paths require the session workspace (DEC-095: no service-cwd dependency,
/// no guessed home fallback).
std::optional<std::filesystem::path> resolve_path(const BasicAgentToolsOptions &options,
                                                  const std::string &raw, Error &error) {
    if (raw.empty()) {
        error = basic_error(ErrorCode::InvalidArgument, "path must not be empty");
        return std::nullopt;
    }
    const std::filesystem::path path(raw);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    if (options.workspace.empty()) {
        error = basic_error(
            ErrorCode::InvalidArgument,
            "workspace_required: no workspace is configured; only absolute paths are accepted");
        return std::nullopt;
    }
    return (options.workspace / path).lexically_normal();
}

/// Short model-facing version fact (digest prefix). The full digest stays
/// host-side; this is a change-detection hint only.
std::string version_hint(const FileVersion &version) {
    return version.content_digest.substr(0, 16);
}

JsonValue::Object file_facts(const std::filesystem::path &path, const FileVersion &version) {
    JsonValue::Object object;
    object.emplace_back("path", json_string(path.string()));
    object.emplace_back("file_size", json_int(static_cast<std::int64_t>(version.size)));
    if (version.digest_available()) {
        object.emplace_back("version", json_string(version_hint(version)));
    }
    return object;
}

/// Trims one preview stream to the frozen budget: bytes first, then lines;
/// a cut reports the remaining facts instead of a silent shrink.
std::string trim_preview(std::string content, std::size_t max_bytes, bool &truncated) {
    if (content.size() > max_bytes) {
        content.resize(max_bytes);
        truncated = true;
    }
    std::size_t lines = 0;
    std::size_t pos = 0;
    while ((pos = content.find('\n', pos)) != std::string::npos) {
        ++lines;
        if (lines >= kPreviewLines) {
            if (pos + 1 < content.size()) {
                content.resize(pos + 1);
                truncated = true;
            }
            break;
        }
        ++pos;
    }
    return content;
}

} // namespace

// ---------------------------------------------------------------------------
// BasicToolSessionState
// ---------------------------------------------------------------------------

struct BasicToolSessionState::Impl {
    struct Entry {
        FileObservation observation;
        std::uint64_t last_use = 0;
    };

    static void evict_bounded(std::map<std::filesystem::path, Entry> &entries) {
        while (entries.size() > BasicToolSessionState::kMaxEntries) {
            auto oldest = entries.begin();
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                if (it->second.last_use < oldest->second.last_use) {
                    oldest = it;
                }
            }
            entries.erase(oldest);
        }
    }

    /// Mutable so `observation_of` can honor the LRU contract on queries
    /// (a read path is a use; the clock never rewinds).
    mutable std::map<std::filesystem::path, Entry> entries;
    mutable std::uint64_t clock = 0;
};

BasicToolSessionState::BasicToolSessionState() : impl_(std::make_unique<Impl>()) {}
BasicToolSessionState::~BasicToolSessionState() = default;

void BasicToolSessionState::remember_full(const std::filesystem::path &path,
                                          const FileVersion &version) {
    FileObservation observation;
    observation.version = version;
    observation.full = true;
    impl_->entries[path] = Impl::Entry{std::move(observation), ++impl_->clock};
    Impl::evict_bounded(impl_->entries);
}

void BasicToolSessionState::record_window(const std::filesystem::path &path,
                                          const FileVersion &version, bool full,
                                          const ObservedByteRange &range) {
    if (!version.digest_available()) {
        return; // oversized files authorize nothing (DEC-095)
    }
    auto &entry = impl_->entries[path];
    if (entry.observation.version != version) {
        entry.observation = FileObservation{};
        entry.observation.version = version;
    }
    entry.last_use = ++impl_->clock;
    if (full) {
        entry.observation.full = true;
        entry.observation.ranges.clear();
    } else if (!entry.observation.full) {
        entry.observation.ranges.push_back(range);
    }
    Impl::evict_bounded(impl_->entries);
}

std::optional<FileObservation>
BasicToolSessionState::observation_of(const std::filesystem::path &path) const {
    const auto found = impl_->entries.find(path);
    if (found == impl_->entries.end()) {
        return std::nullopt;
    }
    found->second.last_use = ++impl_->clock;
    return found->second.observation;
}

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

void register_basic_agent_tools(mira::BuiltinToolRegistry &registry,
                                const DesktopToolContext &desktop,
                                BasicToolSessionState &session,
                                const BasicAgentToolsOptions &options,
                                const Decorator &decorate) {
    if (!options.enabled || !desktop.environment) {
        return;
    }
    auto *filesystem = desktop.environment->filesystem();
    auto *process = desktop.environment->process();
    const FilesystemCapabilities caps = filesystem != nullptr
                                            ? filesystem->capabilities()
                                            : FilesystemCapabilities{};
    // Value-captured gate seam: handlers outlive this frame; the provider
    // pointers stay valid because the environment outlives the registry.
    const AtomPermissionGate gate = desktop.gate;
    const AuthorizeFn authorize = desktop.authorize;
    auto *retention = desktop.output_retention;

    const auto register_tool = [&registry, &decorate](mira::BuiltinToolSpec spec,
                                                      mira::BuiltinToolHandler handler) {
        const std::string name = spec.wire_name;
        const auto registered =
            registry.register_tool(std::move(spec), decorate(name, std::move(handler)));
        if (!registered) {
            throw std::runtime_error("basic tool registration failed: " +
                                     registered.error().safe_message);
        }
    };

    // -- read ---------------------------------------------------------------
    if (filesystem != nullptr && caps.windowed_read) {
        mira::BuiltinToolSpec spec;
        spec.wire_name = "read";
        spec.description =
            "Reads a bounded window of a UTF-8 text file, or of retained command output from a "
            "previous exec in this session (output_ref). 1-based line numbers; continue with "
            "offset = next_offset of the previous result. Nothing is truncated silently: eof "
            "and next_offset facts tell what remains.";
        JsonValue::Object properties;
        properties.emplace_back(
            "path", string_schema("File path (absolute, or relative to the session workspace)"));
        properties.emplace_back(
            "output_ref",
            string_schema("Retained output reference from a previous exec result (same session)"));
        properties.emplace_back(
            "offset", integer_schema(
                          0, 1000000000,
                          "Lines to skip before the window (use the previous next_offset)"));
        properties.emplace_back("limit", integer_schema(1, static_cast<std::int64_t>(
                                                             kReadWindowLines),
                                                        "Maximum lines to return"));
        spec.parameters_schema = mira::JsonSchema{object_schema(std::move(properties), {})};
        mira::BuiltinToolHandler handler =
            [filesystem, retention, &session, options, gate, authorize](
                const JsonValue &arguments,
                const mira::OperationContext &context) -> Result<JsonValue> {
            if (context.cancelled()) {
                return basic_error(ErrorCode::Cancelled, "run cancelled before the tool ran");
            }
            const std::string path_argument = string_member(arguments, "path");
            const std::string output_ref = string_member(arguments, "output_ref");
            if (path_argument.empty() == output_ref.empty()) {
                return basic_error(ErrorCode::InvalidArgument,
                                   "provide exactly one of path or output_ref");
            }
            TextWindowLimits limits;
            limits.max_lines = kReadWindowLines;
            limits.max_bytes = kReadWindowBytes;
            limits.max_scan_bytes = static_cast<std::size_t>(kReadScanBytes);
            limits.max_scan_time = std::chrono::seconds(kReadScanSeconds);
            if (const auto limit = int_member(arguments, "limit")) {
                limits.max_lines = static_cast<std::size_t>(
                    std::clamp<std::int64_t>(*limit, 1,
                                             static_cast<std::int64_t>(kReadWindowLines)));
            }
            std::uint64_t offset = 0;
            if (const auto requested = int_member(arguments, "offset")) {
                offset = requested < 0 ? 0 : static_cast<std::uint64_t>(*requested);
            }
            const auto render_lines = [](const TextWindowOutcome &window) {
                JsonValue::Array lines;
                for (std::size_t index = 0; index < window.lines.size(); ++index) {
                    JsonValue::Object line;
                    line.emplace_back("number", json_int(static_cast<std::int64_t>(
                                                                    window.first_line) +
                                                         static_cast<std::int64_t>(index)));
                    line.emplace_back("text", json_string(window.lines[index]));
                    lines.push_back(JsonValue{std::move(line)});
                }
                JsonValue::Object result;
                result.emplace_back("lines", JsonValue{std::move(lines)});
                result.emplace_back("eof", json_bool(window.eof));
                result.emplace_back(
                    "next_offset", json_int(static_cast<std::int64_t>(window.next_offset)));
                if (window.total_lines) {
                    result.emplace_back("total_lines",
                                        json_int(static_cast<std::int64_t>(*window.total_lines)));
                }
                return result;
            };
            if (!output_ref.empty()) {
                if (retention == nullptr) {
                    return basic_error(ErrorCode::InvalidArgument,
                                       "unsupported_platform: output retention is not wired on "
                                       "this session; retained refs cannot be read");
                }
                const TextWindowOutcome window =
                    retention->read(context.session.to_string(), output_ref, offset, limits);
                if (!window.ok) {
                    return provider_failure(window.error);
                }
                JsonValue::Object result = render_lines(window);
                result.emplace_back("output_ref", json_string(output_ref));
                return JsonValue{std::move(result)};
            }
            Error error;
            const auto path = resolve_path(options, path_argument, error);
            if (!path) {
                return error;
            }
            if (!authorized(gate, authorize, "filesystem.read", path->string(), context)) {
                return permission_denied("filesystem.read");
            }
            const TextWindowOutcome window =
                filesystem->read_text_window(*path, offset, limits, CancelToken{});
            if (!window.ok) {
                return provider_failure(window.error);
            }
            // Full observation: window zero through eof of a digestable
            // file. Otherwise the raw byte range joins the record.
            session.record_window(*path, window.version,
                                  window.eof && offset == 0 && window.version.digest_available(),
                                  window.observed_range);
            JsonValue::Object result = file_facts(*path, window.version);
            JsonValue::Array lines;
            for (std::size_t index = 0; index < window.lines.size(); ++index) {
                JsonValue::Object line;
                line.emplace_back("number", json_int(static_cast<std::int64_t>(window.first_line) +
                                                     static_cast<std::int64_t>(index)));
                line.emplace_back("text", json_string(window.lines[index]));
                lines.push_back(JsonValue{std::move(line)});
            }
            result.emplace_back("lines", JsonValue{std::move(lines)});
            result.emplace_back("eof", json_bool(window.eof));
            result.emplace_back(
                "next_offset", json_int(static_cast<std::int64_t>(window.next_offset)));
            if (window.total_lines) {
                result.emplace_back("total_lines",
                                    json_int(static_cast<std::int64_t>(*window.total_lines)));
            }
            result.emplace_back("has_bom", json_bool(window.has_bom));
            result.emplace_back("has_crlf", json_bool(window.has_crlf));
            return JsonValue{std::move(result)};
        };
        register_tool(std::move(spec), std::move(handler));
    }

    // -- write --------------------------------------------------------------
    if (filesystem != nullptr && caps.write) {
        mira::BuiltinToolSpec spec;
        spec.wire_name = "write";
        spec.has_side_effects = true;
        spec.description =
            "Creates a file with the given content, or replaces an existing file that was fully "
            "read in this session. Refuses to overwrite unread files: read the target first, "
            "then write again.";
        JsonValue::Object properties;
        properties.emplace_back(
            "path", string_schema("File path (absolute, or relative to the session workspace)"));
        properties.emplace_back("content", string_schema("Full new file content (UTF-8)"));
        spec.parameters_schema =
            mira::JsonSchema{object_schema(std::move(properties), {"path", "content"})};
        mira::BuiltinToolHandler handler =
            [filesystem, &session, options, gate, authorize](
                const JsonValue &arguments,
                const mira::OperationContext &context) -> Result<JsonValue> {
            if (context.cancelled()) {
                return basic_error(ErrorCode::Cancelled, "run cancelled before the tool ran");
            }
            Error error;
            const auto path = resolve_path(options, string_member(arguments, "path"), error);
            if (!path) {
                return error;
            }
            if (!authorized(gate, authorize, "filesystem.write", path->string(), context)) {
                return permission_denied("filesystem.write");
            }
            const std::string content = string_member(arguments, "content");
            if (content.size() > kModifyContentBytes) {
                return basic_error(ErrorCode::ResourceExhausted,
                                   "file_too_large: content exceeds the modify budget of " +
                                       std::to_string(kModifyContentBytes) + " bytes");
            }
            FileModifyLimits limits;
            limits.max_content_bytes = kModifyContentBytes;
            // A full observation from this session authorizes the replace;
            // anything else stays create-if-absent (DEC-095 decision 3).
            const auto observation = session.observation_of(*path);
            mirage::desktop::FileWriteOutcome outcome;
            if (observation && observation->full &&
                observation->version.digest_available()) {
                outcome = filesystem->replace_file(*path, content, *observation, limits,
                                                   CancelToken{});
            } else {
                outcome = filesystem->create_file(*path, content, limits, CancelToken{});
                if (!outcome.ok && outcome.error.code == "already_exists") {
                    return basic_error(
                        ErrorCode::InvalidArgument,
                        "not_observed: the target exists but was not read in this session; "
                        "read it first, then write to replace it");
                }
            }
            if (!outcome.ok) {
                return provider_failure(outcome.error);
            }
            if (outcome.applied) {
                session.remember_full(*path, outcome.version);
            }
            JsonValue::Object result = file_facts(*path, outcome.version);
            result.emplace_back("applied", json_bool(outcome.applied));
            result.emplace_back("created", json_bool(outcome.created));
            result.emplace_back("replaced", json_bool(outcome.replaced));
            result.emplace_back(
                "bytes_written", json_int(static_cast<std::int64_t>(outcome.bytes_written)));
            return JsonValue{std::move(result)};
        };
        register_tool(std::move(spec), std::move(handler));
    }

    // -- edit ---------------------------------------------------------------
    if (filesystem != nullptr && caps.edit) {
        mira::BuiltinToolSpec spec;
        spec.wire_name = "edit";
        spec.has_side_effects = true;
        spec.description =
            "Replaces an exact literal string in a file that was read in this session. The "
            "match must be unique unless replace_all is true; zero matches and ambiguous "
            "matches change nothing. Line endings are matched as '\\n'; keep the file's own "
            "convention for new text.";
        JsonValue::Object properties;
        properties.emplace_back(
            "path", string_schema("File path (absolute, or relative to the session workspace)"));
        properties.emplace_back("old_string", string_schema("Exact text to replace"));
        properties.emplace_back("new_string",
                                string_schema("Replacement text (empty deletes)"));
        properties.emplace_back("replace_all",
                                boolean_schema("Replace every occurrence (default false)"));
        spec.parameters_schema = mira::JsonSchema{object_schema(
            std::move(properties), {"path", "old_string", "new_string"})};
        mira::BuiltinToolHandler handler =
            [filesystem, &session, options, gate, authorize](
                const JsonValue &arguments,
                const mira::OperationContext &context) -> Result<JsonValue> {
            if (context.cancelled()) {
                return basic_error(ErrorCode::Cancelled, "run cancelled before the tool ran");
            }
            Error error;
            const auto path = resolve_path(options, string_member(arguments, "path"), error);
            if (!path) {
                return error;
            }
            if (!authorized(gate, authorize, "filesystem.write", path->string(), context)) {
                return permission_denied("filesystem.write");
            }
            const auto observation = session.observation_of(*path);
            if (!observation) {
                return basic_error(ErrorCode::InvalidArgument,
                                   "not_observed: read the file before editing it");
            }
            FileModifyLimits limits;
            limits.max_content_bytes = kModifyContentBytes;
            const FileEditOutcome outcome = filesystem->edit_file(
                *path, string_member(arguments, "old_string"),
                string_member(arguments, "new_string"),
                bool_member(arguments, "replace_all", false), *observation, limits,
                CancelToken{});
            if (!outcome.ok) {
                return provider_failure(outcome.error);
            }
            if (outcome.applied) {
                session.remember_full(*path, outcome.version);
            }
            JsonValue::Object result = file_facts(*path, outcome.version);
            result.emplace_back("applied", json_bool(outcome.applied));
            result.emplace_back(
                "replacements", json_int(static_cast<std::int64_t>(outcome.replacements)));
            return JsonValue{std::move(result)};
        };
        register_tool(std::move(spec), std::move(handler));
    }

    // -- exec ---------------------------------------------------------------
    if (process != nullptr && process->supports_working_directory() && options.exec_enabled &&
        !options.workspace.empty()) {
        mira::BuiltinToolSpec spec;
        spec.wire_name = "exec";
        spec.has_side_effects = true;
        spec.description =
            "Runs one shell command in the session workspace with the user's own permissions "
            "(no sandbox). A non-zero exit code is a result, not an error: check exit_code. "
            "Timeout and cancellation end the whole process group and report the output so "
            "far.";
        JsonValue::Object properties;
        properties.emplace_back("command", string_schema("Shell command line"));
        properties.emplace_back(
            "workdir", string_schema("Working directory relative to the session workspace"));
        properties.emplace_back("timeout_ms", integer_schema(
                                                   1000, static_cast<std::int64_t>(std::chrono::milliseconds(
                                                             options.exec_timeout_max)
                                                             .count()),
                                                   "Wall-clock budget in milliseconds"));
        spec.parameters_schema =
            mira::JsonSchema{object_schema(std::move(properties), {"command"})};
        mira::BuiltinToolHandler handler =
            [process, retention, options, gate, authorize](
                const JsonValue &arguments,
                const mira::OperationContext &context) -> Result<JsonValue> {
            if (context.cancelled()) {
                return basic_error(ErrorCode::Cancelled, "run cancelled before the tool ran");
            }
            const std::string command = string_member(arguments, "command");
            std::filesystem::path workdir = options.workspace;
            if (const auto requested = string_member(arguments, "workdir"); !requested.empty()) {
                const std::filesystem::path relative(requested);
                workdir = relative.is_absolute() ? relative
                                                 : (options.workspace / relative).lexically_normal();
            }
            if (!authorized(gate, authorize, "process.execute", command, context)) {
                return permission_denied("process.execute");
            }
            ProcessLimits limits;
            limits.timeout = options.exec_timeout_default;
            if (const auto timeout = int_member(arguments, "timeout_ms")) {
                limits.timeout = std::chrono::milliseconds(std::clamp<std::int64_t>(
                    *timeout, 1000,
                    static_cast<std::int64_t>(std::chrono::milliseconds(
                                                  options.exec_timeout_max)
                                                  .count())));
            }
            limits.max_output_bytes = options.exec_preview_bytes;
            limits.max_command_bytes = 64u << 10;
            // M10-06 (DEC-095): retention is admitted before any process is
            // created; an exhausted budget refuses the execution instead of
            // starting a command whose output could not be honored.
            std::shared_ptr<BasicOutputStreams> streams;
            if (retention != nullptr) {
                std::string refusal;
                streams = retention->open(context.session.to_string(), &refusal);
                if (!streams) {
                    return basic_error(
                        ErrorCode::ResourceExhausted,
                        refusal + ": output retention budgets are spent; read or expire "
                                  "retained output before running more commands");
                }
            }
            ProcessRequest request;
            request.command = command;
            request.working_directory = workdir;
            request.output_sink = streams.get();
            const ProcessOutcome outcome = process->execute_request(request, limits, CancelToken{});
            BasicOutputStreamFacts facts;
            if (streams) {
                facts = streams->settle(); // terminal state fixes refs and expiry
            }
            // Cancelled / timed-out commands still carry their output and
            // termination facts as a result (DEC-095 decision 5); only
            // spawn/reap/I-O failures become failed records.
            if (!outcome.ok && !outcome.cancelled && !outcome.timed_out) {
                return provider_failure(outcome.error);
            }
            bool preview_truncated = outcome.output_truncated;
            std::string standard_output =
                trim_preview(outcome.standard_output, options.exec_preview_bytes,
                             preview_truncated);
            std::string standard_error =
                trim_preview(outcome.standard_error, options.exec_preview_bytes,
                             preview_truncated);
            JsonValue::Object result;
            result.emplace_back(
                "exit_code", json_int(outcome.exited_normally ? outcome.exit_code : -1));
            std::string terminated = "exit";
            if (outcome.cancelled) {
                terminated = "cancelled";
            } else if (outcome.timed_out) {
                terminated = "timeout";
            } else if (!outcome.exited_normally) {
                terminated = "signal";
            }
            result.emplace_back("terminated", json_string(terminated));
            result.emplace_back("shell", json_string("sh"));
            result.emplace_back("cwd", json_string(workdir.string()));
            result.emplace_back("stdout", json_string(std::move(standard_output)));
            result.emplace_back("stderr", json_string(std::move(standard_error)));
            result.emplace_back("output_truncated", json_bool(preview_truncated));
            if (streams) {
                result.emplace_back("stdout_ref", json_string(facts.stdout_ref));
                result.emplace_back("stderr_ref", json_string(facts.stderr_ref));
                result.emplace_back(
                    "retained_stdout_bytes",
                    json_int(static_cast<std::int64_t>(facts.stdout_bytes)));
                result.emplace_back(
                    "retained_stderr_bytes",
                    json_int(static_cast<std::int64_t>(facts.stderr_bytes)));
                result.emplace_back("retained_truncated",
                                    json_bool(facts.stdout_truncated || facts.stderr_truncated));
            }
            return JsonValue{std::move(result)};
        };
        register_tool(std::move(spec), std::move(handler));
    }
}

} // namespace mirage::integration
