#pragma once
#include "phase_timing.hpp"
#include <kairo/comm/mailbox.hpp>
#include <mira/conversation_loop.hpp>
#include <mira/model_dialect.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>
#include <mutex>
#include <optional>
#include <string>

#include <functional>
#include <optional>

namespace mirage::integration::detail {
inline constexpr std::size_t desktop_image_bytes = 8u << 20;
inline constexpr std::size_t desktop_capture_bytes = 32u << 20;
// Packaging only: PNG RGB8, stored DEFLATE, no resizing or perception policy.
mira::Result<std::vector<std::byte>> encode_desktop_png(const mirage::desktop::ImageFrame &,
                                                        const mirage::desktop::CancelToken &);

class DesktopImageSource final : public mira::IArtifactSource {
  public:
    using Store = std::shared_ptr<mira::MemoryArtifactStore>;
    bool bind(Store value) { return current_.try_publish(std::move(value)); }
    kairo::comm::CommStats stats() const { return current_.stats(); }
    mira::Result<std::vector<std::byte>> fetch(const mira::ArtifactRef &) override;
    // DEC-088 capability 1: identity of consecutively re-offered desktop
    // captures. The ModelLayer's source outlives single harness turns, so the
    // count survives host reconstruction for the same task; the pinned loop
    // renders the annotation. Content or task/authority/source mapping changes
    // reset it; ordinary action epochs do not erase visual no-progress facts.
    mira::InputReuseMeta record_input_reuse(const std::string &digest, const std::string &scope);

  private:
    kairo::comm::LatestMailbox<Store> current_;
    mutable std::mutex reuse_mutex_;
    std::string reuse_digest_;
    std::string reuse_scope_;
    std::uint32_t reuse_count_ = 0;
};

// Scoped to one serialized harness task; no image state survives the turn.
class DesktopImageTurn {
  public:
    /// `acquire_budget` bounds one capture acquisition; `image_max_age` is the
    /// freshness window `supply()`/`validate_request()` enforce from the
    /// capture start. Both default to the production values. Sanitizer builds
    /// pass larger values because the same work (PNG/DEFLATE on a full frame)
    /// runs several times slower.
    DesktopImageTurn(std::shared_ptr<DesktopImageSource>, DesktopToolContext,
                     mira::OperationContext, mirage::desktop::CancelToken,
                     bool supply_images = true,
                     std::chrono::milliseconds acquire_budget = std::chrono::seconds{5},
                     std::chrono::milliseconds image_max_age = std::chrono::seconds{5});
    ~DesktopImageTurn();
    DesktopImageTurn(const DesktopImageTurn &) = delete;
    DesktopImageTurn &operator=(const DesktopImageTurn &) = delete;
    void register_tool(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &,
                       bool allow_mutations = true);
    /// DEC-088 capability 5: supply-time context-budget signal, anchored on
    /// the last provider-reported input of the session (DEC-087). A nullopt
    /// return keeps the full image: no declared window, no anchor yet, or no
    /// signal wired — degradation fails open.
    using BudgetRemaining = std::function<std::optional<std::int64_t>()>;
    void set_budget_remaining(BudgetRemaining provider);
    void set_timings(conversation::Timings *timings) { timings_ = timings; }
    mira::Result<std::vector<mira::ModelInputItem>> supply(const mira::AgentLoopSpec &);
    /// DEC-088 capability 1: resolves the reuse identity of this request's
    /// supplied capture under the same owner/cancellation gate as supply();
    /// nullopt when the request carries no desktop image, so the count never
    /// advances for text-only requests. Resolved once per assembled request,
    /// right after supply().
    std::optional<mira::InputReuseMeta> input_reuse_identity(const mira::AgentLoopSpec &) const;
    mira::Result<void> validate_request(const mira::ModelRequest &, const mira::OperationContext &);
    mira::Result<mira::JsonValue> observe_shared(const std::string &, const std::string &,
                                                 const mira::OperationContext &);
    bool supplied_image() const { return supplied_image_; }
    // DEC-069: workflow-local fresh target resolution through the same numbered action path.
    mira::Result<mira::JsonValue> local_click(const mira::JsonValue &,
                                              const mira::OperationContext &);

