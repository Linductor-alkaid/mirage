#pragma once
#include <cstdint>
#include <string>

namespace mirage::desktop {
// Product UI child ownership, not an Agent process.execute capability.
// Runtime's serialized context calls open()/pid(); its owning teardown
// calls stop() after closing IPC. Platform implementations own OS handles.
class FrontendProcess {
  public:
    virtual ~FrontendProcess() = default;
    virtual bool open(const std::string &endpoint, std::string &diagnostic) = 0;
    virtual std::int64_t pid() = 0;                 // reaps an exited child; zero means absent
    virtual bool stop(std::string &diagnostic) = 0; // bounded, idempotent
};
} // namespace mirage::desktop
