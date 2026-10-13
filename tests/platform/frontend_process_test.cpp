#include "../support/ipc_io.hpp"
#include <kairo/executor.hpp>
#include <mirage/platform/frontend_process.hpp>

#include <csignal>
#include <cstdlib>
#include <fstream>

using namespace std::chrono_literals;
namespace {
volatile std::sig_atomic_t term_count = 0, exit_requested = 0;
void on_term(int) { term_count = 1; }
void on_exit(int) { exit_requested = 1; }
} // namespace
int main() {
    const char *endpoint = std::getenv("MIRAGE_PET_SOCKET");
    if (endpoint && std::string_view(endpoint).ends_with("/fixture-child")) {
        std::signal(SIGUSR1, on_exit);
        std::ofstream(std::string(endpoint) + ".ready") << "ready";
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!exit_requested && std::chrono::steady_clock::now() < deadline)
            (void)::poll(nullptr, 0, 5);
        return exit_requested ? 0 : 1;
    }
    // Confine a regression to this external test process group. Count SIGTERM
    // rather than letting the historical kill(0) terminate the test driver.
    MIRAGE_CHECK(::setpgid(0, 0) == 0 || ::getpgrp() == ::getpid());
    std::signal(SIGTERM, on_term);
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    config.queue_capacity = config.max_in_flight_tasks = 4;
    MIRAGE_CHECK(executor.initialize(config));
    auto result = executor.submit_auto([] {
        mirage::testing::TempDir directory;
        std::string diagnostic;
        for (bool pet : {false, true}) {
            auto process = pet ? mirage::platform::make_pet_process("/missing/child")
                               : mirage::platform::make_frontend_process("/missing/child");
            MIRAGE_CHECK(process->stop(diagnostic));
            MIRAGE_CHECK(!process->open("unused", diagnostic));
            MIRAGE_CHECK(process->stop(diagnostic));
            MIRAGE_CHECK(term_count == 0);
        }
        auto completed = mirage::platform::make_pet_process("/bin/true");
        MIRAGE_CHECK(completed->open("unused", diagnostic));
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (completed->pid() && std::chrono::steady_clock::now() < deadline)
            (void)::poll(nullptr, 0, 2);
        MIRAGE_CHECK(completed->pid() == 0);
        MIRAGE_CHECK(completed->stop(diagnostic) && term_count == 0);

        auto process =
            mirage::platform::make_pet_process(mirage::platform::current_executable_path());
        const auto endpoint_path = (directory.root() / "fixture-child").string();
        MIRAGE_CHECK(process->open(endpoint_path, diagnostic));
        const auto first = process->pid();
        MIRAGE_CHECK(first > 0 && process->open(endpoint_path, diagnostic) &&
                     process->pid() == first);
        const auto ready = std::chrono::steady_clock::now() + 2s;
        while (!std::filesystem::exists(endpoint_path + ".ready") &&
               std::chrono::steady_clock::now() < ready)
            (void)::poll(nullptr, 0, 2);
        MIRAGE_CHECK(std::filesystem::exists(endpoint_path + ".ready"));
        MIRAGE_CHECK(process->stop(diagnostic));
        MIRAGE_CHECK(process->pid() == 0 && process->stop(diagnostic) && term_count == 0);
    });
    result.get();
    executor.shutdown(true);
    return mirage::testing::finish("frontend_process_test");
}
