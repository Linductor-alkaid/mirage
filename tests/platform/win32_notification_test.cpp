// M4-05 Windows notification frontend tests against the session's real
// notification area (DEC-017 decision 9 discipline, DEC-018 carrier): the
// fixture is the session's actual tray — open() builds the hidden carrier
// window and the resident icon, notify() posts real balloons, and every
// reported outcome must agree with an independently observed channel. No
// independent probe of balloon VISIBILITY exists on Windows (by design:
// the frozen contract says ok = accepted, not seen), so the positive
// scenarios assert acceptance through the provider and treat the platform's
// presentation as unobservable (loud notes, never visibility claims).
//
// Assertion philosophy: the frozen refusal order (cancelled → empty title →
// contract byte budgets → UTF-8) plus the platform's own caps (DEC-018
// decision 3) are strict and must all happen before any side effect —
// proven by boundary arithmetic that byte budgets alone would pass (a
// 64-CJK-character title fits the 256-byte contract budget but cannot fit
// the 63-code-unit balloon array). The open() fail-closed discipline is
// exercised by the option being off. A session without a notification area
// (a service context) legitimately leaves the accessor null: the test
// notes it loudly and skips the positive scenarios rather than failing —
// an environment-limited run is recorded, never a fabricated pass.

#include "../support/test.hpp"

#include <mirage/platform/windows/windows_desktop_environment.hpp>

// Keep every entry point on the explicit W surface regardless of the
// toolchain's default (MinGW defaults to ANSI).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <string>