  private:
    mira::Result<mira::JsonValue> capture(const mira::JsonValue &, const mira::OperationContext &);
    mira::Result<mira::JsonValue> mark(const mira::JsonValue &, const mira::OperationContext &);
    mira::Result<mira::JsonValue> click_mark(const mira::JsonValue &,
                                             const mira::OperationContext &);
    mira::Result<mira::JsonValue> pointer_mark(const mira::JsonValue &,
                                               const mira::OperationContext &,
                                               const std::string &operation);
    mira::Result<mira::JsonValue>
    publish_image(const desktop::ImageFrame &, const desktop::WindowGeometry &, const std::string &,
                  const std::string &, std::uint64_t, std::chrono::steady_clock::time_point,
                  const desktop::CancelToken &, mira::JsonValue::Object extra = {});
    /// M7-19: preview-scales, publishes and retires one raw native frame with
    /// the full observation tail (shared by capture_desktop and the
    /// target_pixels_changed rejection refresh).
    mira::Result<mira::JsonValue>
    finish_native_publish(desktop::ImageFrame publish_frame, const desktop::ImageFrame &raw_frame,
                          const desktop::WindowGeometry &area, const std::string &kind,
                          const std::string &kind_id, std::uint64_t source_sequence,
                          std::uint64_t epoch);
    void register_mark_tools(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &,
                             bool);
    void register_region_tool(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &,
                              bool);
    mira::Result<mira::JsonValue> pointer_region(const mira::JsonValue &,
                                                 const mira::OperationContext &,
                                                 const std::string &operation);
    mira::Result<mira::JsonValue> capture_desktop(const mira::JsonValue &,
                                                  const mira::OperationContext &);
    struct RegionSource {
        desktop::ImageFrame frame;
        desktop::WindowGeometry area;
        desktop::DisplayInfo display;
        desktop::WindowQueryOutcome front;
        std::string window, scope;
        std::int32_t image_width = 0, image_height = 0;
    };
    void register_shared_input(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &);
    mira::Result<mira::JsonValue> shared_input(const std::string &, const mira::JsonValue &,
                                               const mira::OperationContext &);
    void register_native_ocr(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &);
    mira::Result<mira::JsonValue> native_ocr(const mira::JsonValue &,
                                             const mira::OperationContext &);
    void clear();
    std::chrono::milliseconds planning_image_age() const {
        // A supplied native target intent already supports local revalidation
        // for 60 seconds (DEC-081). It is not fresh completion evidence.
        return native_input_frame_ ? std::chrono::seconds{60} : image_max_age_;
    }
    std::shared_ptr<DesktopImageSource> source_;
    DesktopImageSource::Store store_;
    DesktopToolContext desktop_;
    mira::OperationContext owner_;
    mirage::desktop::CancelToken cancel_;
    std::optional<mira::ArtifactDescriptor> image_;
    std::optional<EnvironmentVisualRefresh> marks_;
    std::optional<desktop::WindowInfo> mark_window_;
    std::optional<RegionSource> region_source_;
    mira::JsonValue metadata_;
    std::uint64_t epoch_ = 0;
    std::chrono::steady_clock::time_point captured_;
    std::optional<desktop::ImageFrame> native_frame_;
    std::optional<desktop::ImageFrame> native_input_frame_;
    bool native_input_enabled_ = false;
    bool native_input_fenced_ = false;
    bool text_only_capture_ = false;
    std::string continuous_source_key_;
    std::uint64_t continuous_authority_ = 0;
    std::uint64_t native_newer_than_sequence_ = 0;
    bool supply_images_ = true;
    std::chrono::milliseconds acquire_budget_ = std::chrono::seconds{5};
    std::chrono::milliseconds image_max_age_ = std::chrono::seconds{5};
    bool native_started_ = false;
    std::string native_capture_key_;
    mira::JsonValue native_capture_args_;
    std::unique_ptr<desktop::DesktopCaptureSession> native_session_;
    std::uint64_t native_authority_ = 0;
    bool native_failed_ = false;
    std::string ocr_failed_digest_;
    std::uint64_t ocr_failed_epoch_ = 0;
    std::optional<mira::Error> ocr_failed_result_;
    std::string ocr_success_digest_;
    std::uint64_t ocr_success_epoch_ = 0, ocr_success_authority_ = 0;
    std::optional<ImageOcrOutcome> ocr_success_;
    bool expect_image_ = false;
    bool supplied_image_ = false;
    bool region_tool_enabled_ = false;
    std::optional<mira::Error> supply_error_;
    BudgetRemaining budget_remaining_;
    conversation::Timings *timings_ = nullptr;
};
} // namespace mirage::integration::detail
