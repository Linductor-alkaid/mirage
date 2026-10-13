#pragma once
#include <mira/conversation_loop.hpp>
#include <mirage/conversation.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>

namespace mirage::integration::detail {
// Per-turn product postconditions; owned by the serialized harness caller.
class DesktopActionVerification {
  public:
    DesktopActionVerification(DesktopToolContext desktop, mira::OperationContext owner,
                              mirage::desktop::CancelToken cancel);
    static bool mutation(const std::string &name);
    mira::Result<mira::JsonValue> execute(const std::string &, const mira::JsonValue &,
                                          const mira::OperationContext &,
                                          const mira::BuiltinToolHandler &);
    using SharedObservation = std::function<mira::Result<mira::JsonValue>(
        const std::string &, const std::string &, const mira::OperationContext &)>;
    void set_shared_observation(SharedObservation callback) {
        shared_observation_ = std::move(callback);
    }
    void register_tool(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &);
    mira::Result<std::vector<mira::ModelInputItem>> supply(const mira::AgentLoopSpec &) const;
    bool settle();
    bool no_effect_rejection() const;
    mira::Result<mira::JsonValue> recheck(const std::string &action_id,
                                          const mira::OperationContext &);
    mira::Result<void> validate_request(const mira::ModelRequest &, const mira::OperationContext &);
    void project(std::vector<conversation::Part> &) const;
    bool empty() const { return records_.empty(); }
    bool ocr_failed() const { return ocr_failed_; }

  private:
    struct Record {
        std::string id, action, window, kind, title, role, name, text, application_id;
        // DEC-067 private host constraints; ordinary model tool schemas do not expose these.
        std::string application, page_title, page_text;
        std::string visual_scope, display_id, model_revision;
        mirage::desktop::WindowGeometry display_geometry;
        mirage::desktop::WindowQueryOutcome initial_front;
        std::uint64_t backend_calls = 0;
        std::string status = "unverified", reason = "condition_missing", evidence;
        std::string observed_digest;
        std::int64_t captured_at_ms = 0;
        std::uint64_t epoch = 0;
        bool dispatched = false, uncertain = false, input_released = false;
        mira::Sha256Digest condition_digest;
    };
    void observe(Record &, const mira::OperationContext &);
    static std::string failure_receipt(const Record &, std::string message,
                                       const std::string &field = {}, std::string code = {},
                                       const std::string &refreshed_scope = {});
    bool observe_text(Record &, const mirage::desktop::WindowGeometry &,
                      const mirage::desktop::CancelToken &,
                      std::chrono::steady_clock::time_point deadline,
                      const mira::OperationContext &, std::int64_t &matching_nodes,
                      std::string &visual_digest);
    mira::JsonValue summary(const Record &) const;
    SharedObservation shared_observation_;
    DesktopToolContext desktop_;
    mira::OperationContext owner_;
    mirage::desktop::CancelToken cancel_;
    std::vector<Record> records_;
    bool fenced_ = false;
    bool canonical_failure_ = false;
    bool ocr_failed_ = false;
    std::size_t visual_observations_ = 0;
    std::uint64_t last_backend_calls_ = 0;
    std::vector<std::string> visual_scopes_;
};
} // namespace mirage::integration::detail
