#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mirage::runtime::pet {
inline constexpr std::size_t max_bubble_text_bytes = 2048;
struct Status;
struct Preview {
    std::string request_id;
    std::string text;
    std::uint64_t sequence = 0;
    bool truncated = false;
};
struct Bubble {
    std::string kind;           // thinking / dialog
    std::string content_origin; // status_hint / model_preview / answer_summary
    std::string request_id;
    std::string text;
    std::uint64_t sequence = 0;
    bool truncated = false;
    bool final = false;
    std::uint64_t remaining_ms = 0;
    friend bool operator==(const Bubble &, const Bubble &) = default;
};
bool valid_bubble(const Bubble &bubble);
// Empty on malformed UTF-8. Collapses whitespace, removes control bytes and
// preserves complete latest code points within the bound.
std::string bubble_line(std::string_view text, std::size_t bound = max_bubble_text_bytes);
// Conservative extractive projection; empty when no complete short core phrase
// can preserve its qualifications. Never merely cuts a sentence to N characters.
// During streaming, only delimited sentences/lines are eligible. Canonical
// replies may also use a complete trailing phrase without punctuation.
std::string answer_summary(std::string_view text, bool complete = true);
class BubbleProjection {
  public:
    using Clock = std::chrono::steady_clock;
    std::optional<Bubble> update(const Status &, bool model_active, std::string_view final_reply,
                                 const Preview *thinking, const Preview *answer,
                                 Clock::time_point now);

  private:
    std::string source_, request_, pending_answer_;
    std::uint64_t consumed_ = 0;
    bool has_answer_ = false, settled_ = false;
    Bubble bubble_;
    Clock::time_point summary_at_{}, settled_at_{};
};
} // namespace mirage::runtime::pet