namespace {

namespace desktop = mirage::desktop;
using mirage::platform::windows_backend::ApplicationOptions;
using mirage::platform::windows_backend::NotificationsOptions;
using mirage::platform::windows_backend::UiaOptions;
using mirage::platform::windows_backend::Win32Options;
using mirage::platform::windows_backend::WindowsDesktopEnvironment;

/// The scenario that runs against a live carrier: the shared
/// acceptance / refusal matrix. `enabled_environment` must expose a
/// non-null notification provider (the caller checked).
void notification_surface_contract(desktop::NotificationProvider &notifications) {
    // Acceptance through the real tray: ok means the shell took the
    // balloon (never that the user saw it — unobservable by design).
    const desktop::NotificationOutcome posted =
        notifications.notify("Mirage notification test", "posted through the carrier");
    MIRAGE_CHECK(posted.ok);
    MIRAGE_CHECK(!posted.cancelled);
    MIRAGE_CHECK(posted.error.code.empty());

    // Boundary acceptance: exactly the platform's caps still land (63 /
    // 255 UTF-16 code units) — no silent headroom shaving.
    const std::string full_title(63, 'a');
    const std::string full_body(255, 'b');
    const desktop::NotificationOutcome at_cap = notifications.notify(full_title, full_body);
    MIRAGE_CHECK(at_cap.ok);

    // Frozen refusal order, every check before any side effect.
    desktop::CancelToken cancelled;
    cancelled.request_cancel();
    const desktop::NotificationOutcome cancelled_outcome = notifications.notify(
        "Mirage notification test", "body", desktop::NotificationLimits{}, cancelled);
    MIRAGE_CHECK(!cancelled_outcome.ok);
    MIRAGE_CHECK(cancelled_outcome.cancelled);
    MIRAGE_CHECK(cancelled_outcome.error.code == "cancelled");

    const desktop::NotificationOutcome empty_title = notifications.notify("", "body");
    MIRAGE_CHECK(!empty_title.ok);
    MIRAGE_CHECK(empty_title.error.code == "invalid_argument");

    desktop::NotificationLimits tight_title;
    tight_title.max_title_bytes = 4;
    const desktop::NotificationOutcome overlong_title = notifications.notify(
        "Mirage notification test", "body", tight_title, desktop::CancelToken{});
    MIRAGE_CHECK(!overlong_title.ok);
    MIRAGE_CHECK(overlong_title.error.code == "invalid_argument");

    desktop::NotificationLimits tight_body;
    tight_body.max_body_bytes = 4;
    // The body must actually exceed the budget: a 4-byte body under a
    // 4-byte cap is within budget and posts (the frozen check is `>`).
    const desktop::NotificationOutcome overlong_body =
        notifications.notify("title", "over-budget body", tight_body, desktop::CancelToken{});
    MIRAGE_CHECK(!overlong_body.ok);
    MIRAGE_CHECK(overlong_body.error.code == "invalid_argument");

    // Malformed UTF-8 (overlong encoding of 'a') — refused, never repaired.
    const std::string malformed("M\xffirage");
    const desktop::NotificationOutcome invalid_utf8 = notifications.notify(malformed, "body");
    MIRAGE_CHECK(!invalid_utf8.ok);
    MIRAGE_CHECK(invalid_utf8.error.code == "invalid_argument");
}

/// The platform's own payload caps (DEC-018 decision 3), proven by cases
/// the contract byte budgets alone would pass — over-cap payloads refuse
/// with a stable error code instead of being silently truncated.
void platform_payload_caps_refuse_before_effects(desktop::NotificationProvider &notifications) {
    // 64 UTF-16 code units = 64 bytes of ASCII: inside the 256-byte
    // contract title budget, outside the 63-unit balloon array.
    const std::string one_over_title(64, 'a');
    const desktop::NotificationOutcome over_title = notifications.notify(one_over_title, "body");
    MIRAGE_CHECK(!over_title.ok);
    MIRAGE_CHECK(over_title.error.code == "invalid_argument");

    // 64 CJK characters: 192 bytes of UTF-8 (inside the 256-byte contract
    // budget), 64 code units (outside the platform cap) — the cap counts
    // UTF-16 code units, not bytes. Escaped literals keep the source
    // byte-identical on both gate toolchains.
    std::string cjk_over_title;
    for (int index = 0; index < 64; ++index) {
        cjk_over_title += "\xE5\x90\xAF"; // 启 — one character, one code unit
    }
    const desktop::NotificationOutcome cjk_over = notifications.notify(cjk_over_title, "body");
    MIRAGE_CHECK(!cjk_over.ok);
    MIRAGE_CHECK(cjk_over.error.code == "invalid_argument");

    // The boundary twin: 63 CJK characters (189 bytes, 63 code units) fit
    // both caps and land.
    std::string cjk_at_cap;
    for (int index = 0; index < 63; ++index) {
        cjk_at_cap += "\xE5\x90\xAF";
    }
    const desktop::NotificationOutcome cjk_ok = notifications.notify(cjk_at_cap, "CJK at cap");
    MIRAGE_CHECK(cjk_ok.ok);

    // 256 UTF-16 code units = 256 bytes of ASCII: inside the 4096-byte
    // contract body budget, outside the 255-unit balloon array.
    const std::string one_over_body(256, 'b');
    const desktop::NotificationOutcome over_body = notifications.notify("title", one_over_body);
    MIRAGE_CHECK(!over_body.ok);
    MIRAGE_CHECK(over_body.error.code == "invalid_argument");

    // Embedded NUL: the balloon arrays are NUL-terminated, so a NUL byte
    // would silently truncate the payload — refused instead.
    std::string embedded_nul("a");
    embedded_nul.push_back('\0');
    embedded_nul.append("b");
    const desktop::NotificationOutcome nul_title = notifications.notify(embedded_nul, "body");
    MIRAGE_CHECK(!nul_title.ok);
    MIRAGE_CHECK(nul_title.error.code == "invalid_argument");
    const desktop::NotificationOutcome nul_body = notifications.notify("title", embedded_nul);
    MIRAGE_CHECK(!nul_body.ok);
    MIRAGE_CHECK(nul_body.error.code == "invalid_argument");
}

/// Verification round on top of the caps matrix: the sharp edges of the
/// platform boundary and the refusal order that the base matrix cannot
/// separate (same stable error code).
void platform_boundary_sharpness(desktop::NotificationProvider &notifications) {
    // Cancellation outranks the argument checks: a cancelled token refuses
    // with "cancelled" even for a payload that would also be rejected.
    desktop::CancelToken cancelled;
    cancelled.request_cancel();
    const desktop::NotificationOutcome cancel_first =
        notifications.notify("", "body", desktop::NotificationLimits{}, cancelled);
    MIRAGE_CHECK(cancel_first.cancelled);
    MIRAGE_CHECK(cancel_first.error.code == "cancelled");

    // Surrogate pairs count as TWO UTF-16 code units each (the balloon
    // arrays are UTF-16): 32 astral characters are 128 UTF-8 bytes — well
    // inside the 256-byte contract budget — but 64 code units, over the
    // 63-unit cap. The 31-pair twin (62 units) lands. The BMP cases in the
    // caps matrix cannot distinguish code units from characters; this can.
    std::string astral_over;
    for (int index = 0; index < 32; ++index) {
        astral_over += "\xF0\x9F\x98\x80"; // U+1F600: 2 code units, 4 bytes
    }
    const desktop::NotificationOutcome astral_refused = notifications.notify(astral_over, "body");
    MIRAGE_CHECK(!astral_refused.ok);
    MIRAGE_CHECK(astral_refused.error.code == "invalid_argument");

    std::string astral_at_cap;
    for (int index = 0; index < 31; ++index) {
        astral_at_cap += "\xF0\x9F\x98\x80";
    }
    const desktop::NotificationOutcome astral_ok =
        notifications.notify(astral_at_cap, "31 surrogate pairs");
    MIRAGE_CHECK(astral_ok.ok);

    // The caller's byte budget cannot lift the platform cap: a raised
    // budget still refuses the 64-unit title.
    desktop::NotificationLimits raised;
    raised.max_title_bytes = 4096;
    const std::string over_units(64, 'a');
    const desktop::NotificationOutcome still_refused =
        notifications.notify(over_units, "body", raised, desktop::CancelToken{});
    MIRAGE_CHECK(!still_refused.ok);
    MIRAGE_CHECK(still_refused.error.code == "invalid_argument");

    // An empty body carries no refusal in the frozen contract (only the
    // title must be non-empty; the Linux twin delivers an empty body): the
    // shell takes the title-only balloon.
    const desktop::NotificationOutcome empty_body = notifications.notify("Mirage empty body", "");
    MIRAGE_CHECK(empty_body.ok);

    // The body cap counts code units too: 256 CJK characters are 768 UTF-8
    // bytes — far inside the 4096-byte contract budget — but 256 code
    // units, one over the platform cap.
    std::string cjk_body_over;
    for (int index = 0; index < 256; ++index) {
        cjk_body_over += "\xE5\x90\xAF";
    }
    const desktop::NotificationOutcome body_refused = notifications.notify("title", cjk_body_over);
    MIRAGE_CHECK(!body_refused.ok);
    MIRAGE_CHECK(body_refused.error.code == "invalid_argument");
}

} // namespace

