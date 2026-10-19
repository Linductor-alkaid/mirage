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
    /// DEC-056 settlement input: a successful desktop observation (capture,
    /// OCR, listing) completed after the current trailing record. It lets a
    /// trailing clean no-effect rejection settle on fresh evidence instead of
    /// failing a turn whose goal an earlier verified action already met.
    void note_observation();
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
    bool empty() const { return records_.empty() && archived_.empty(); }
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
        mira::JsonValue observation;
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
    static bool settled(const Record &record);
    // Oldest-first archival of settled records so a Continuous run never
    // refuses its 33rd action (M9-03); returns false only when every kept
    // record still carries unconfirmed side effects.
    bool archive_settled_locked();
    SharedObservation shared_observation_;
    DesktopToolContext desktop_;
    mira::OperationContext owner_;
    mirage::desktop::CancelToken cancel_;
    // M9-03 active-window capacity (DEC-090): `records_` keeps the recent
    // action window; settled records (verified, or a clean rejection with no
    // dispatched effects) archive out of the front instead of refusing new
    // actions. Unconfirmed side effects never archive. `archived_` still
    // resolves receipts for request validation and UI projection; its oldest
    // entries drop only well past the point where the compacted canonical
    // history has stopped replaying them.
    static constexpr std::size_t kMaxRecords = 32;
    static constexpr std::size_t kMaxArchivedRecords = 128;
    std::vector<Record> records_;
    std::vector<Record> archived_;
    bool fenced_ = false;
    bool canonical_failure_ = false;
    // Set by note_observation while records_.back() is a clean no-effect
    // rejection; cleared by the next execute() (any new mutation attempt).
    bool observation_after_rejection_ = false;
    bool ocr_failed_ = false;
    // Host-side visual evidence budget, per environment epoch (M9-03): a new
    // `desktop.actions` epoch reopens the budget the same way the failure
    // tracker's per-epoch retry budget does, so long tasks keep verifying
    // while same-epoch OCR churn still settles with a bounded receipt.
    std::uint64_t visual_epoch_ = 0;
    bool visual_epoch_seen_ = false;
    std::uint32_t visual_epoch_observations_ = 0;
    std::uint64_t last_backend_calls_ = 0;
    // Recent visual scopes for the freshness check; a bounded window instead
    // of an unbounded per-run list (M9-03). Captures mint fresh scope refs,
    // so only replayed ancient evidence can re-enter the window.
    static constexpr std::size_t kMaxVisualScopes = 8;
    std::vector<std::string> visual_scopes_;
};
} // namespace mirage::integration::detail
