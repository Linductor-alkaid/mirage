// M2-01 provider contract tests. The fake environment enforces the same
// contracts real backends must honor, so every scenario here is a statement
// about the contract itself (DEC-005): budget refusals instead of
// truncation, cancellation observed before effects, invalid arguments
// rejected before state changes, and null-provider accessors failing closed.

#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/element_reference.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>

#include <string>

namespace {

using mirage::desktop::CancelToken;

mirage::desktop::SemanticNode node(std::string ref, std::string role, std::string name) {
    mirage::desktop::SemanticNode n;
    n.ref = std::move(ref);
    n.role = std::move(role);
    n.name = std::move(name);
    return n;
}

void environment_accessors_fail_closed_by_default() {
    // A bare environment (no providers overridden) must expose nothing.
    class BareEnvironment final : public mirage::desktop::DesktopEnvironment {
      public:
        mirage::desktop::EnvironmentInfo info() const override { return {"bare", "test"}; }
    } bare;

    MIRAGE_CHECK(bare.filesystem() == nullptr);
    MIRAGE_CHECK(bare.process() == nullptr);
    MIRAGE_CHECK(bare.application() == nullptr);
    MIRAGE_CHECK(bare.window() == nullptr);
    MIRAGE_CHECK(bare.accessibility() == nullptr);
    MIRAGE_CHECK(bare.screen() == nullptr);
    MIRAGE_CHECK(bare.input() == nullptr);
    MIRAGE_CHECK(bare.clipboard() == nullptr);
    MIRAGE_CHECK(bare.notification() == nullptr);

    mirage::testing::FakeDesktopEnvironment full;
    MIRAGE_CHECK(full.filesystem() != nullptr);
    MIRAGE_CHECK(full.process() != nullptr);
    MIRAGE_CHECK(full.application() != nullptr);
    MIRAGE_CHECK(full.window() != nullptr);
    MIRAGE_CHECK(full.accessibility() != nullptr);
    MIRAGE_CHECK(full.screen() != nullptr);
    MIRAGE_CHECK(full.input() != nullptr);
    MIRAGE_CHECK(full.clipboard() != nullptr);
    MIRAGE_CHECK(full.notification() != nullptr);
    MIRAGE_CHECK(full.info().platform == "test");
}

void window_provider_contract() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::WindowProvider &provider = *env.window();

    mirage::desktop::WindowInfo first;
    first.id = "w1";
    first.title = "Editor";
    first.geometry = {10, 20, 800, 600};
    mirage::desktop::WindowInfo second;
    second.id = "w2";
    second.title = "Terminal";
    env.windows = {first, second};

    const auto listed = provider.list_windows();
    MIRAGE_CHECK(listed.ok);
    MIRAGE_CHECK(listed.windows.size() == 2);
    MIRAGE_CHECK(listed.windows[0].title == "Editor");

    // Budget refusal, not truncation.
    mirage::desktop::WindowListLimits tight;
    tight.max_windows = 1;
    const auto refused = provider.list_windows(tight, {});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.windows.empty());
    MIRAGE_CHECK(refused.error.code == "result_too_large");

    // Front window: none focused -> found=false but ok.
    const auto none = provider.front_window();
    MIRAGE_CHECK(none.ok);
    MIRAGE_CHECK(!none.found);

    // Activation is exclusive and unknown ids fail before effects.
    const auto activated = provider.activate("w2");
    MIRAGE_CHECK(activated.ok);
    MIRAGE_CHECK(provider.front_window().window.id == "w2");
    MIRAGE_CHECK(env.windows[0].focused == false);
    MIRAGE_CHECK(env.windows[1].focused == true);

    const auto unknown = provider.activate("missing");
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "not_found");
    MIRAGE_CHECK(provider.front_window().window.id == "w2"); // unchanged

    const auto blank = provider.activate("");
    MIRAGE_CHECK(!blank.ok);
    MIRAGE_CHECK(blank.error.code == "invalid_argument");

    // Cancellation is observed before effects.
    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled = provider.activate("w1", cancel);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(cancelled.cancelled);
    MIRAGE_CHECK(provider.front_window().window.id == "w2");

    env.failures.window_list_error = true;
    MIRAGE_CHECK(provider.list_windows().error.code == "io_error");
}

void accessibility_provider_contract() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::AccessibilityProvider &provider = *env.accessibility();

    mirage::desktop::SemanticSnapshot snapshot;
    snapshot.application = "Editor";
    snapshot.window_title = "main.cpp — Editor";
    snapshot.nodes.push_back(node("@e1", "menu", "File"));
    auto editor = node("@e2", "editor", "main.cpp");
    editor.focused = true;
    snapshot.nodes.push_back(editor);

    // A window without a snapshot has no accessibility tree.
    const auto unsupported = provider.semantic_snapshot("w9");
    MIRAGE_CHECK(!unsupported.ok);
    MIRAGE_CHECK(unsupported.error.code == "unsupported_window");

    env.snapshots["w1"] = snapshot;
    const auto generated = provider.semantic_snapshot("w1");
    MIRAGE_CHECK(generated.ok);
    MIRAGE_CHECK(generated.snapshot.application == "Editor");
    MIRAGE_CHECK(generated.snapshot.nodes.size() == 2);
    MIRAGE_CHECK(generated.snapshot.nodes[1].ref == "@e2");
    MIRAGE_CHECK(generated.snapshot.nodes[1].focused);

    // Node budget refuses instead of truncating.
    mirage::desktop::SemanticSnapshotLimits tight;
    tight.max_nodes = 1;
    const auto refused = provider.semantic_snapshot("w1", tight, {});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "snapshot_too_large");

    const auto blank = provider.semantic_snapshot("");
    MIRAGE_CHECK(!blank.ok);
    MIRAGE_CHECK(blank.error.code == "invalid_argument");

    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled = provider.semantic_snapshot("w1", {}, cancel);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(cancelled.cancelled);

    env.failures.snapshot_error = true;
    MIRAGE_CHECK(provider.semantic_snapshot("w1").error.code == "io_error");
}

