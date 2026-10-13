#pragma once
#include <cstdint>
#include <string>

namespace mirage::desktop {
// Product UI child ownership, not an Agent process.execute capability.
// Runtime serializes all access. A retained finite Executor task may own
// stop() while serial code reads only a cached snapshot; teardown joins that
// task before using the handle. Platform implementations own OS handles.
class FrontendProcess {
  public:
    virtual ~FrontendProcess() = default;
    // Starts a child or requests restoration/activation of the existing owned window.
    // Restoration must not depend on the child consuming a paint-loop message.
    virtual bool open(const std::string &endpoint, std::string &diagnostic) = 0;
    virtual std::int64_t pid() = 0;                 // reaps an exited child; zero means absent
    virtual bool stop(std::string &diagnostic) = 0; // bounded, idempotent
};
} // namespace mirage::desktop
