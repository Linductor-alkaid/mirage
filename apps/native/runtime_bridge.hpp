#pragma once
#include <functional>
#include <memory>
#include <mirage/runtime/ipc/protocol.hpp>
#include <optional>
namespace mirage::native_ui {
struct RuntimeMessage {
    enum class Kind { Connected, Lost, Response, Event } kind = Kind::Lost;
    std::string tag;
    std::uint64_t local_id = 0;
    mirage::runtime::ipc::Response response;
    std::optional<mirage::runtime::ipc::Event> event;
};
// Native frontend process owner; implementation hides Executor/transport types.
class RuntimeBridge {
  public:
    explicit RuntimeBridge(std::function<void()> wake_ui, std::string endpoint = {});
    ~RuntimeBridge();
    bool call(mirage::runtime::ipc::Request request, std::string tag, std::uint64_t local_id = 0);
    bool receive(RuntimeMessage &out);
    bool connected() const;
    bool take_gap();
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::native_ui