void snapshot_rendering_is_deterministic() {
    mirage::desktop::SemanticSnapshot snapshot;
    snapshot.application = "Visual Studio Code";
    snapshot.window_title = "mira — Visual Studio Code";
    snapshot.nodes.push_back(node("@e1", "menu", "File"));
    auto disabled = node("@e2", "button", "Run");
    disabled.enabled = false;
    snapshot.nodes.push_back(disabled);
    auto focused = node("@e3", "editor", "main.cpp");
    focused.focused = true;
    snapshot.nodes.push_back(focused);

    const std::string first = mirage::desktop::render_semantic_snapshot(snapshot);
    const std::string second = mirage::desktop::render_semantic_snapshot(snapshot);
    MIRAGE_CHECK(first == second);
    MIRAGE_CHECK(first.find("Application: Visual Studio Code") == 0);
    MIRAGE_CHECK(first.find("Window: mira — Visual Studio Code\n") != std::string::npos);
    MIRAGE_CHECK(first.find("@e1 menu \"File\"\n") != std::string::npos);
    MIRAGE_CHECK(first.find("@e2 button \"Run\" [disabled]\n") != std::string::npos);
    MIRAGE_CHECK(first.find("@e3 editor \"main.cpp\" [focused]\n") != std::string::npos);

    // Node order is preserved; refs are not renumbered.
    MIRAGE_CHECK(first.find("@e1") < first.find("@e2"));
    MIRAGE_CHECK(first.find("@e2") < first.find("@e3"));

    mirage::desktop::SemanticSnapshot empty;
    MIRAGE_CHECK(mirage::desktop::render_semantic_snapshot(empty).empty());
}

void element_target_hint_groups() {
    mirage::desktop::ElementTarget empty;
    MIRAGE_CHECK(!empty.has_any_hint());
    MIRAGE_CHECK(empty.hint_count() == 0);

    mirage::desktop::ElementTarget by_reference;
    by_reference.reference.id = "@e5";
    MIRAGE_CHECK(by_reference.has_any_hint());
    MIRAGE_CHECK(by_reference.hint_count() == 1);

    mirage::desktop::ElementTarget by_semantic;
    by_semantic.semantic.role = "button";
    by_semantic.semantic.name = "Run";
    MIRAGE_CHECK(by_semantic.has_any_hint());
    MIRAGE_CHECK(by_semantic.hint_count() == 1);

    mirage::desktop::ElementTarget combined;
    combined.reference.id = "@e5";
    combined.semantic.name = "Run";
    combined.structural.path = "/menu/run";
    combined.visual.template_id = "cache:settings";
    combined.spatial.relative_to.id = "@e1";
    combined.raw = {100, 200};
    MIRAGE_CHECK(combined.hint_count() == 6);

    // Zero raw coordinates alone do not form a hint (indistinguishable from
    // an unset raw hint in the value representation).
    mirage::desktop::ElementTarget zero_raw;
    zero_raw.raw = {0, 0};
    MIRAGE_CHECK(!zero_raw.has_any_hint());
}

void screen_provider_contract() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::ScreenProvider &provider = *env.screen();

    env.displays.push_back({"d1", {0, 0, 1920, 1080}, true});
    const auto listed = provider.list_displays();
    MIRAGE_CHECK(listed.ok);
    MIRAGE_CHECK(listed.displays.size() == 1);
    MIRAGE_CHECK(listed.displays[0].primary);

    const auto captured = provider.capture_display("d1");
    MIRAGE_CHECK(captured.ok);
    MIRAGE_CHECK(captured.frame.width == 1920);
    MIRAGE_CHECK(captured.frame.height == 1080);
    MIRAGE_CHECK(captured.frame.format == mirage::desktop::ImageFormat::Bgra8);
    MIRAGE_CHECK(captured.frame.pixels.size() == static_cast<std::size_t>(1920) * 4u * 1080u);
    MIRAGE_CHECK(captured.frame.pixels.front() == env.pixel_value());

    // Byte budget refuses instead of truncating.
    mirage::desktop::CaptureLimits tight;
    tight.max_bytes = 1024;
    const auto refused = provider.capture_display("d1", tight, {});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.frame.pixels.empty());
    MIRAGE_CHECK(refused.error.code == "capture_too_large");

    const auto unknown = provider.capture_display("missing");
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "not_found");

    env.windows.push_back({"w1", "Editor", {10, 10, 320, 200}, false});
    const auto window_frame = provider.capture_window("w1");
    MIRAGE_CHECK(window_frame.ok);
    MIRAGE_CHECK(window_frame.frame.width == 320);
    MIRAGE_CHECK(window_frame.frame.height == 200);

    const auto roi = provider.capture_roi({0, 0, 16, 8});
    MIRAGE_CHECK(roi.ok);
    MIRAGE_CHECK(roi.frame.width == 16);
    MIRAGE_CHECK(roi.frame.stride >= static_cast<std::size_t>(16) * 4u);

    const auto bad_roi = provider.capture_roi({0, 0, -1, 8});
    MIRAGE_CHECK(!bad_roi.ok);
    MIRAGE_CHECK(bad_roi.error.code == "invalid_argument");

    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(provider.capture_roi({0, 0, 16, 8}, {}, cancel).cancelled);

    env.failures.capture_error = true;
    MIRAGE_CHECK(provider.capture_roi({0, 0, 16, 8}).error.code == "io_error");
}

void input_provider_contract() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::InputProvider &provider = *env.input();

    const auto key = provider.inject_key({"enter"}, true);
    MIRAGE_CHECK(key.ok);
    MIRAGE_CHECK(env.input_log.back() == "down enter");

    const auto chord = provider.inject_key({"ctrl+shift+t"}, true);
    MIRAGE_CHECK(chord.ok);

    const auto text = provider.type_text("héllo");
    MIRAGE_CHECK(text.ok);
    MIRAGE_CHECK(env.input_log.back() == "text héllo");

    const auto moved = provider.pointer_move(120, 80);
    MIRAGE_CHECK(moved.ok);
    const auto clicked = provider.pointer_button("left", true);
    MIRAGE_CHECK(clicked.ok);
    MIRAGE_CHECK(env.input_log.back() == "press left");

    // Contract rejections before any log entry is produced.
    const std::size_t log_size = env.input_log.size();
    MIRAGE_CHECK(!provider.inject_key({"ctrl+alt+meta+ctrl+x"}, true).ok); // repeat modifier
    MIRAGE_CHECK(!provider.inject_key({"alt+ctrl+x"}, true).ok);           // wrong order
    MIRAGE_CHECK(!provider.inject_key({"f13"}, true).ok);
    MIRAGE_CHECK(!provider.inject_key({"ctrl+"}, true).ok);
    MIRAGE_CHECK(provider.inject_key({" "}, true).error.code == "invalid_argument");
    MIRAGE_CHECK(!provider.type_text("\xff\xfe").ok);      // invalid UTF-8
    MIRAGE_CHECK(!provider.pointer_button("up", true).ok); // not a button

    mirage::desktop::InputLimits tight;
    tight.max_text_bytes = 4;
    MIRAGE_CHECK(!provider.type_text("12345", tight, {}).ok);
    MIRAGE_CHECK(provider.type_text("1234", tight, {}).ok);

    mirage::desktop::InputLimits dead;
    dead.timeout = std::chrono::milliseconds{0};
    MIRAGE_CHECK(provider.pointer_move(1, 1, dead, {}).error.code == "invalid_argument");

    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(provider.type_text("x", {}, cancel).cancelled);
    MIRAGE_CHECK(env.input_log.size() == log_size + 1); // only the tight-budget success landed
}

