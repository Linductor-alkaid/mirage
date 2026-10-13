#include <mirage/runtime/pet/status.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mirage::runtime::pet {
namespace {
bool host_name(std::string_view state) {
    return state == "stopped" || state == "starting" || state == "running" || state == "stopping" ||
           state == "failed";
}
bool source_name(std::string_view value) {
    return value.empty() || value == "task" || value == "turn" || value == "workflow";
}
bool active(const Work &work) {
    return work.state == "queued" || work.state == "active" || work.state == "paused" ||
           work.state == "cancelling";
}
bool executing_phase(std::string_view value) {
    return value == "thinking" || value == "looking" || value == "mouse" || value == "typing";
}
bool same_source(const Work &work, const Status &status) {
    return work.source_kind == status.source_kind && work.source_id == status.source_id;
}
bool same_presentation(Status a, Status b) {
    a.revision = b.revision = a.elapsed_ms = b.elapsed_ms = 0;
    return a == b;
}
bool same_phase(Status a, Status b) {
    a.active_count = b.active_count = 0;
    return same_presentation(std::move(a), std::move(b));
}
} // namespace

bool known_display_state(std::string_view state) {
    constexpr std::string_view values[] = {
        "starting", "idle",         "thinking",     "looking",      "mouse",
        "typing",   "task_success", "task_failure", "exiting",      "waiting",
        "paused",   "takeover",     "cancelled",    "disconnected", "unknown"};
    return std::find(std::begin(values), std::end(values), state) != std::end(values);
}
std::string_view running_tool_phase(std::string_view name) {
    if (name == "desktop.input.type_text" || name == "desktop.input.key" ||
        name == "desktop.session.type_text" || name == "desktop.session.key")
        return "typing";
    if (name == "desktop.input.click" || name == "desktop.session.click" ||
        name == "desktop.session.scroll" || name == "desktop.vision.click" ||
        name == "desktop.vision.click_region" || name == "desktop.vision.drag" ||
        name == "desktop.vision.scroll" || name == "desktop.knowledge.visual_click")
        return "mouse";
    if (name == "desktop.screen.capture" || name == "desktop.screen.list_displays" ||
        name == "desktop.vision.ocr" || name == "desktop.vision.mark" ||
        name == "desktop.accessibility.semantic_snapshot" || name == "desktop.window.front" ||
        name == "desktop.window.list" || name == "desktop.application.list")
        return "looking";
    return "unknown";
}
const char *state_label(std::string_view state) {
    if (state == "starting")
        return "启动中";
    if (state == "idle")
        return "待机";
    if (state == "thinking")
        return "思考中";
    if (state == "looking")
        return "观察中";
    if (state == "mouse")
        return "鼠标操作";
    if (state == "typing")
        return "键盘输入";
    if (state == "task_success")
        return "任务成功";
    if (state == "task_failure")
        return "任务失败";
    if (state == "exiting")
        return "正在退出";
    if (state == "waiting")
        return "等待确认";
    if (state == "paused")
        return "已暂停";
    if (state == "takeover")
        return "人工接管";
    if (state == "cancelled")
        return "已取消";
    if (state == "disconnected")
        return "连接中断";
    return "执行中";
}
bool valid_status(const Status &status) {
    const auto fits = [](const std::string &text, std::size_t bound) {
        return text.size() <= bound && text.find('\0') == std::string::npos;
    };
    constexpr auto wire_integer_max =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    return status.schema_version == 1 && !status.owner_epoch.empty() &&
           fits(status.owner_epoch, max_identity_bytes) && status.revision > 0 &&
           status.revision <= wire_integer_max && status.elapsed_ms <= wire_integer_max &&
           host_name(status.host_state) && known_display_state(status.display_state) &&
           source_name(status.source_kind) && fits(status.source_id, max_identity_bytes) &&
           fits(status.session_id, max_identity_bytes) &&
           (status.source_kind.empty() == status.source_id.empty()) &&
           status.active_count <= max_work_items && fits(status.summary, max_summary_bytes) &&
           fits(status.reason_code, 64);
}

