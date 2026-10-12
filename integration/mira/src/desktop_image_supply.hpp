#pragma once
#include <kairo/comm/mailbox.hpp>
#include <mira/conversation_loop.hpp>
#include <mira/model_dialect.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>

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

  private:
    kairo::comm::LatestMailbox<Store> current_;
};

// Scoped to one serialized harness task; no image state survives the turn.
class DesktopImageTurn {
  public:
    DesktopImageTurn(std::shared_ptr<DesktopImageSource>, DesktopToolContext,
                     mira::OperationContext, mirage::desktop::CancelToken,
                     bool supply_images = true);
    ~DesktopImageTurn();
    DesktopImageTurn(const DesktopImageTurn &) = delete;
    DesktopImageTurn &operator=(const DesktopImageTurn &) = delete;
    void register_tool(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &,
                       bool allow_mutations = true);
    mira::Result<std::vector<mira::ModelInputItem>> supply(const mira::AgentLoopSpec &);
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
    void register_mark_tools(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &,
                             bool);
    void register_region_tool(mira::BuiltinToolRegistry &, const DesktopAtomToolset::Decorator &,
                              bool);
    mira::Result<mira::JsonValue> click_region(const mira::JsonValue &,
                                               const mira::OperationContext &);
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
    std::uint64_t native_newer_than_sequence_ = 0;
    bool supply_images_ = true;
    std::size_t captures_ = 0;
    std::unique_ptr<desktop::DesktopCaptureSession> native_session_;
    std::uint64_t native_authority_ = 0;
    bool native_failed_ = false;
    bool expect_image_ = false;
    bool supplied_image_ = false;
    bool region_tool_enabled_ = false;
    std::optional<mira::Error> supply_error_;
};
} // namespace mirage::integration::detail