void key_name_and_utf8_helpers() {
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("a"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("?"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("space"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("enter"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("f12"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("ctrl+c"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("ctrl+alt+delete"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("shift+?"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name(""));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("ctrl+"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("ctrl+ctrl+c"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("meta+shift+alt+ctrl+x")); // wrong order
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("f13"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("F12")); // vocabulary is lowercase
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("ctrl+shift+shift+x"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("执行")); // no non-ASCII key names

    MIRAGE_CHECK(mirage::desktop::is_valid_utf8(""));
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("plain ascii"));
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("héllo wörld"));
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("执行"));
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("\xF0\x9F\x8C\x90")); // U+1F310
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xff"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xc3"));             // truncated
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xc0\x80"));         // overlong
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xed\xa0\x80"));     // surrogate
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xf5\x80\x80\x80")); // > U+10FFFF
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("ok\xc3\x28"));       // bad continuation
}

/// M2-06 pointer query contract: pointer_position is the read-only half of
/// the input surface feeding the DesktopObservation pointer_state component.
/// Cancellation is observed before the result, the position comes from the
/// provider's tracked state, successful pointer_move calls update it, and a
/// query is side-effect free (no injection, no state change).
void pointer_position_contract() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::InputProvider &provider = *env.input();

    // A fresh environment tracks the origin until something moves it.
    const auto initial = provider.pointer_position();
    MIRAGE_CHECK(initial.ok);
    MIRAGE_CHECK(!initial.cancelled);
    MIRAGE_CHECK(initial.error.code.empty());
    MIRAGE_CHECK(initial.position.x == 0);
    MIRAGE_CHECK(initial.position.y == 0);
    MIRAGE_CHECK(initial.position.x == env.pointer.x);
    MIRAGE_CHECK(initial.position.y == env.pointer.y);

    // A successful pointer_move updates the reported position.
    const auto moved = provider.pointer_move(42, 7);
    MIRAGE_CHECK(moved.ok);
    MIRAGE_CHECK(env.pointer.x == 42);
    MIRAGE_CHECK(env.pointer.y == 7);
    const auto after_move = provider.pointer_position();
    MIRAGE_CHECK(after_move.ok);
    MIRAGE_CHECK(after_move.position.x == 42);
    MIRAGE_CHECK(after_move.position.y == 7);

    // Negative coordinates are ordinary positions (global desktop space).
    MIRAGE_CHECK(provider.pointer_move(-3, -4).ok);
    const auto negative = provider.pointer_position();
    MIRAGE_CHECK(negative.ok);
    MIRAGE_CHECK(negative.position.x == -3);
    MIRAGE_CHECK(negative.position.y == -4);

    // A query is read-only: no injection log entries, no position change.
    const std::size_t log_size = env.input_log.size();
    const auto repeat = provider.pointer_position();
    MIRAGE_CHECK(repeat.ok);
    MIRAGE_CHECK(repeat.position.x == -3);
    MIRAGE_CHECK(repeat.position.y == -4);
    MIRAGE_CHECK(env.pointer.x == -3);
    MIRAGE_CHECK(env.pointer.y == -4);
    MIRAGE_CHECK(env.input_log.size() == log_size);

    // Cancellation precedes the result: cancelled outcome, position state
    // untouched.
    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled = provider.pointer_position(cancel);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(cancelled.cancelled);
    MIRAGE_CHECK(cancelled.error.code == "cancelled");
    MIRAGE_CHECK(env.pointer.x == -3);
    MIRAGE_CHECK(env.pointer.y == -4);
    MIRAGE_CHECK(env.input_log.size() == log_size);
}

void clipboard_provider_contract() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::ClipboardProvider &provider = *env.clipboard();

    const auto empty = provider.read_text();
    MIRAGE_CHECK(!empty.ok);
    MIRAGE_CHECK(empty.error.code == "not_found");

    const auto written = provider.write_text("payload");
    MIRAGE_CHECK(written.ok);
    const auto read_back = provider.read_text();
    MIRAGE_CHECK(read_back.ok);
    MIRAGE_CHECK(read_back.content == "payload");

    mirage::desktop::ClipboardReadLimits tight_read;
    tight_read.max_bytes = 4;
    MIRAGE_CHECK(provider.read_text(tight_read, {}).error.code == "clipboard_too_large");

    mirage::desktop::ClipboardWriteLimits tight_write;
    tight_write.max_bytes = 4;
    MIRAGE_CHECK(!provider.write_text("12345", tight_write, {}).ok);
    MIRAGE_CHECK(provider.read_text().content == "payload"); // unchanged

    // Zero budgets are invalid arguments in both directions, decided before
    // any content lookup or ownership change.
    mirage::desktop::ClipboardReadLimits zero_read;
    zero_read.max_bytes = 0;
    MIRAGE_CHECK(provider.read_text(zero_read, {}).error.code == "invalid_argument");
    mirage::desktop::ClipboardWriteLimits zero_write;
    zero_write.max_bytes = 0;
    MIRAGE_CHECK(provider.write_text("x", zero_write, {}).error.code == "invalid_argument");
    MIRAGE_CHECK(provider.read_text().content == "payload"); // unchanged

    // Malformed UTF-8 payloads are refused with invalid_argument before the
    // clipboard is touched — the same pre-side-effect rejection the real
    // backends enforce (M2-04): bad continuation byte and a truncated
    // 2-byte sequence.
    MIRAGE_CHECK(!provider.write_text("\xff\xfe").ok);
    MIRAGE_CHECK(provider.write_text("\xff\xfe").error.code == "invalid_argument");
    MIRAGE_CHECK(!provider.write_text("ok\xc3").ok);
    MIRAGE_CHECK(provider.write_text("ok\xc3").error.code == "invalid_argument");
    MIRAGE_CHECK(provider.read_text().content == "payload"); // state unchanged

    env.failures.clipboard_unsupported_content = true;
    MIRAGE_CHECK(provider.read_text().error.code == "unsupported_content");
    // Writes stay possible; the read-side content flag is injection-only.
    MIRAGE_CHECK(provider.write_text("next").ok);

    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(provider.write_text("x", {}, cancel).cancelled);
}

