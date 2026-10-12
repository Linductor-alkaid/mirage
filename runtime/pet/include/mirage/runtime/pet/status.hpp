#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace mirage::runtime::pet {
// DEC-084: presentation only. Never an execution/permission/monitoring authority.
inline constexpr std::size_t max_work_items = 1024;
inline constexpr std::size_t max_summary_bytes = 256;
inline constexpr std::size_t max_identity_bytes = 128;
inline constexpr std::size_t max_status_wire_bytes = 4096;

struct Status {
    int schema_version = 1;
    std::string owner_epoch;
    std::uint64_t revision = 0;
    std::string host_state = "stopped";
    std::string display_state = "unknown";
    std::string source_kind; // task / turn / workflow, empty for global states
    std::string source_id;
    std::string session_id;
    std::size_t active_count = 0;
    std::string summary;
    std::string reason_code;
    std::uint64_t elapsed_ms = 0; // elapsed duration, never an inter-process clock timestamp
    friend bool operator==(const Status &, const Status &) = default;
};

bool valid_status(const Status &status);
bool known_display_state(std::string_view state);
const char *state_label(std::string_view state);
// Presentation hint for a tool that has actually entered its running stage.
// Unknown tools remain unknown; a planned tool call is not desktop activity.
std::string_view running_tool_phase(std::string_view tool_name);

// One fresh view from an existing bounded Runtime registry. No retained task table.
struct Work {
    std::string source_kind;
    std::string source_id;
    std::string session_id;
    std::string state;             // queued / active / paused / completed / failed / cancelled
    std::string phase = "unknown"; // only independently evidenced executing phases
    bool input_owner = false;
    std::uint64_t completion_order = 0; // current-owner watermark; 0 for restored history
};

// Called exclusively on the Runtime serial domain. Retains only the current
// presentation and its clock; the supplied registries remain the facts of work.
class Projection {
  public:
    using Clock = std::chrono::steady_clock;
    explicit Projection(std::string owner_epoch);
    const Status &current() const { return current_; }
    std::uint64_t completed_seen() const { return completed_seen_; }
    Status update(std::string_view host, std::span<const Work> work, bool takeover,
                  bool waiting_permission, Clock::time_point now);

  private:
    Status current_;
    Clock::time_point changed_at_{};
    bool initialized_ = false;
    std::uint64_t completed_seen_ = 0;
};
} // namespace mirage::runtime::pet
