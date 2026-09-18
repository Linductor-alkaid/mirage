#pragma once

#include <cstddef>
#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Budget for one clipboard text read (RULE-07). Content beyond the cap is
/// refused — never silently truncated.
struct ClipboardReadLimits {
    std::size_t max_bytes = 1u << 20;
};

/// Budget for one clipboard text write. Longer payloads are refused before
/// the clipboard is touched.
struct ClipboardWriteLimits {
    std::size_t max_bytes = 1u << 20;
};

struct ClipboardReadOutcome {
    bool ok = false;
    std::string content;
    ProviderError error; ///< meaningful only when ok is false
};

struct ClipboardWriteOutcome {
    bool ok = false;
    bool cancelled = false;
    ProviderError error; ///< meaningful only when ok is false
};

/// Plain-text clipboard access (design doc section 5). Non-text clipboard
/// content (images, private formats) is out of contract scope: reads report
/// "unsupported_content" instead of returning substitutes. Both directions
/// are real side effects on shared machine state — callers judge the
/// `clipboard.read` / `clipboard.write` capabilities through the runtime
/// permission gate (RULE-05, DEC-010) before invoking. The provider stays
/// permission-agnostic. Methods are synchronous and bounded; callers decide
/// the execution context.
class ClipboardProvider {
  public:
    virtual ~ClipboardProvider() = default;

    /// Reads the current clipboard as UTF-8 text; fails closed with a stable
    /// ProviderError when the clipboard is empty ("not_found"), holds
    /// non-text content, or exceeds the read budget.
    virtual ClipboardReadOutcome read_text(const ClipboardReadLimits &limits,
                                           const CancelToken &cancel) = 0;

    ClipboardReadOutcome read_text() { return read_text(ClipboardReadLimits{}, CancelToken{}); }

    /// Replaces the clipboard content with UTF-8 text. An over-budget write
    /// payload is refused with "invalid_argument" before the clipboard is
    /// touched — caller-supplied payloads follow the M1-05 process-contract
    /// precedent, while the read budget above caps environment-supplied
    /// content and reports "clipboard_too_large" instead.
    virtual ClipboardWriteOutcome write_text(const std::string &text,
                                             const ClipboardWriteLimits &limits,
                                             const CancelToken &cancel) = 0;

    ClipboardWriteOutcome write_text(const std::string &text) {
        return write_text(text, ClipboardWriteLimits{}, CancelToken{});
    }
};

} // namespace mirage::desktop
