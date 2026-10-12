#pragma once

#include <memory>
#include <mirage/runtime/pet/status.hpp>
#include <string>

namespace mirage::pet_ui {
// One Executor owner for the pet process. Transport events and full snapshots
// meet in a latest-value mailbox; UI never performs socket I/O.
class Bridge {
  public:
    explicit Bridge(std::string endpoint);
    ~Bridge();
    Bridge(const Bridge &) = delete;
    Bridge &operator=(const Bridge &) = delete;
    bool start(std::string &diagnostic);
    bool latest(runtime::pet::Status &out);
    bool connected() const;
    void set_animating(bool value);
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::pet_ui
