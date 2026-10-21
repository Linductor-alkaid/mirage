#pragma once

#include "loader.hpp"
#include <memory>
#include <mirage/runtime/persistence/app_preferences.hpp>
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
    bool preferences(runtime::persistence::AppPreferences &out);
    void set_bubble_expiry(std::uint64_t remaining_ms);
    ResourceLoader &loader();
    void set_animating(bool value,
                       std::chrono::milliseconds interval = std::chrono::milliseconds{33});
    void set_visible(bool value);
    bool visible() const;
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::pet_ui