int main() {
    // Fail closed without the option: the base-class null default is the
    // honest "capability absent" surface.
    {
        WindowsDesktopEnvironment env;
        MIRAGE_CHECK(env.notification() == nullptr);
    }

    WindowsDesktopEnvironment env{Win32Options{}, UiaOptions{}, ApplicationOptions{},
                                  NotificationsOptions{true}};
    if (env.notification() == nullptr) {
        // The probe failed: either this session has no notification area
        // (service context, shell-less desktop — the DEC-018 decision 2
        // discipline) or the carrier build broke. The run cannot tell the
        // two apart from inside, so it notes loudly and reports the
        // environment limitation instead of fabricating evidence.
        std::fprintf(stderr, "[win32-notify-test] no notification surface in this session "
                             "(open() probe failed); positive scenarios are environment-limited\n");
        std::fflush(stderr);
        return mirage::testing::finish("win32_notification_test");
    }
    desktop::NotificationProvider &notifications = *env.notification();

    // Scenario 1: the frozen contract surface over the real tray.
    notification_surface_contract(notifications);

    // Scenario 2: the platform's own caps, refused before any side effect.
    platform_payload_caps_refuse_before_effects(notifications);

    // Scenario 2b (verification): the boundary's sharp edges — surrogate
    // pairs, budget-independence of the platform cap, the empty body, and
    // the refusal order the base matrix cannot separate.
    platform_boundary_sharpness(notifications);

    // Scenario 3: a second environment in the same process — the carrier
    // class is reused (ERROR_CLASS_ALREADY_EXISTS is the expected path) and
    // a second (hWnd, uID) icon coexists; both tear down cleanly.
    {
        WindowsDesktopEnvironment second{Win32Options{}, UiaOptions{}, ApplicationOptions{},
                                         NotificationsOptions{true}};
        if (second.notification() != nullptr) {
            const desktop::NotificationOutcome posted =
                second.notification()->notify("Mirage second carrier", "coexisting icon");
            MIRAGE_CHECK(posted.ok);
        } else {
            std::fprintf(stderr, "[win32-notify-test] second carrier unavailable in this "
                                 "session; coexistence scenario skipped\n");
            std::fflush(stderr);
        }
    }

    // Scenario 4 (verification): tear a carrier down and open a fresh one —
    // the class registration survives the destructor (the reuse path), the
    // teardown releases the icon slot, and the fresh carrier posts. Strict:
    // the primary carrier just proved the session's tray works, so a failed
    // reopen is a teardown defect, not an environment property.
    {
        WindowsDesktopEnvironment transient{Win32Options{}, UiaOptions{}, ApplicationOptions{},
                                            NotificationsOptions{true}};
        MIRAGE_CHECK(transient.notification() != nullptr);
        if (transient.notification() != nullptr) {
            const desktop::NotificationOutcome before =
                transient.notification()->notify("Mirage reopen", "before teardown");
            MIRAGE_CHECK(before.ok);
        }
    } // NIM_DELETE, then DestroyWindow, in the destructor.
    {
        WindowsDesktopEnvironment reopened{Win32Options{}, UiaOptions{}, ApplicationOptions{},
                                           NotificationsOptions{true}};
        MIRAGE_CHECK(reopened.notification() != nullptr);
        if (reopened.notification() != nullptr) {
            const desktop::NotificationOutcome after =
                reopened.notification()->notify("Mirage reopen", "after teardown");
            MIRAGE_CHECK(after.ok);
        }
    }

    // The destructor of `env` runs here: icon removed (NIM_DELETE) before
    // the carrier window is destroyed.
    return mirage::testing::finish("win32_notification_test");
}