void application_provider_contract() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::ApplicationProvider &provider = *env.application();

    env.applications.push_back({"app.one.desktop", "Application One", false});
    env.applications.push_back({"app.two.desktop", "Application Two", false});

    const auto listed = provider.list_applications();
    MIRAGE_CHECK(listed.ok);
    MIRAGE_CHECK(listed.applications.size() == 2);

    const auto unknown_state = provider.running_state("missing.desktop");
    MIRAGE_CHECK(!unknown_state.ok);
    MIRAGE_CHECK(unknown_state.error.code == "not_found");

    const auto launched = provider.launch("app.one.desktop");
    MIRAGE_CHECK(launched.ok);
    MIRAGE_CHECK(!launched.instance_id.empty());
    MIRAGE_CHECK(provider.running_state("app.one.desktop").running);

    // Single-instance discipline: a second launch is refused.
    const auto duplicate = provider.launch("app.one.desktop");
    MIRAGE_CHECK(!duplicate.ok);
    MIRAGE_CHECK(duplicate.error.code == "already_running");

    // Launching an unknown id never creates an instance.
    const auto unknown = provider.launch("missing.desktop");
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "not_found");
    MIRAGE_CHECK(env.application_instances.count("missing.desktop") == 0);

    const auto terminated = provider.terminate("app.one.desktop");
    MIRAGE_CHECK(terminated.ok);
    MIRAGE_CHECK(!provider.running_state("app.one.desktop").running);

    const auto terminate_unknown = provider.terminate("app.one.desktop");
    MIRAGE_CHECK(!terminate_unknown.ok);
    MIRAGE_CHECK(terminate_unknown.error.code == "not_found");

    env.failures.application_terminate_stuck = true;
    env.application_instances["app.two.desktop"] = "i-two";
    const auto stuck = provider.terminate("app.two.desktop");
    MIRAGE_CHECK(!stuck.ok);
    MIRAGE_CHECK(stuck.error.code == "deadline_exceeded");
    MIRAGE_CHECK(provider.running_state("app.two.desktop").running); // untouched

    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(provider.launch("app.two.desktop", {}, cancel).cancelled);
}

