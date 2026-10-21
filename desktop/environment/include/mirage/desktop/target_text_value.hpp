#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/element_reference.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

class AccessibilityProvider;

namespace text_value_policy {
inline constexpr std::uint32_t version = 1;
inline constexpr std::size_t max_bytes = 4096;
inline constexpr std::chrono::milliseconds timeout{5000};
} // namespace text_value_policy

struct TextValueReadLimits {
    std::size_t max_bytes = text_value_policy::max_bytes;
    std::chrono::milliseconds timeout = text_value_policy::timeout;
};

enum class TextValueSource { none, accessibility };

/// Temporary plaintext, never a receipt or a log payload. The backend must
/// revalidate the exact scoped object, focus, protection and committed state
/// before and after its one complete read. No name/coordinate hint fallback.
struct TextValueReadOutcome {
    bool ok = false;
    bool cancelled = false;
    ProviderError error;
    std::string reference, snapshot_scope;
    std::string utf8_text;
    TextValueSource source = TextValueSource::none;
    bool complete = false, committed = false, protected_content = false;
    std::chrono::steady_clock::time_point read_started{}, read_finished{};
};

/// Immutable at the caller's action boundary. This compares a complete value,
/// including an empty value; an insertion payload alone is not an expected value.
struct TextValueCondition {
    ElementTarget target;
    std::string expected;
};

enum class TextValueStatus { matched, mismatched, verification_unavailable };

/// Metadata only: never retains actual or expected plaintext. This is not an
/// input execution receipt and cannot clear execution/authority/epoch fences.
struct TextValueVerification {
    std::uint32_t version = text_value_policy::version;
    TextValueStatus status = TextValueStatus::verification_unavailable;
    std::string reason;
    TextValueSource source = TextValueSource::none;
    std::string reference, snapshot_scope;
    std::chrono::steady_clock::time_point read_started{}, read_finished{};
};

/// Exactly one fresh, read-only attempt. No input, OCR, retries, fuzzy equality
/// or weaker fallback. The caller owns task/authority/epoch checks and dispatches
/// through its existing managed context. No backend currently opts in by default.
TextValueVerification verify_text_value(AccessibilityProvider &, const TextValueCondition &,
                                        const TextValueReadLimits &, const CancelToken &);

} // namespace mirage::desktop
