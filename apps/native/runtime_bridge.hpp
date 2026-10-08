#pragma once
#include "attachment.hpp"
#include <functional>
#include <memory>
#include <mirage/runtime/ipc/protocol.hpp>
#include <optional>
namespace mirage::native_ui {
struct RuntimeMessage {
    enum class Kind { Connected, Lost, Response, Event, Attachment, Diagnostic } kind = Kind::Lost;
    std::string tag;
    std::uint64_t local_id = 0;
    mirage::runtime::ipc::Response response;
    std::optional<mirage::runtime::ipc::Event> event;
    AttachmentResult attachment = {};
    std::uint64_t attachment_generation = 0;
};
// Native frontend process owner; implementation hides Executor/transport types.
class RuntimeBridge {
  public:
    explicit RuntimeBridge(std::function<void()> wake_ui, std::string endpoint = {});
    ~RuntimeBridge();
    bool call(mirage::runtime::ipc::Request request, std::string tag, std::uint64_t local_id = 0);
    bool receive(RuntimeMessage &out);
    bool load_attachment(const std::string &path, std::uint64_t session_id,
                         std::uint64_t generation = 0);
    bool connected() const;
    bool take_gap();
    void set_activity(bool active, bool animating = false);
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::native_ui
