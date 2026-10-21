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
    static bool observation(const std::string &name);
    /// Harness policy v2: explicit, bounded condition dependencies; major
    /// tool version isolates these semantics from historical receipts.
    static void configure_policy(mira::BuiltinToolSpec &);
    /// DEC-056 v2: record a successful desktop observation after input, using
    /// the current action and authority epochs. Settlement consumes this
    /// bounded checkpoint without capturing. Ordinary read-only tools are
    /// not observations; callers use observation(name) to classify them.
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
        std::uint64_t epoch = 0, authority = 0;
        std::chrono::steady_clock::time_point observed_at{};
        std::vector<std::string> dependencies;
        bool dispatched = false, uncertain = false, input_released = false;
        mira::Sha256Digest condition_digest;
        // Historical recovery facts only; never current dispatch authority.
        // Digest excludes volatile image scope/epoch; no input plaintext copied.
        std::string attempt_identity, failure_code, failure_field, recovery_hint;
    };
    void observe(Record &, const mira::OperationContext &);
    static std::string failure_receipt(const Record &, std::string message,
                                       const std::string &field = {}, std::string code = {},
                                       const std::string &refreshed_scope = {},
                                       const std::string &hint = {});
    bool observe_text(Record &, const mirage::desktop::WindowGeometry &,
                      const mirage::desktop::CancelToken &,
                      std::chrono::steady_clock::time_point deadline,
                      const mira::OperationContext &, std::int64_t &matching_nodes,
                      std::string &visual_digest);
    mira::JsonValue summary(const Record &) const;
    mira::JsonValue recovery_facts() const;
    static bool settled(const Record &record);
    Record *find_record(const std::string &);
    bool fresh_feedback() const;
    // Oldest-first archival of settled records so a Continuous run never
    // refuses its 33rd action (M9-03); returns false only when every kept
    // record still carries unconfirmed side effects.
    bool archive_settled_locked();
    SharedObservation shared_observation_;
    DesktopToolContext desktop_;
    mira::OperationContext owner_;
    mirage::desktop::CancelToken cancel_;
    // M9-03 active-window capacity (DEC-090): `records_` keeps the recent
    // action window; settled records (verified, completed without requested
    // conditions, or a clean rejection) archive instead of refusing new
    // actions. Unmet conditions and uncertain effects never archive. `archived_` still
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
    std::uint64_t observation_epoch_ = 0, observation_authority_ = 0;
    std::chrono::steady_clock::time_point observation_at_{};
    bool request_feedback_valid_ = false;
    std::uint64_t request_feedback_epoch_ = 0, request_feedback_authority_ = 0;
    // Fresh condition IDs supplied to this request; consumed by its next
    // mutation attempt without recapturing and invalidating visual targets.
    std::vector<std::string> request_conditions_;
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