Projection::Projection(std::string owner_epoch) {
    current_.owner_epoch = std::move(owner_epoch);
    current_.revision = 1;
    if (!valid_status(current_))
        throw std::invalid_argument("invalid pet owner epoch");
}
Status Projection::update(std::string_view host, std::span<const Work> work, bool takeover,
                          bool waiting_permission, Clock::time_point now) {
    Status next;
    next.owner_epoch = current_.owner_epoch;
    next.host_state = host_name(host) ? std::string{host} : "failed";
    next.revision = current_.revision;
    next.active_count = static_cast<std::size_t>(std::count_if(work.begin(), work.end(), active));
    const auto selected = std::find_if(
        work.begin(), work.end(), [&](const auto &item) { return same_source(item, current_); });
    auto completed = selected;
    for (auto item = work.begin(); item != work.end(); ++item) {
        if (item->state != "completed" && item->state != "failed" && item->state != "cancelled")
            continue;
        if (item->completion_order > completed_seen_ &&
            (completed == work.end() || item->completion_order > completed->completion_order))
            completed = item;
    }
    const auto bind = [&](const Work &item) {
        next.source_kind = item.source_kind;
        next.source_id = item.source_id;
        next.session_id = item.session_id;
    };
    if (work.size() > max_work_items) {
        next.active_count = std::min(next.active_count, max_work_items);
        next.reason_code = "projection_capacity";
    } else if (host == "starting") {
        next.display_state = "starting";
    } else if (host == "stopping" || host == "stopped") {
        next.display_state = "exiting";
    } else if (next.host_state == "failed") {
        next.display_state = "task_failure";
        next.reason_code = "host_failed";
    } else if (takeover) {
        next.display_state = "takeover";
    } else if (waiting_permission) {
        next.display_state = "waiting";
    } else if (next.active_count) {
        auto choice = std::find_if(work.begin(), work.end(), [](const auto &item) {
            return active(item) && item.input_owner;
        });
        if (choice == work.end() && selected != work.end() && active(*selected))
            choice = selected;
        if (choice == work.end())
            choice = std::find_if(work.begin(), work.end(), active);
        bind(*choice);
        next.display_state = choice->state == "cancelling" ? "cancelled"
                             : choice->state == "paused"   ? "paused"
                             : choice->state == "active" && executing_phase(choice->phase)
                                 ? choice->phase
                                 : "unknown";
        if (next.display_state == "unknown")
            next.reason_code = choice->state == "queued" ? "queued" : "phase_unavailable";
    } else if (completed != work.end() &&
               (completed->state == "completed" || completed->state == "failed" ||
                completed->state == "cancelled")) {
        const std::string terminal = completed->state == "completed" ? "task_success"
                                     : completed->state == "failed"  ? "task_failure"
                                                                     : "cancelled";
        const bool expired = same_source(*completed, current_) &&
                             current_.display_state == terminal && terminal != "task_failure" &&
                             now - changed_at_ >= std::chrono::seconds{5};
        if (!expired) {
            bind(*completed);
            next.display_state = terminal;
        } else {
            next.display_state = "idle";
        }
    } else {
        next.display_state = "idle";
    }
    for (const auto &item : work)
        completed_seen_ = std::max(completed_seen_, item.completion_order);
    next.summary = state_label(next.display_state);
    if (next.display_state == "cancelled" && next.active_count)
        next.summary = "正在取消";
    if (!valid_status(next)) {
        next.source_kind.clear();
        next.source_id.clear();
        next.session_id.clear();
        next.display_state = "unknown";
        next.reason_code = "invalid_source";
        next.summary = state_label(next.display_state);
    }
    if (!initialized_ || !same_phase(next, current_))
        changed_at_ = now;
    if (initialized_ && !same_presentation(next, current_))
        ++next.revision;
    initialized_ = true;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - changed_at_);
    next.elapsed_ms = static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count()));
    current_ = next;
    return next;
}
} // namespace mirage::runtime::pet
