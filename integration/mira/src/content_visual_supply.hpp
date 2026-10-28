#pragma once
#include <algorithm>
#include <chrono>
#include <mira/json.hpp>
#include <optional>
#include <string>

namespace mirage::integration::detail {
// DEC-135: admission by elapsed time on the existing caller, never a scheduler.
class ContentVisualSupply {
    using Clock = std::chrono::steady_clock;
    Clock::time_point next_{};
    std::string reason_ = "disabled";
    std::optional<std::int64_t> failure_code_;

  public:
    static constexpr std::chrono::seconds interval{5};
    void reset() {
        next_ = {};
        reason_ = "awaiting_supplier";
        failure_code_.reset();
    }
    bool ready(Clock::time_point now) const { return now >= next_; }
    void attempted(Clock::time_point now) { next_ = now + interval; }
    void completed(Clock::time_point now) { next_ = std::max(next_, now + interval); }
    void reason(const char *value) {
        reason_ = value;
        failure_code_.reset();
    }
    void failed(std::int64_t code) {
        reason_ = "packaging_or_publish_failed";
        failure_code_ = code;
    }
    std::chrono::milliseconds retry_after(Clock::time_point now) const {
        return std::chrono::milliseconds{std::clamp<std::int64_t>(
            std::chrono::ceil<std::chrono::milliseconds>(next_ - now).count(), 0, 5000)};
    }
    mira::JsonValue project(Clock::time_point now = Clock::now()) const {
        using Json = mira::JsonValue;
        return Json{Json::Object{{"reason", reason_},
                                 {"failure_code", failure_code_ ? Json{*failure_code_} : Json{}},
                                 {"retry_after_ms", retry_after(now).count()},
                                 {"minimum_interval_ms", 5000},
                                 {"window_ms", 30000},
                                 {"input_actionable", false}}};
    }
};
} // namespace mirage::integration::detail
