#pragma once

// In-memory DesktopEnvironment for contract and runtime tests (M2-01).
// Deterministic: no threads, no real platform calls. It enforces the same
// contracts real backends must honor — budgets refuse instead of truncating,
// cancellation is observed before effects, invalid arguments are rejected
// before any state changes — so tests can exercise negative paths without a
// desktop. Failure injection is explicit per provider via FakeFailures.

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/path_scope.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mirage::testing {

/// Explicit failure injection knobs; every knob maps to one negative
/// scenario the fake cannot reach with in-memory state alone.
struct FakeFailures {
    bool window_list_error = false;
    bool capture_error = false;
    bool clipboard_unsupported_content = false;
    bool application_terminate_stuck = false;
    bool notify_error = false;
    bool filesystem_error = false;
    bool snapshot_error = false;
};

class FakeDesktopEnvironment final : public mirage::desktop::DesktopEnvironment {
  public:
    mirage::desktop::EnvironmentInfo info() const override { return {"fake", "test"}; }

    // ---- provider accessors (all present) ----

    mirage::desktop::FilesystemProvider *filesystem() override { return &filesystem_; }
    mirage::desktop::ProcessProvider *process() override { return &process_; }
    mirage::desktop::ApplicationProvider *application() override { return &application_; }
    mirage::desktop::WindowProvider *window() override { return &window_; }
    mirage::desktop::AccessibilityProvider *accessibility() override { return &accessibility_; }
    mirage::desktop::ScreenProvider *screen() override { return &screen_; }
    mirage::desktop::InputProvider *input() override { return &input_; }
    mirage::desktop::ClipboardProvider *clipboard() override { return &clipboard_; }
    mirage::desktop::NotificationProvider *notification() override { return &notification_; }

    // ---- test-visible state ----

    /// Window table; id is "w1", "w2", ... in insertion order.
    std::vector<mirage::desktop::WindowInfo> windows;
    /// Snapshot served per window id; a window without an entry reports
    /// "unsupported_window" (no accessibility tree).
    std::map<std::string, mirage::desktop::SemanticSnapshot> snapshots;
    std::vector<mirage::desktop::DisplayInfo> displays;
    std::vector<mirage::desktop::ApplicationInfo> applications;
    std::map<std::string, std::string> application_instances; ///< app id -> instance id
    std::string clipboard_text;
    bool clipboard_has_text = false;
    std::vector<std::pair<std::string, std::string>> notifications; ///< title, body
    std::vector<std::string> input_log;                             ///< human-readable actions
    FakeFailures failures;

    // ---- accessibility action test surface ----

    /// Window id the latest successful snapshot was taken against.
    std::string active_window_id;
    /// Refs issued by the latest snapshot (registry semantics: a fresh
    /// snapshot replaces the set).
    std::set<std::string> current_refs;
    /// Refs that were "activated" / written via semantic actions.
    std::vector<std::string> activated_refs;
    std::vector<std::pair<std::string, std::string>> text_writes; ///< ref -> new text
    /// Refs that expose a semantic action / editable text.
    std::vector<std::string> actionable_refs;
    std::vector<std::string> editable_refs;

    // ---- filesystem test surface (in-memory files) ----

    std::map<std::string, std::string> files; ///< path -> content

    // ---- process test surface (scripted outcomes) ----

    /// Outcome served for the next execute() call, then reset to a clean
    /// success so unscripted calls behave like a trivially-successful shell.
    mirage::desktop::ProcessOutcome next_process_outcome = default_process_outcome();

    static mirage::desktop::ProcessOutcome default_process_outcome() {
        mirage::desktop::ProcessOutcome outcome;
        outcome.ok = true;
        outcome.exited_normally = true;
        outcome.exit_code = 0;
        return outcome;
    }

  private:
    class FakeFilesystem final : public mirage::desktop::FilesystemProvider {
      public:
        explicit FakeFilesystem(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::FileReadOutcome
        read_text_file(const std::filesystem::path &path,
                       const mirage::desktop::FileReadLimits &limits,
                       const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::FileReadOutcome outcome;
            if (cancel.cancelled()) {
                outcome.error = {"cancelled", "read cancelled"};
                return outcome;
            }
            if (env_.failures.filesystem_error) {
                outcome.error = {"io_error", "injected filesystem failure"};
                return outcome;
            }
            if (!scope_.contains(path)) {
                outcome.error = {"permission_denied", "path outside declared read scope"};
                return outcome;
            }
            const auto it = env_.files.find(path.string());
            if (it == env_.files.end()) {
                outcome.error = {"not_found", "no such file"};
                return outcome;
            }
            if (it->second.size() > limits.max_bytes) {
                outcome.error = {"file_too_large", "file exceeds the read budget"};
                return outcome;
            }
            outcome.ok = true;
            outcome.content = it->second;
            return outcome;
        }

        mirage::desktop::PathScope &scope() { return scope_; }

      private:
        FakeDesktopEnvironment &env_;
        mirage::desktop::PathScope scope_;
    };

    class FakeProcess final : public mirage::desktop::ProcessProvider {
      public:
        explicit FakeProcess(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::ProcessOutcome
        execute(const std::string &command, const mirage::desktop::ProcessLimits &limits,
                const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::ProcessOutcome outcome = env_.next_process_outcome;
            env_.next_process_outcome = default_process_outcome();
            if (command.size() > limits.max_command_bytes) {
                outcome.ok = false;
                outcome.error = {"invalid_argument", "command exceeds the length budget"};
                return outcome;
            }
            if (cancel.cancelled() && outcome.ok) {
                outcome.ok = false;
                outcome.cancelled = true;
                outcome.error = {"cancelled", "execution cancelled"};
            }
            return outcome;
        }

      private:
        FakeDesktopEnvironment &env_;
    };

    class FakeApplication final : public mirage::desktop::ApplicationProvider {
      public:
        explicit FakeApplication(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::ApplicationListOutcome
        list_applications(const mirage::desktop::ApplicationListLimits &limits,
                          const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::ApplicationListOutcome outcome;
            if (cancel.cancelled()) {
                outcome.error = {"cancelled", "listing cancelled"};
                return outcome;
            }
            if (env_.applications.size() > limits.max_applications) {
                outcome.error = {"result_too_large", "application table exceeds the budget"};
                return outcome;
            }
            outcome.ok = true;
            outcome.applications = env_.applications;
            return outcome;
        }

        mirage::desktop::ApplicationQueryOutcome
        running_state(const std::string &application_id,
                      const mirage::desktop::CancelToken &) override {
            mirage::desktop::ApplicationQueryOutcome outcome;
            if (application_id.empty()) {
                outcome.error = {"invalid_argument", "application id must not be empty"};
                return outcome;
            }
            if (!find_application(application_id)) {
                outcome.error = {"not_found", "unknown application id"};
                return outcome;
            }
            outcome.ok = true;
            const auto instance = env_.application_instances.find(application_id);
            if (instance != env_.application_instances.end()) {
                outcome.running = true;
                outcome.instance_id = instance->second;
            }
            return outcome;
        }

        mirage::desktop::ApplicationLaunchOutcome
        launch(const std::string &application_id,
               const mirage::desktop::ApplicationLaunchLimits &limits,
               const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::ApplicationLaunchOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "launch cancelled"};
                return outcome;
            }
            if (limits.timeout.count() <= 0) {
                outcome.error = {"invalid_argument", "timeout must be positive"};
                return outcome;
            }
            if (application_id.empty()) {
                outcome.error = {"invalid_argument", "application id must not be empty"};
                return outcome;
            }
            if (application_id.size() > limits.max_command_bytes) {
                outcome.error = {"invalid_argument", "application id exceeds the length budget"};
                return outcome;
            }
            if (!find_application(application_id)) {
                outcome.error = {"not_found", "unknown application id"};
                return outcome;
            }
            if (env_.application_instances.count(application_id) != 0) {
                outcome.error = {"already_running", "application already has a live instance"};
                return outcome;
            }
            const std::string instance = "i-" + application_id;
            env_.application_instances.emplace(application_id, instance);
            mark_running(application_id, true);
            outcome.ok = true;
            outcome.instance_id = instance;
            return outcome;
        }

        mirage::desktop::ApplicationLaunchOutcome
        terminate(const std::string &application_id,
                  const mirage::desktop::ApplicationLaunchLimits &limits,
                  const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::ApplicationLaunchOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "termination cancelled"};
                return outcome;
            }
            if (limits.timeout.count() <= 0) {
                outcome.error = {"invalid_argument", "timeout must be positive"};
                return outcome;
            }
            if (application_id.empty()) {
                outcome.error = {"invalid_argument", "application id must not be empty"};
                return outcome;
            }
            if (!find_application(application_id)) {
                outcome.error = {"not_found", "unknown application id"};
                return outcome;
            }
            if (env_.application_instances.count(application_id) == 0) {
                outcome.error = {"not_found", "application has no running instance"};
                return outcome;
            }
            if (env_.failures.application_terminate_stuck) {
                outcome.error = {"deadline_exceeded",
                                 "instance ignored the termination request within the budget"};
                return outcome;
            }
            env_.application_instances.erase(application_id);
            mark_running(application_id, false);
            outcome.ok = true;
            outcome.instance_id = application_id;
            return outcome;
        }

      private:
        const mirage::desktop::ApplicationInfo *find_application(const std::string &id) const {
            for (const auto &application : env_.applications) {
                if (application.id == id) {
                    return &application;
                }
            }
            return nullptr;
        }

        void mark_running(const std::string &id, bool running) {
            for (auto &application : env_.applications) {
                if (application.id == id) {
                    application.running = running;
                }
            }
        }

        FakeDesktopEnvironment &env_;
    };

    class FakeWindow final : public mirage::desktop::WindowProvider {
      public:
        explicit FakeWindow(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::WindowListOutcome
        list_windows(const mirage::desktop::WindowListLimits &limits,
                     const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::WindowListOutcome outcome;
            if (cancel.cancelled()) {
                outcome.error = {"cancelled", "enumeration cancelled"};
                return outcome;
            }
            if (env_.failures.window_list_error) {
                outcome.error = {"io_error", "injected window enumeration failure"};
                return outcome;
            }
            if (env_.windows.size() > limits.max_windows) {
                outcome.error = {"result_too_large", "window table exceeds the budget"};
                return outcome;
            }
            outcome.ok = true;
            outcome.windows = env_.windows;
            return outcome;
        }

        mirage::desktop::WindowQueryOutcome
        front_window(const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::WindowQueryOutcome outcome;
            if (cancel.cancelled()) {
                outcome.error = {"cancelled", "query cancelled"};
                return outcome;
            }
            outcome.ok = true;
            for (const auto &window : env_.windows) {
                if (window.focused) {
                    outcome.found = true;
                    outcome.window = window;
                    break;
                }
            }
            return outcome;
        }

        mirage::desktop::WindowActionOutcome
        activate(const std::string &window_id,
                 const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::WindowActionOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "activation cancelled"};
                return outcome;
            }
            if (window_id.empty()) {
                outcome.error = {"invalid_argument", "window id must not be empty"};
                return outcome;
            }
            bool hit = false;
            for (auto &window : env_.windows) {
                if (window.id == window_id) {
                    hit = true;
                }
            }
            if (!hit) {
                outcome.error = {"not_found", "unknown window id"};
                return outcome;
            }
            for (auto &window : env_.windows) {
                window.focused = window.id == window_id;
            }
            outcome.ok = true;
            return outcome;
        }

      private:
        FakeDesktopEnvironment &env_;
    };

    class FakeAccessibility final : public mirage::desktop::AccessibilityProvider {
      public:
        explicit FakeAccessibility(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::SnapshotOutcome
        semantic_snapshot(const std::string &window_id,
                          const mirage::desktop::SemanticSnapshotLimits &limits,
                          const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::SnapshotOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "snapshot cancelled"};
                return outcome;
            }
            if (env_.failures.snapshot_error) {
                outcome.error = {"io_error", "injected accessibility failure"};
                return outcome;
            }
            if (window_id.empty()) {
                outcome.error = {"invalid_argument", "window id must not be empty"};
                return outcome;
            }
            const auto it = env_.snapshots.find(window_id);
            if (it == env_.snapshots.end()) {
                outcome.error = {"unsupported_window", "window has no accessibility tree"};
                return outcome;
            }
            if (it->second.nodes.size() > limits.max_nodes) {
                outcome.error = {"snapshot_too_large", "snapshot exceeds the node budget"};
                return outcome;
            }
            // A fresh snapshot replaces the reference registry (stale
            // handles from older snapshots stop resolving).
            env_.active_window_id = window_id;
            env_.current_refs.clear();
            for (const auto &node : it->second.nodes) {
                env_.current_refs.insert(node.ref);
            }
            outcome.ok = true;
            outcome.snapshot = it->second;
            return outcome;
        }

        mirage::desktop::ElementActionOutcome
        activate_element(const mirage::desktop::ElementTarget &target,
                         const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::ElementActionOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "activation cancelled"};
                return outcome;
            }
            if (!target.visual.ocr_text.empty() || !target.visual.template_id.empty() ||
                !target.spatial.relative_to.id.empty() || target.raw.x != 0 || target.raw.y != 0) {
                outcome.error = {"unsupported_hint", "hint is not accessibility-resolvable"};
                return outcome;
            }
            const std::optional<std::string> resolved = resolve_locked(target);
            if (!resolved.has_value()) {
                // No hint group set at all: invalid before any lookup
                // (contract, element_reference.hpp).
                outcome.error = {"invalid_argument", "target carries no hint"};
                return outcome;
            }
            if (resolved->empty()) {
                outcome.error = {"not_found", "target does not resolve to a current element"};
                return outcome;
            }
            const std::string &ref = *resolved;
            if (std::find(env_.actionable_refs.begin(), env_.actionable_refs.end(), ref) ==
                env_.actionable_refs.end()) {
                outcome.error = {"unsupported_element", "element exposes no semantic action"};
                return outcome;
            }
            env_.activated_refs.push_back(ref);
            outcome.ok = true;
            return outcome;
        }

        mirage::desktop::ElementActionOutcome
        set_text(const mirage::desktop::ElementTarget &target, const std::string &text,
                 const mirage::desktop::InputLimits &limits,
                 const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::ElementActionOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "text input cancelled"};
                return outcome;
            }
            if (text.size() > limits.max_text_bytes) {
                outcome.error = {"invalid_argument", "text exceeds the length budget"};
                return outcome;
            }
            if (!mirage::desktop::is_valid_utf8(text)) {
                outcome.error = {"invalid_argument", "text must be UTF-8"};
                return outcome;
            }
            const std::optional<std::string> resolved = resolve_locked(target);
            if (!resolved.has_value()) {
                outcome.error = {"invalid_argument", "target carries no hint"};
                return outcome;
            }
            if (resolved->empty()) {
                outcome.error = {"not_found", "target does not resolve to a current element"};
                return outcome;
            }
            const std::string &ref = *resolved;
            if (std::find(env_.editable_refs.begin(), env_.editable_refs.end(), ref) ==
                env_.editable_refs.end()) {
                outcome.error = {"unsupported_element", "element exposes no editable text"};
                return outcome;
            }
            env_.text_writes.emplace_back(ref, text);
            outcome.ok = true;
            return outcome;
        }

      private:
        /// Contract resolution order: reference -> semantic -> structural
        /// ("/role/name/..." pairs walked from a root node). nullopt means
        /// the target carries no hint group at all (invalid_argument per the
        /// contract); "" means a hint was set but does not resolve
        /// (not_found).
        std::optional<std::string>
        resolve_locked(const mirage::desktop::ElementTarget &target) const {
            if (!target.reference.id.empty()) {
                return env_.current_refs.count(target.reference.id) != 0
                           ? std::optional<std::string>(target.reference.id)
                           : std::optional<std::string>(std::string());
            }
            const auto active = env_.snapshots.find(env_.active_window_id);
            if (active == env_.snapshots.end()) {
                return std::string();
            }
            if (!target.semantic.role.empty() || !target.semantic.name.empty()) {
                for (const auto &node : active->second.nodes) {
                    const bool role_ok =
                        target.semantic.role.empty() || node.role == target.semantic.role;
                    const bool name_ok =
                        target.semantic.name.empty() || node.name == target.semantic.name;
                    if (role_ok && name_ok) {
                        return node.ref;
                    }
                }
                return std::string();
            }
            if (!target.structural.path.empty()) {
                return resolve_structural(active->second, target.structural.path);
            }
            return std::nullopt;
        }

        static std::string resolve_structural(const mirage::desktop::SemanticSnapshot &snapshot,
                                              const std::string &path) {
            std::vector<std::string> segments;
            std::string current;
            for (const char ch : path) {
                if (ch == '/') {
                    if (!current.empty()) {
                        segments.push_back(current);
                        current.clear();
                    }
                } else {
                    current.push_back(ch);
                }
            }
            if (!current.empty()) {
                segments.push_back(current);
            }
            if (segments.empty() || segments.size() % 2 != 0) {
                return {}; // path is role/name pairs from a root
            }
            for (std::size_t root = 0; root < snapshot.nodes.size(); ++root) {
                if (snapshot.nodes[root].parent != mirage::desktop::kNoParent) {
                    continue;
                }
                std::size_t cursor = root;
                bool matched = true;
                for (std::size_t s = 0; s + 1 < segments.size(); s += 2) {
                    const auto &node = snapshot.nodes[cursor];
                    if (node.role != segments[s] || node.name != segments[s + 1]) {
                        matched = false;
                        break;
                    }
                    if (s + 2 >= segments.size()) {
                        break;
                    }
                    // Step to the FIRST CHILD matching the next role/name
                    // pair (enumeration order), so sibling branches are
                    // scanned instead of walking only the first child.
                    bool stepped = false;
                    for (std::size_t j = 0; j < snapshot.nodes.size(); ++j) {
                        const auto &child = snapshot.nodes[j];
                        if (child.parent == cursor && child.role == segments[s + 2] &&
                            child.name == segments[s + 3]) {
                            cursor = j;
                            stepped = true;
                            break;
                        }
                    }
                    if (!stepped) {
                        matched = false;
                        break;
                    }
                }
                if (matched) {
                    return snapshot.nodes[cursor].ref;
                }
            }
            return {};
        }

        FakeDesktopEnvironment &env_;
    };

    class FakeScreen final : public mirage::desktop::ScreenProvider {
      public:
        explicit FakeScreen(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::DisplayListOutcome
        list_displays(const mirage::desktop::DisplayListLimits &limits,
                      const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::DisplayListOutcome outcome;
            if (cancel.cancelled()) {
                outcome.error = {"cancelled", "enumeration cancelled"};
                return outcome;
            }
            if (env_.displays.size() > limits.max_displays) {
                outcome.error = {"result_too_large", "display table exceeds the budget"};
                return outcome;
            }
            outcome.ok = true;
            outcome.displays = env_.displays;
            return outcome;
        }

        mirage::desktop::CaptureOutcome
        capture_display(const std::string &display_id, const mirage::desktop::CaptureLimits &limits,
                        const mirage::desktop::CancelToken &cancel) override {
            if (cancel.cancelled()) {
                return cancelled_capture();
            }
            for (const auto &display : env_.displays) {
                if (display.id == display_id) {
                    return make_frame(display.geometry, limits);
                }
            }
            mirage::desktop::CaptureOutcome outcome;
            outcome.error = {"not_found", "unknown display id"};
            return outcome;
        }

        mirage::desktop::CaptureOutcome
        capture_window(const std::string &window_id, const mirage::desktop::CaptureLimits &limits,
                       const mirage::desktop::CancelToken &cancel) override {
            if (cancel.cancelled()) {
                return cancelled_capture();
            }
            for (const auto &window : env_.windows) {
                if (window.id == window_id) {
                    return make_frame(window.geometry, limits);
                }
            }
            mirage::desktop::CaptureOutcome outcome;
            outcome.error = {"not_found", "unknown window id"};
            return outcome;
        }

        mirage::desktop::CaptureOutcome
        capture_roi(const mirage::desktop::WindowGeometry &roi,
                    const mirage::desktop::CaptureLimits &limits,
                    const mirage::desktop::CancelToken &cancel) override {
            if (cancel.cancelled()) {
                return cancelled_capture();
            }
            if (roi.width <= 0 || roi.height <= 0) {
                mirage::desktop::CaptureOutcome outcome;
                outcome.error = {"invalid_argument", "roi extents must be positive"};
                return outcome;
            }
            return make_frame(roi, limits);
        }

      private:
        FakeDesktopEnvironment &env_;

        static mirage::desktop::CaptureOutcome cancelled_capture() {
            mirage::desktop::CaptureOutcome outcome;
            outcome.cancelled = true;
            outcome.error = {"cancelled", "capture cancelled"};
            return outcome;
        }

        mirage::desktop::CaptureOutcome make_frame(const mirage::desktop::WindowGeometry &area,
                                                   const mirage::desktop::CaptureLimits &limits) {
            mirage::desktop::CaptureOutcome outcome;
            if (env_.failures.capture_error) {
                outcome.error = {"io_error", "injected capture failure"};
                return outcome;
            }
            const std::size_t stride = static_cast<std::size_t>(area.width) * 4u;
            const std::size_t bytes = stride * static_cast<std::size_t>(area.height);
            if (bytes > limits.max_bytes) {
                outcome.error = {"capture_too_large", "capture exceeds the byte budget"};
                return outcome;
            }
            outcome.ok = true;
            outcome.frame.format = mirage::desktop::ImageFormat::Bgra8;
            outcome.frame.width = area.width;
            outcome.frame.height = area.height;
            outcome.frame.stride = stride;
            outcome.frame.pixels.assign(bytes, env_.capture_pixel);
            return outcome;
        }
    };

    class FakeInput final : public mirage::desktop::InputProvider {
      public:
        explicit FakeInput(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::InputOutcome
        inject_key(const mirage::desktop::KeySym &key, bool pressed,
                   const mirage::desktop::InputLimits &limits,
                   const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::InputOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "injection cancelled"};
                return outcome;
            }
            if (limits.timeout.count() <= 0) {
                outcome.error = {"invalid_argument", "timeout must be positive"};
                return outcome;
            }
            if (!mirage::desktop::is_valid_key_name(key.name)) {
                outcome.error = {"invalid_argument", "unknown key name"};
                return outcome;
            }
            env_.input_log.push_back((pressed ? "down " : "up ") + key.name);
            outcome.ok = true;
            return outcome;
        }

        mirage::desktop::InputOutcome
        type_text(const std::string &text, const mirage::desktop::InputLimits &limits,
                  const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::InputOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "injection cancelled"};
                return outcome;
            }
            if (limits.timeout.count() <= 0) {
                outcome.error = {"invalid_argument", "timeout must be positive"};
                return outcome;
            }
            if (text.size() > limits.max_text_bytes) {
                outcome.error = {"invalid_argument", "text exceeds the length budget"};
                return outcome;
            }
            if (!mirage::desktop::is_valid_utf8(text)) {
                outcome.error = {"invalid_argument", "text must be UTF-8"};
                return outcome;
            }
            env_.input_log.push_back("text " + text);
            outcome.ok = true;
            return outcome;
        }

        mirage::desktop::InputOutcome
        pointer_move(std::int32_t x, std::int32_t y, const mirage::desktop::InputLimits &limits,
                     const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::InputOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "injection cancelled"};
                return outcome;
            }
            if (limits.timeout.count() <= 0) {
                outcome.error = {"invalid_argument", "timeout must be positive"};
                return outcome;
            }
            env_.input_log.push_back("move " + std::to_string(x) + "," + std::to_string(y));
            outcome.ok = true;
            return outcome;
        }

        mirage::desktop::InputOutcome
        pointer_button(const mirage::desktop::MouseButton &button, bool pressed,
                       const mirage::desktop::InputLimits &limits,
                       const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::InputOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "injection cancelled"};
                return outcome;
            }
            if (limits.timeout.count() <= 0) {
                outcome.error = {"invalid_argument", "timeout must be positive"};
                return outcome;
            }
            if (button != "left" && button != "middle" && button != "right" && button != "back" &&
                button != "forward") {
                outcome.error = {"invalid_argument", "unknown mouse button"};
                return outcome;
            }
            env_.input_log.push_back(std::string(pressed ? "press " : "release ") + button);
            outcome.ok = true;
            return outcome;
        }

      private:
        FakeDesktopEnvironment &env_;
    };

    class FakeClipboard final : public mirage::desktop::ClipboardProvider {
      public:
        explicit FakeClipboard(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::ClipboardReadOutcome
        read_text(const mirage::desktop::ClipboardReadLimits &limits,
                  const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::ClipboardReadOutcome outcome;
            if (cancel.cancelled()) {
                outcome.error = {"cancelled", "read cancelled"};
                return outcome;
            }
            // Same argument contract as the real backends: a zero budget is
            // an invalid argument, decided before any content lookup.
            if (limits.max_bytes == 0) {
                outcome.error = {"invalid_argument", "read budget must be positive"};
                return outcome;
            }
            if (env_.failures.clipboard_unsupported_content) {
                outcome.error = {"unsupported_content", "clipboard holds non-text content"};
                return outcome;
            }
            if (!env_.clipboard_has_text) {
                outcome.error = {"not_found", "clipboard is empty"};
                return outcome;
            }
            if (env_.clipboard_text.size() > limits.max_bytes) {
                outcome.error = {"clipboard_too_large", "clipboard exceeds the read budget"};
                return outcome;
            }
            outcome.ok = true;
            outcome.content = env_.clipboard_text;
            return outcome;
        }

        mirage::desktop::ClipboardWriteOutcome
        write_text(const std::string &text, const mirage::desktop::ClipboardWriteLimits &limits,
                   const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::ClipboardWriteOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "write cancelled"};
                return outcome;
            }
            // Same rejection order as the real X11 backend: zero budget,
            // then payload budget, then encoding — all before the clipboard
            // state is touched.
            if (limits.max_bytes == 0) {
                outcome.error = {"invalid_argument", "write budget must be positive"};
                return outcome;
            }
            if (text.size() > limits.max_bytes) {
                outcome.error = {"invalid_argument", "payload exceeds the write budget"};
                return outcome;
            }
            if (!mirage::desktop::is_valid_utf8(text)) {
                outcome.error = {"invalid_argument", "text must be UTF-8"};
                return outcome;
            }
            env_.clipboard_text = text;
            env_.clipboard_has_text = true;
            outcome.ok = true;
            return outcome;
        }

      private:
        FakeDesktopEnvironment &env_;
    };

    class FakeNotification final : public mirage::desktop::NotificationProvider {
      public:
        explicit FakeNotification(FakeDesktopEnvironment &env) : env_(env) {}

        mirage::desktop::NotificationOutcome
        notify(const std::string &title, const std::string &body,
               const mirage::desktop::NotificationLimits &limits,
               const mirage::desktop::CancelToken &cancel) override {
            mirage::desktop::NotificationOutcome outcome;
            if (cancel.cancelled()) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "notification cancelled"};
                return outcome;
            }
            if (env_.failures.notify_error) {
                outcome.error = {"io_error", "injected notification failure"};
                return outcome;
            }
            if (title.empty()) {
                outcome.error = {"invalid_argument", "title must not be empty"};
                return outcome;
            }
            if (title.size() > limits.max_title_bytes) {
                outcome.error = {"invalid_argument", "title exceeds the byte budget"};
                return outcome;
            }
            if (body.size() > limits.max_body_bytes) {
                outcome.error = {"invalid_argument", "body exceeds the byte budget"};
                return outcome;
            }
            env_.notifications.emplace_back(title, body);
            outcome.ok = true;
            return outcome;
        }

      private:
        FakeDesktopEnvironment &env_;
    };

    std::uint8_t capture_pixel = 0x5A;

  public:
    FakeDesktopEnvironment()
        : filesystem_(*this), process_(*this), application_(*this), window_(*this),
          accessibility_(*this), screen_(*this), input_(*this), clipboard_(*this),
          notification_(*this) {}

    /// Fill value used for every generated frame; tests assert on it.
    std::uint8_t pixel_value() const { return capture_pixel; }
    void set_pixel_value(std::uint8_t value) { capture_pixel = value; }

    FakeFilesystem filesystem_;
    FakeProcess process_;
    FakeApplication application_;
    FakeWindow window_;
    FakeAccessibility accessibility_;
    FakeScreen screen_;
    FakeInput input_;
    FakeClipboard clipboard_;
    FakeNotification notification_;
};

} // namespace mirage::testing
