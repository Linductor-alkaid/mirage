#pragma once

// Shared apps-side shape of the blocking worker that parks on one
// SessionClient episode (REUSE-15): wakeup() stops the client cooperatively
// and drive() consumes the run() exit. Each process keeps its own unique
// Executor owner (DEC-030 EXEC-01 / DEC-096); this class only factors the
// worker skeleton the native and pet frontends share.

#include <kairo/blocking_io.hpp>

#include <mirage/runtime/ipc/session_client.hpp>

#include <memory>
#include <string>

namespace mirage::apps {

class SessionRunWorker : public kairo::IBlockingIoWorker {
  public:
    explicit SessionRunWorker(std::shared_ptr<mirage::runtime::ipc::SessionClient> client)
        : client_(std::move(client)) {}

    void wakeup() noexcept override { client_->stop(); }

  protected:
    /// Drives the episode to its end; the diagnostic is the run() exit text.
    mirage::runtime::ipc::SessionClient::RunExit drive(std::string &diagnostic) {
        return client_->run(diagnostic);
    }

  private:
    std::shared_ptr<mirage::runtime::ipc::SessionClient> client_;
};

} // namespace mirage::apps
