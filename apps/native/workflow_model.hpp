#pragma once

#include <mirage/runtime/ipc/protocol.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mirage::native_ui {
namespace workflow_ipc = mirage::runtime::ipc;

// UI-thread-only bounded projection. Runtime remains the state/attempt owner.
class WorkflowModel {
  public:
    struct Request {
        std::string tag;
        std::uint64_t revision;
        workflow_ipc::Request body;
    };
    static constexpr std::size_t max_catalog = 128, max_runs = 256;
    std::vector<workflow_ipc::WorkflowSummary> catalog;
    std::vector<workflow_ipc::WorkflowRunSummary> runs;
    std::optional<workflow_ipc::WorkflowLearningStatus> learning;
    std::optional<workflow_ipc::WorkflowRecoveryStatus> recovery;
    std::string selected_run, notice;
    bool catalog_fresh = false, runs_fresh = false;

    bool busy() const { return pending_ != 0; }
    bool ready() const { return !busy() && runs_fresh; }
    const workflow_ipc::WorkflowRunSummary *selected() const {
        const auto found = std::find_if(
            runs.begin(), runs.end(), [&](const auto &run) { return run.run_id == selected_run; });
        return found == runs.end() ? nullptr : &*found;
    }
    bool select(const std::string &id) {
        if (busy() || !runs_fresh || std::none_of(runs.begin(), runs.end(), [&](const auto &run) {
                return run.run_id == id;
            }))
            return false;
        if (selected_run != id) {
            selected_run = id;
            recovery.reset();
            notice.clear();
        }
        return true;
    }
    void invalidate(const std::string &reason) {
        ++revision_;
        pending_ = 0;
        catalog_fresh = runs_fresh = false;
        learning.reset();
        recovery.reset();
        notice = reason;
    }
    // Events cannot prove intermediate states. Keep the snapshot visible,
    // disable mutations, and let the user explicitly refresh the authority.
    void changed() { invalidate("运行状态有更新，请刷新后继续操作。"); }
    std::vector<Request> refresh() {
        if (busy())
            return {};
        ++revision_;
        pending_ = 7;
        catalog_fresh = runs_fresh = false;
        learning.reset();
        notice.clear();
        return {{"workflow.catalog", revision_, workflow_ipc::WorkflowListRequest{}},
                {"workflow.runs", revision_, workflow_ipc::WorkflowRunsRequest{}},
                {"workflow.learning", revision_, workflow_ipc::WorkflowLearningStatusRequest{}}};
    }
    std::optional<Request> run(const std::string &id) {
        if (busy() || !catalog_fresh || !runs_fresh)
            return {};
        const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const auto &item) {
            return item.workflow_id == id && item.runnable;
        });
        if (found == catalog.end())
            return {};
        return begin("workflow.run", id,
                     workflow_ipc::WorkflowRunRequest{id, found->head_digest, "{}", ""});
    }
    bool allowed(const std::string &action) const {
        const auto *run = selected();
        if (!ready() || !run)
            return false;
        if (action == "status")
            return true;
        if (action == "start")
            return run->state == "waiting_agent" && !recovery;
        if (action == "cancel")
            return recovery && !recovery->settled;
        if (action == "lesson")
            return run->state == "completed" && learning && learning->enabled && recovery &&
                   recovery->settled &&
                   (recovery->outcome == "patched-and-resumed" ||
                    recovery->outcome == "resumed-without-patch");
        if (action == "stop")
            return !terminal(run->state);
        return false;
    }
    std::optional<Request> control(const std::string &action) {
        if (!allowed(action))
            return {};
        if (action == "stop")
            return begin("workflow.stop", selected_run,
                         workflow_ipc::WorkflowCancelRunRequest{selected_run});
        return begin("workflow." + action, selected_run,
                     workflow_ipc::WorkflowRecoveryRequest{selected_run, action});
    }
    // Ignore stale revisions or unknown tags. Matching replies release the UI slot;
    // validate the expected payload before changing any displayed facts.
    bool accept(const std::string &tag, std::uint64_t revision,
                const workflow_ipc::Response &response) {
        const unsigned bit = tag == "workflow.catalog"    ? 1U
                             : tag == "workflow.runs"     ? 2U
                             : tag == "workflow.learning" ? 4U
                                                          : 8U;
        if (revision != revision_ || !(pending_ & bit) || (bit == 8 && tag != operation_))
            return false;
        pending_ &= ~bit;
        if (!response.ok) {
            if (bit == 8)
                runs_fresh = false; // A failed reply cannot prove a control had no effect.
            notice = response.error.message.size() <= 2048
                         ? std::string(bit == 8 ? "操作未完成，请先刷新确认运行状态。"
                                                : "操作未完成：") +
                               response.error.message
                         : "服务返回错误，请刷新后重试。";
            return true;
        }
        bool valid = false;
        if (bit == 1) {
            if (const auto *value = std::get_if<workflow_ipc::WorkflowList>(&response.payload);
                value && valid_catalog(*value)) {
                catalog = value->workflows;
                catalog_fresh = valid = true;
            }
        } else if (bit == 2) {
            if (const auto *value = std::get_if<workflow_ipc::WorkflowRunList>(&response.payload);
                value && valid_runs(*value)) {
                runs = value->runs;
                std::sort(runs.begin(), runs.end(), [](const auto &a, const auto &b) {
                    return a.created_at_ms == b.created_at_ms ? a.run_id < b.run_id
                                                              : a.created_at_ms > b.created_at_ms;
                });
                if (!selected()) {
                    selected_run.clear();
                    recovery.reset();
                }
                runs_fresh = valid = true;
            }
        } else if (bit == 4) {
            if (const auto *value =
                    std::get_if<workflow_ipc::WorkflowLearningStatus>(&response.payload)) {
                learning = *value;
                valid = true;
            }
        } else if (tag == "workflow.run") {
            if (const auto *value =
                    std::get_if<workflow_ipc::WorkflowRunStarted>(&response.payload);
                value && hex(value->run_id, 32)) {
                selected_run = value->run_id;
                recovery.reset();
                runs_fresh = false;
                notice = "运行已接纳。请刷新查看实际状态。";
                valid = true;
            }
        } else if (tag == "workflow.stop") {
            if (const auto *value =
                    std::get_if<workflow_ipc::WorkflowRunCancelled>(&response.payload);
                value && value->run_id == target_ && valid_state(value->state)) {
                runs_fresh = false;
                notice = "已请求停止运行。请刷新确认状态。";
                valid = true;
            }
        } else if (const auto *value =
                       std::get_if<workflow_ipc::WorkflowRecoveryStatus>(&response.payload);
                   value && value->run_id == target_ && "workflow." + value->action == tag) {
            if (value->action == "lesson")
                notice = "成功恢复经验已保存。";
            else if (value->action == "cancel")
                notice = "已请求取消恢复；查询结果确认结算，运行由服务继续管理。";
            else {
                recovery = *value;
                notice = value->settled ? "恢复尝试已结算；请刷新确认运行结果。"
                                        : "恢复尝试尚未结算，可查询结果或取消恢复。";
            }
            valid = true;
        }
        if (!valid) {
            if (bit == 8)
                runs_fresh = false;
            notice = "服务响应与请求不匹配或超出显示范围，请刷新后重试。";
        }
        return true;
    }
    static bool terminal(const std::string &s) {
        return s == "completed" || s == "failed" || s == "cancelled";
    }
    static std::string state_label(const std::string &s) {
        return s == "created"         ? "已创建"
               : s == "running"       ? "运行中"
               : s == "paused"        ? "已暂停"
               : s == "waiting_agent" ? "等待 Agent"
               : s == "waiting_user"  ? "等待用户"
               : s == "completed"     ? "已完成"
               : s == "failed"        ? "失败"
               : s == "cancelled"     ? "已取消"
                                      : "状态未知";
    }

  private:
    std::uint64_t revision_ = 0;
    unsigned pending_ = 0;
    std::string operation_, target_;
    Request begin(std::string tag, std::string target, workflow_ipc::Request body) {
        ++revision_;
        pending_ = 8;
        operation_ = tag;
        target_ = std::move(target);
        notice.clear();
        return {std::move(tag), revision_, std::move(body)};
    }
    static bool hex(const std::string &s, std::size_t size) {
        return s.size() == size && s.find_first_not_of("0123456789abcdef") == std::string::npos &&
               (size != 32 || s.find_first_not_of('0') != std::string::npos);
    }
    static bool valid_state(const std::string &s) {
        return terminal(s) || s == "created" || s == "running" || s == "paused" ||
               s == "waiting_user" || s == "waiting_agent";
    }
    static bool valid_catalog(const workflow_ipc::WorkflowList &value) {
        if (value.workflows.size() > max_catalog)
            return false;
        std::set<std::string> ids;
        std::size_t bytes = 0;
        for (const auto &item : value.workflows) {
            bytes += item.name.size();
            if (!hex(item.workflow_id, 32) || !hex(item.head_digest, 64) ||
                item.name.size() > 4096 || bytes > 256 * 1024 ||
                !ids.insert(item.workflow_id).second)
                return false;
        }
        return true;
    }
    static bool valid_runs(const workflow_ipc::WorkflowRunList &value) {
        if (value.runs.size() > max_runs)
            return false;
        std::set<std::string> ids;
        for (const auto &item : value.runs)
            if (!hex(item.run_id, 32) || !hex(item.workflow_id, 32) || !valid_state(item.state) ||
                !ids.insert(item.run_id).second)
                return false;
        return true;
    }
};
} // namespace mirage::native_ui