void notification_provider_contract() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::NotificationProvider &provider = *env.notification();

    const auto posted = provider.notify("Task done", "1 task completed");
    MIRAGE_CHECK(posted.ok);
    MIRAGE_CHECK(env.notifications.size() == 1);
    MIRAGE_CHECK(env.notifications.back().first == "Task done");
    MIRAGE_CHECK(env.notifications.back().second == "1 task completed");

    MIRAGE_CHECK(provider.notify("", "body").error.code == "invalid_argument");

    mirage::desktop::NotificationLimits tight;
    tight.max_title_bytes = 4;
    tight.max_body_bytes = 8;
    MIRAGE_CHECK(provider.notify("toolongtitle", "b", tight, {}).error.code == "invalid_argument");
    MIRAGE_CHECK(provider.notify("t", "way too long body", tight, {}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(env.notifications.size() == 1); // nothing landed

    env.failures.notify_error = true;
    MIRAGE_CHECK(provider.notify("t", "b").error.code == "io_error");

    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(provider.notify("t", "b", {}, cancel).cancelled);
}

void filesystem_and_process_still_hold_m1_contracts() {
    // The fake keeps the M1 provider shapes so environment-level tests can
    // exercise whole-task flows; scope and budget semantics must hold here
    // exactly as on the real backend.
    mirage::testing::FakeDesktopEnvironment env;
    env.files = {{"/ws/notes.txt", "hello"}};
    env.filesystem_.scope() = mirage::desktop::PathScope{{"/ws"}};

    const auto read = env.filesystem()->read_text_file("/ws/notes.txt");
    MIRAGE_CHECK(read.ok);
    MIRAGE_CHECK(read.content == "hello");

    const auto outside = env.filesystem()->read_text_file("/etc/hostname");
    MIRAGE_CHECK(!outside.ok);
    MIRAGE_CHECK(outside.error.code == "permission_denied");

    const auto executed = env.process()->execute("true", {});
    MIRAGE_CHECK(executed.ok);
    MIRAGE_CHECK(executed.exit_code == 0);

    env.next_process_outcome = [] {
        mirage::desktop::ProcessOutcome outcome;
        outcome.ok = false;
        outcome.timed_out = true;
        outcome.error = {"deadline_exceeded", "command exceeded its budget"};
        return outcome;
    }();
    const auto timed_out = env.process()->execute("sleep 100", {});
    MIRAGE_CHECK(!timed_out.ok);
    MIRAGE_CHECK(timed_out.timed_out);
    MIRAGE_CHECK(env.process()->execute("true", {}).ok); // script reset after use
}

// Independent-verification additions: boundary and negative-path hardening
// for the M2-01 contract surface (DEC-005). Each scenario pins an edge the
// base scenarios leave open; none changes contract header semantics.

void contract_helper_hardening_edges() {
    // UTF-8: boundary code points are accepted...
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("\xc2\x80"));         // U+0080 (min 2B)
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("\xdf\xbf"));         // U+07FF (max 2B)
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("\xe0\xa0\x80"));     // U+0800 (min 3B)
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("\xed\x9f\xbf"));     // U+D7FF (below surrogates)
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("\xef\xbf\xbf"));     // U+FFFF (max 3B)
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("\xf0\x90\x80\x80")); // U+10000 (min 4B)
    MIRAGE_CHECK(mirage::desktop::is_valid_utf8("\xf4\x8f\xbf\xbf")); // U+10FFFF (max)
    // ...and every malformed shape is refused.
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xf8\x80\x80")); // F8 is never a lead byte
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xfe\x80\x80"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xff\x80\x80"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xf5\x80\x80\x80")); // > U+10FFFF lead F5
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xf7\xbf\xbf\xbf")); // > U+10FFFF lead F7
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xc1\x80"));         // overlong 2B
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xe0\x9f\xbf"));     // overlong (U+07FF in 3B)
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xf0\x8f\xbf\xbf")); // overlong (U+FFFF in 4B)
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\x80"));             // lone continuation
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xbf"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("a\x80"
                                                 "b"));            // stray continuation mid-text
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xe2\x82"));     // truncated 3B
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xf0\x9f\x8c")); // truncated 4B
    MIRAGE_CHECK(!mirage::desktop::is_valid_utf8("\xf4\x90\x80\x80")); // first code point past max

    // Key names: the full canonical modifier chain and printable edges.
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("ctrl+alt+shift+meta+a"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("shift+f1"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("f11"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("ctrl++")); // '+' is a printable key
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("z"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("~"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("ctrl+space"));
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("alt+left"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("f0"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("f01")); // no zero padding
    MIRAGE_CHECK(mirage::desktop::is_valid_key_name("f"));    // literal printable character
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("fx"));  // multi-char f-names need digits
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("meta+meta+a"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("alt+alt+a"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("CTRL+a")); // vocabulary is lowercase
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("ctrl+ENTER"));
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("\x7f")); // DEL is not printable
    MIRAGE_CHECK(!mirage::desktop::is_valid_key_name("ctrl+meta+alt+shift+x")); // wrong order
}

/// parse_key_chord is the M2-02 shared parser behind is_valid_key_name: the
/// flags and base it produces are what backends inject, so the split must
/// agree with validation on every edge the vocabulary defines.
void key_chord_parsing_matches_validation() {
    const auto plain = mirage::desktop::parse_key_chord("a");
    MIRAGE_CHECK(plain.has_value());
    MIRAGE_CHECK(!plain->ctrl && !plain->alt && !plain->shift && !plain->meta);
    MIRAGE_CHECK(plain->base == "a");

    const auto full = mirage::desktop::parse_key_chord("ctrl+alt+shift+meta+x");
    MIRAGE_CHECK(full.has_value());
    MIRAGE_CHECK(full->ctrl && full->alt && full->shift && full->meta);
    MIRAGE_CHECK(full->base == "x");

    const auto named = mirage::desktop::parse_key_chord("ctrl+space");
    MIRAGE_CHECK(named.has_value() && named->ctrl && !named->shift);
    MIRAGE_CHECK(named->base == "space");

    const auto fn = mirage::desktop::parse_key_chord("shift+f12");
    MIRAGE_CHECK(fn.has_value() && fn->shift && fn->base == "f12");

    // 'ctrl++': the first '+' is the modifier separator, the second is the
    // literal printable key.
    const auto plus = mirage::desktop::parse_key_chord("ctrl++");
    MIRAGE_CHECK(plus.has_value() && plus->ctrl && plus->base == "+");

    // A lone modifier prefix has no base; a repeated modifier leaves an
    // invalid remainder; both must be rejected, not produce a chord.
    MIRAGE_CHECK(!mirage::desktop::parse_key_chord("ctrl+").has_value());
    MIRAGE_CHECK(!mirage::desktop::parse_key_chord("ctrl+ctrl+c").has_value());
    MIRAGE_CHECK(!mirage::desktop::parse_key_chord("meta+meta+a").has_value());
    MIRAGE_CHECK(!mirage::desktop::parse_key_chord("meta+shift+alt+ctrl+x").has_value());
    MIRAGE_CHECK(!mirage::desktop::parse_key_chord("").has_value());

    // Validation and parsing cannot diverge: every accepted name parses and
    // every rejected name fails to parse.
    for (const char *name : {"a",
                             "?",
                             "space",
                             "enter",
                             "f12",
                             "ctrl+c",
                             "ctrl+alt+delete",
                             "shift+?",
                             "ctrl+alt+shift+meta+a",
                             "alt+left",
                             "f1",
                             "f",
                             "z",
                             "~",
                             "",
                             "ctrl+",
                             "ctrl+ctrl+c",
                             "f13",
                             "F12",
                             "ctrl+shift+shift+x",
                             "fx",
                             "f0",
                             "执行",
                             "\x7f"}) {
        MIRAGE_CHECK(mirage::desktop::is_valid_key_name(name) ==
                     mirage::desktop::parse_key_chord(name).has_value());
    }
}

void provider_budget_boundaries() {
    mirage::testing::FakeDesktopEnvironment env;
    for (int i = 1; i <= 3; ++i) {
        env.windows.push_back({"w" + std::to_string(i), "Window " + std::to_string(i), {}, false});
    }
    env.displays.push_back({"d1", {0, 0, 100, 100}, true});
    env.displays.push_back({"d2", {100, 0, 100, 100}, false});
    env.applications.push_back({"app.one.desktop", "One", false});
    env.applications.push_back({"app.two.desktop", "Two", false});

    // Exact budget is accepted; one over is refused as a whole table.
    mirage::desktop::WindowListLimits win_ok;
    win_ok.max_windows = 3;
    MIRAGE_CHECK(env.window()->list_windows(win_ok, {}).ok);
    mirage::desktop::WindowListLimits win_tight;
    win_tight.max_windows = 2;
    const auto win_refused = env.window()->list_windows(win_tight, {});
    MIRAGE_CHECK(!win_refused.ok);
    MIRAGE_CHECK(win_refused.error.code == "result_too_large");
    MIRAGE_CHECK(win_refused.windows.empty());

    mirage::desktop::DisplayListLimits disp_ok;
    disp_ok.max_displays = 2;
    MIRAGE_CHECK(env.screen()->list_displays(disp_ok, {}).ok);
    mirage::desktop::DisplayListLimits disp_tight;
    disp_tight.max_displays = 1;
    const auto disp_refused = env.screen()->list_displays(disp_tight, {});
    MIRAGE_CHECK(!disp_refused.ok);
    MIRAGE_CHECK(disp_refused.error.code == "result_too_large");
    MIRAGE_CHECK(disp_refused.displays.empty());

    mirage::desktop::ApplicationListLimits app_ok;
    app_ok.max_applications = 2;
    MIRAGE_CHECK(env.application()->list_applications(app_ok, {}).ok);
    mirage::desktop::ApplicationListLimits app_tight;
    app_tight.max_applications = 1;
    const auto app_refused = env.application()->list_applications(app_tight, {});
    MIRAGE_CHECK(!app_refused.ok);
    MIRAGE_CHECK(app_refused.error.code == "result_too_large");
    MIRAGE_CHECK(app_refused.applications.empty());

    // Byte budgets: exact size in, one byte out.
    MIRAGE_CHECK(env.clipboard()->write_text("payload").ok);
    mirage::desktop::ClipboardReadLimits read_exact;
    read_exact.max_bytes = 7;
    MIRAGE_CHECK(env.clipboard()->read_text(read_exact, {}).ok);
    mirage::desktop::ClipboardReadLimits read_tight;
    read_tight.max_bytes = 6;
    MIRAGE_CHECK(env.clipboard()->read_text(read_tight, {}).error.code == "clipboard_too_large");
    mirage::desktop::ClipboardWriteLimits write_exact;
    write_exact.max_bytes = 7;
    MIRAGE_CHECK(env.clipboard()->write_text("payload", write_exact, {}).ok);
    mirage::desktop::ClipboardWriteLimits write_tight;
    write_tight.max_bytes = 6;
    MIRAGE_CHECK(!env.clipboard()->write_text("payload", write_tight, {}).ok);
    MIRAGE_CHECK(env.clipboard()->read_text().content == "payload"); // unchanged

    mirage::desktop::CaptureLimits cap_exact;
    cap_exact.max_bytes = 4; // 1x1 BGRA
    MIRAGE_CHECK(env.screen()->capture_roi({0, 0, 1, 1}, cap_exact, {}).ok);
    mirage::desktop::CaptureLimits cap_tight;
    cap_tight.max_bytes = 3;
    MIRAGE_CHECK(env.screen()->capture_roi({0, 0, 1, 1}, cap_tight, {}).error.code ==
                 "capture_too_large");

    mirage::desktop::InputLimits text_exact;
    text_exact.max_text_bytes = 5;
    MIRAGE_CHECK(env.input()->type_text("12345", text_exact, {}).ok);
    MIRAGE_CHECK(env.input()->type_text("").ok); // empty text is valid
}

void accessibility_element_action_contract() {
    using mirage::desktop::ElementTarget;
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::AccessibilityProvider &provider = *env.accessibility();

    const auto make = [](std::string ref, std::string role, std::string name, std::size_t parent) {
        mirage::desktop::SemanticNode n;
        n.ref = std::move(ref);
        n.role = std::move(role);
        n.name = std::move(name);
        n.parent = parent;
        return n;
    };

    mirage::desktop::SemanticSnapshot w1;
    w1.application = "Editor";
    w1.window_title = "main";
    // Two branches from the root: the target of the structural path below
    // lives in the SECOND branch, so resolution must scan siblings instead
    // of walking only the first child.
    w1.nodes.push_back(make("@e1", "window", "main", mirage::desktop::kNoParent));
    w1.nodes.push_back(make("@e2", "panel", "left", 0));
    w1.nodes.push_back(make("@e3", "button", "Run", 1));
    w1.nodes.push_back(make("@e4", "entry", "Name", 1));
    w1.nodes.push_back(make("@e5", "button", "Deep", 0));
    env.snapshots["w1"] = w1;
    env.actionable_refs = {"@e3", "@e5"};
    env.editable_refs = {"@e4"};

    mirage::desktop::SemanticSnapshot w2;
    w2.application = "Editor";
    w2.window_title = "second";
    w2.nodes.push_back(make("@f1", "button", "Second", mirage::desktop::kNoParent));
    env.snapshots["w2"] = w2;
    env.actionable_refs.push_back("@f1");

    // Before any snapshot there is no live window: semantic and structural
    // hints cannot resolve and report not_found.
    ElementTarget semantic;
    semantic.semantic.role = "button";
    semantic.semantic.name = "Run";
    MIRAGE_CHECK(provider.activate_element(semantic).error.code == "not_found");

    const auto generated = provider.semantic_snapshot("w1");
    MIRAGE_CHECK(generated.ok);

    // Reference resolution against the snapshot registry.
    ElementTarget reference;
    reference.reference.id = "@e3";
    const auto ref_hit = provider.activate_element(reference);
    MIRAGE_CHECK(ref_hit.ok);
    MIRAGE_CHECK(env.activated_refs.size() == 1);
    MIRAGE_CHECK(env.activated_refs.back() == "@e3");

    // Structural resolution reaches a target that is not on the first-child
    // chain: siblings are scanned for each role/name step.
    ElementTarget structural;
    structural.structural.path = "/window/main/button/Deep";
    const auto structural_hit = provider.activate_element(structural);
    MIRAGE_CHECK(structural_hit.ok);
    MIRAGE_CHECK(env.activated_refs.back() == "@e5");

    // Structural syntax and lookup failures stay not_found.
    ElementTarget odd;
    odd.structural.path = "/window/main/button"; // odd segment count
    MIRAGE_CHECK(provider.activate_element(odd).error.code == "not_found");
    ElementTarget no_step;
    no_step.structural.path = "/window/main/panel/Nowhere";
    MIRAGE_CHECK(provider.activate_element(no_step).error.code == "not_found");
    ElementTarget bad_root;
    bad_root.structural.path = "/dialog/other/button/Run";
    MIRAGE_CHECK(provider.activate_element(bad_root).error.code == "not_found");

    // Semantic resolution: role+name, and role alone hits the first match in
    // snapshot order.
    const auto semantic_hit = provider.activate_element(semantic);
    MIRAGE_CHECK(semantic_hit.ok);
    MIRAGE_CHECK(env.activated_refs.back() == "@e3");
    ElementTarget role_only;
    role_only.semantic.role = "button";
    const auto role_hit = provider.activate_element(role_only);
    MIRAGE_CHECK(role_hit.ok);
    MIRAGE_CHECK(env.activated_refs.back() == "@e3");
    ElementTarget unknown;
    unknown.semantic.role = "button";
    unknown.semantic.name = "Nowhere";
    MIRAGE_CHECK(provider.activate_element(unknown).error.code == "not_found");

    // Resolution order (DEC-005): reference wins even when a semantic hint
    // would match another element.
    ElementTarget reference_first;
    reference_first.reference.id = "@e5";
    reference_first.semantic.role = "entry";
    const auto order_hit = provider.activate_element(reference_first);
    MIRAGE_CHECK(order_hit.ok);
    MIRAGE_CHECK(env.activated_refs.back() == "@e5");

    // set_text lands on the editable element addressed by role+name.
    ElementTarget entry;
    entry.semantic.role = "entry";
    entry.semantic.name = "Name";
    const auto written = provider.set_text(entry, "hello");
    MIRAGE_CHECK(written.ok);
    MIRAGE_CHECK(env.text_writes.size() == 1);
    MIRAGE_CHECK(env.text_writes.back().first == "@e4");
    MIRAGE_CHECK(env.text_writes.back().second == "hello");

    // Element capability boundaries: actions need an actionable element,
    // text needs an editable one.
    ElementTarget not_actionable;
    not_actionable.reference.id = "@e4";
    MIRAGE_CHECK(provider.activate_element(not_actionable).error.code == "unsupported_element");
    ElementTarget not_editable;
    not_editable.semantic.role = "button";
    not_editable.semantic.name = "Run";
    MIRAGE_CHECK(provider.set_text(not_editable, "x").error.code == "unsupported_element");
    MIRAGE_CHECK(env.text_writes.size() == 1); // only the successful write landed

    // Non-accessibility hints fail closed before any effect, and take
    // priority over a valid reference in the same target.
    const std::size_t activated_before = env.activated_refs.size();
    ElementTarget visual_ocr;
    visual_ocr.visual.ocr_text = "Run";
    MIRAGE_CHECK(provider.activate_element(visual_ocr).error.code == "unsupported_hint");
    ElementTarget visual_template;
    visual_template.visual.template_id = "cache:settings";
    MIRAGE_CHECK(provider.activate_element(visual_template).error.code == "unsupported_hint");
    ElementTarget spatial;
    spatial.spatial.relative_to.id = "@e3";
    spatial.spatial.dx = 4;
    MIRAGE_CHECK(provider.activate_element(spatial).error.code == "unsupported_hint");
    ElementTarget raw;
    raw.raw = {3, 4};
    MIRAGE_CHECK(provider.activate_element(raw).error.code == "unsupported_hint");
    ElementTarget reference_with_visual;
    reference_with_visual.reference.id = "@e3";
    reference_with_visual.visual.template_id = "cache:settings";
    MIRAGE_CHECK(provider.activate_element(reference_with_visual).error.code == "unsupported_hint");
    ElementTarget no_hint;
    MIRAGE_CHECK(provider.activate_element(no_hint).error.code == "invalid_argument");
    MIRAGE_CHECK(env.activated_refs.size() == activated_before); // no side effect landed

    // set_text budget and encoding checks run before the element is touched.
    mirage::desktop::InputLimits exact;
    exact.max_text_bytes = 5;
    MIRAGE_CHECK(provider.set_text(entry, "hello", exact, {}).ok);
    MIRAGE_CHECK(env.text_writes.back().second == "hello");
    MIRAGE_CHECK(provider.set_text(entry, "hello!", exact, {}).error.code == "invalid_argument");
    MIRAGE_CHECK(provider.set_text(entry, "", exact, {}).ok); // empty text is valid
    MIRAGE_CHECK(env.text_writes.back().second.empty());
    MIRAGE_CHECK(provider.set_text(entry, "\xff", {}).error.code == "invalid_argument");
    MIRAGE_CHECK(provider.set_text(entry, "\xc3", {}).error.code == "invalid_argument");
    MIRAGE_CHECK(env.text_writes.size() == 3); // only the three valid writes landed

    // Cancellation precedes even the hint validation and never lands.
    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(provider.activate_element(visual_ocr, cancel).cancelled);
    MIRAGE_CHECK(provider.activate_element(reference, cancel).cancelled);
    MIRAGE_CHECK(provider.set_text(entry, "x", {}, cancel).cancelled);
    MIRAGE_CHECK(env.activated_refs.size() == activated_before);
    MIRAGE_CHECK(env.text_writes.size() == 3);

    // A fresh snapshot replaces the reference registry: refs from older
    // snapshots stop resolving, and semantic hints now search the new
    // window's snapshot.
    const auto second = provider.semantic_snapshot("w2");
    MIRAGE_CHECK(second.ok);
    MIRAGE_CHECK(provider.activate_element(reference).error.code == "not_found");
    MIRAGE_CHECK(provider.activate_element(semantic).error.code == "not_found");
    MIRAGE_CHECK(env.activated_refs.size() == activated_before); // stale misses are inert
    ElementTarget second_ref;
    second_ref.reference.id = "@f1";
    const auto second_hit = provider.activate_element(second_ref);
    MIRAGE_CHECK(second_hit.ok);
    MIRAGE_CHECK(env.activated_refs.back() == "@f1");
}

void accessibility_empty_snapshot_and_boundary() {
    mirage::testing::FakeDesktopEnvironment env;
    mirage::desktop::AccessibilityProvider &provider = *env.accessibility();

    // A window whose accessibility tree is empty-but-valid succeeds.
    env.snapshots["w-empty"] = mirage::desktop::SemanticSnapshot{};
    const auto empty = provider.semantic_snapshot("w-empty");
    MIRAGE_CHECK(empty.ok);
    MIRAGE_CHECK(empty.snapshot.nodes.empty());

    mirage::desktop::SemanticSnapshot snapshot;
    snapshot.nodes.push_back(node("@e1", "button", "A"));
    snapshot.nodes.push_back(node("@e2", "button", "B"));
    env.snapshots["w1"] = snapshot;

    // Node budget boundary: count == max_nodes succeeds.
    mirage::desktop::SemanticSnapshotLimits exact;
    exact.max_nodes = 2;
    const auto exact_outcome = provider.semantic_snapshot("w1", exact, {});
    MIRAGE_CHECK(exact_outcome.ok);
    MIRAGE_CHECK(exact_outcome.snapshot.nodes.size() == 2);

    mirage::desktop::SemanticSnapshotLimits over;
    over.max_nodes = 3; // only refused when the tree EXCEEDS the budget
    MIRAGE_CHECK(provider.semantic_snapshot("w1", over, {}).ok);
}

void application_argument_validation() {
    mirage::testing::FakeDesktopEnvironment env;
    env.applications.push_back({"app.one.desktop", "One", false});

    // Empty ids are invalid arguments, rejected before any lookup effect.
    MIRAGE_CHECK(env.application()->running_state("").error.code == "invalid_argument");
    MIRAGE_CHECK(env.application()->launch("").error.code == "invalid_argument");
    MIRAGE_CHECK(env.application()->terminate("").error.code == "invalid_argument");
    MIRAGE_CHECK(env.application_instances.empty());

    // Non-positive timeouts are invalid arguments; no instance is created or
    // dropped by the rejected call.
    mirage::desktop::ApplicationLaunchLimits dead;
    dead.timeout = std::chrono::milliseconds{0};
    MIRAGE_CHECK(env.application()->launch("app.one.desktop", dead, {}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(env.application_instances.empty());

    MIRAGE_CHECK(env.application()->launch("app.one.desktop").ok);
    mirage::desktop::ApplicationLaunchLimits negative;
    negative.timeout = std::chrono::milliseconds{-1};
    MIRAGE_CHECK(env.application()->terminate("app.one.desktop", negative, {}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(env.application()->running_state("app.one.desktop").running); // untouched

    // Launch length budget boundary: id size == max_command_bytes succeeds.
    mirage::desktop::ApplicationLaunchLimits exact;
    exact.max_command_bytes = std::string("app.one.desktop").size();
    MIRAGE_CHECK(env.application()->terminate("app.one.desktop").ok);
    const auto exact_launch = env.application()->launch("app.one.desktop", exact, {});
    MIRAGE_CHECK(exact_launch.ok);
    mirage::desktop::ApplicationLaunchLimits tight;
    tight.max_command_bytes = std::string("app.one.desktop").size() - 1;
    MIRAGE_CHECK(env.application()->terminate("app.one.desktop").ok); // reset single instance
    MIRAGE_CHECK(env.application()->launch("app.one.desktop", tight, {}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(env.application_instances.empty());
}

void cancellation_leaves_state_unchanged() {
    mirage::testing::FakeDesktopEnvironment env;
    env.windows.push_back({"w1", "Editor", {0, 0, 10, 10}, true});
    env.applications.push_back({"app.one.desktop", "One", false});
    MIRAGE_CHECK(env.clipboard()->write_text("keep").ok);

    CancelToken cancel;
    cancel.request_cancel();

    const std::size_t log_size = env.input_log.size();
    MIRAGE_CHECK(env.input()->inject_key({"a"}, true, {}, cancel).cancelled);
    MIRAGE_CHECK(env.input()->type_text("x", {}, cancel).cancelled);
    MIRAGE_CHECK(env.input()->pointer_move(5, 5, {}, cancel).cancelled);
    MIRAGE_CHECK(env.input()->pointer_button("left", true, {}, cancel).cancelled);
    MIRAGE_CHECK(env.input_log.size() == log_size); // no injection landed

    MIRAGE_CHECK(env.clipboard()->write_text("new", {}, cancel).cancelled);
    MIRAGE_CHECK(env.clipboard()->read_text().content == "keep"); // clipboard untouched

    const auto launch = env.application()->launch("app.one.desktop", {}, cancel);
    MIRAGE_CHECK(launch.cancelled);
    MIRAGE_CHECK(env.application_instances.count("app.one.desktop") == 0); // nothing spawned
    MIRAGE_CHECK(env.application()->launch("app.one.desktop").ok);
    const auto terminate = env.application()->terminate("app.one.desktop", {}, cancel);
    MIRAGE_CHECK(terminate.cancelled);
    MIRAGE_CHECK(env.application()->running_state("app.one.desktop").running); // still alive

    const auto notify = env.notification()->notify("t", "b", {}, cancel);
    MIRAGE_CHECK(notify.cancelled);
    MIRAGE_CHECK(env.notifications.empty());

    const auto activate = env.window()->activate("w1", cancel);
    MIRAGE_CHECK(activate.cancelled);
    MIRAGE_CHECK(env.windows[0].focused); // focus untouched by cancelled call

    const auto snapshot = env.accessibility()->semantic_snapshot("w1", {}, cancel);
    MIRAGE_CHECK(snapshot.cancelled);
    MIRAGE_CHECK(!snapshot.ok);
}

void element_target_hint_edges() {
    mirage::desktop::ElementTarget role_only;
    role_only.semantic.role = "button";
    MIRAGE_CHECK(role_only.hint_count() == 1);

    mirage::desktop::ElementTarget ocr_only;
    ocr_only.visual.ocr_text = "Run";
    MIRAGE_CHECK(ocr_only.hint_count() == 1);

    mirage::desktop::ElementTarget spatial_zero_offsets;
    spatial_zero_offsets.spatial.relative_to.id = "@e1";
    spatial_zero_offsets.spatial.dx = 0;
    spatial_zero_offsets.spatial.dy = 0;
    MIRAGE_CHECK(spatial_zero_offsets.hint_count() == 1);

    mirage::desktop::ElementTarget raw_y;
    raw_y.raw = {0, 5};
    MIRAGE_CHECK(raw_y.hint_count() == 1);
    mirage::desktop::ElementTarget raw_x;
    raw_x.raw = {5, 0};
    MIRAGE_CHECK(raw_x.hint_count() == 1);

    mirage::desktop::ElementTarget reference_with_zero_raw;
    reference_with_zero_raw.reference.id = "@e5";
    reference_with_zero_raw.raw = {0, 0};
    MIRAGE_CHECK(reference_with_zero_raw.hint_count() == 1); // only the reference counts
}

void snapshot_rendering_edges() {
    mirage::desktop::SemanticSnapshot snapshot;
    snapshot.window_title = "Only Title";
    auto both = node("@e1", "button", "Run");
    both.focused = true;
    both.enabled = false;
    snapshot.nodes.push_back(both);
    snapshot.nodes.push_back(node("@e2", "menu", "")); // unnamed node renders bare

    const std::string text = mirage::desktop::render_semantic_snapshot(snapshot);
    MIRAGE_CHECK(text.find("Application: ") == std::string::npos); // no empty header line
    MIRAGE_CHECK(text.find("Window: Only Title\n") == 0);
    // focused is reported before disabled, deterministically.
    MIRAGE_CHECK(text.find("@e1 button \"Run\" [focused] [disabled]\n") != std::string::npos);
    MIRAGE_CHECK(text.find("@e2 menu\n") != std::string::npos);

    const std::string again = mirage::desktop::render_semantic_snapshot(snapshot);
    MIRAGE_CHECK(text == again);
}

void screen_and_notification_edges() {
    mirage::testing::FakeDesktopEnvironment env;
    env.displays.push_back({"d1", {0, 0, 100, 100}, true});
    env.windows.push_back({"w1", "Editor", {10, 10, 32, 16}, false});

    // Zero extents are as invalid as negative ones.
    MIRAGE_CHECK(env.screen()->capture_roi({0, 0, 0, 8}).error.code == "invalid_argument");
    MIRAGE_CHECK(env.screen()->capture_roi({0, 0, 8, 0}).error.code == "invalid_argument");

    CancelToken cancel;
    cancel.request_cancel();
    const auto window_cancelled = env.screen()->capture_window("w1", {}, cancel);
    MIRAGE_CHECK(window_cancelled.cancelled);
    MIRAGE_CHECK(!window_cancelled.ok);
    MIRAGE_CHECK(window_cancelled.frame.pixels.empty());

    // Notification byte budgets hold at the exact boundary.
    mirage::desktop::NotificationLimits exact;
    exact.max_title_bytes = 2;
    exact.max_body_bytes = 3;
    MIRAGE_CHECK(env.notification()->notify("ab", "abc", exact, {}).ok);
    MIRAGE_CHECK(env.notifications.size() == 1);
    MIRAGE_CHECK(env.notification()->notify("abc", "abc", exact, {}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(env.notification()->notify("ab", "abcd", exact, {}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(env.notifications.size() == 1); // nothing extra landed
}

} // namespace

int main() {
    environment_accessors_fail_closed_by_default();
    window_provider_contract();
    accessibility_provider_contract();
    accessibility_element_action_contract();
    snapshot_rendering_is_deterministic();
    element_target_hint_groups();
    screen_provider_contract();
    input_provider_contract();
    key_name_and_utf8_helpers();
    contract_helper_hardening_edges();
    key_chord_parsing_matches_validation();
    provider_budget_boundaries();
    accessibility_empty_snapshot_and_boundary();
    application_argument_validation();
    cancellation_leaves_state_unchanged();
    pointer_position_contract();
    element_target_hint_edges();
    snapshot_rendering_edges();
    screen_and_notification_edges();
    clipboard_provider_contract();
    application_provider_contract();
    notification_provider_contract();
    filesystem_and_process_still_hold_m1_contracts();
    return mirage::testing::finish("provider_contract");
}
