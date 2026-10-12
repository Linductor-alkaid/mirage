#include "../support/test.hpp"
#include <kairo/executor.hpp>
#include <mira/model_profile.hpp>
#include <mirage/platform/credential_store.hpp>
#include <string>

int main(int argc, char **argv) {
    using namespace mirage::platform;
    const std::string ref(32, 'a');
    MIRAGE_CHECK(!read_credential("bad").ok);
    MIRAGE_CHECK(!write_credential("../bad", "synthetic-key").ok);
    MIRAGE_CHECK(!write_credential(ref, std::string(2049, 'x')).ok);
    MIRAGE_CHECK(!write_credential(ref, "synthetic key").ok);
    MIRAGE_CHECK(!write_credential(ref, "synthetic\nkey").ok);
    if (argc == 2 && std::string(argv[1]).starts_with("--fixture-")) {
        kairo::Executor owner;
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 1;
        MIRAGE_CHECK(owner.initialize(config));
        auto result = owner.submit_auto([&] {
            const std::string mode(argv[1]);
            if (mode == "--fixture-write")
                MIRAGE_CHECK(write_credential(ref, "mirage-synthetic-credential-probe").ok);
            else if (mode == "--fixture-write-refused")
                MIRAGE_CHECK(!write_credential(std::string(32, 'e'), "synthetic-key").ok);
            else if (mode == "--fixture-read") {
                const auto read = read_credential(ref);
                MIRAGE_CHECK(read.ok && read.value == "mirage-synthetic-credential-probe");
            } else if (mode == "--fixture-delete") {
                MIRAGE_CHECK(write_credential(ref, "").ok);
                MIRAGE_CHECK(write_credential(ref, "").ok);
                const auto missing = read_credential(ref);
                MIRAGE_CHECK(!missing.ok && missing.error.find("没有") != std::string::npos);
            } else if (mode == "--fixture-guards") {
                const auto locked = read_credential(std::string(32, 'b'));
                MIRAGE_CHECK(!locked.ok && locked.error.find("已锁定") != std::string::npos);
                MIRAGE_CHECK(!write_credential(std::string(32, 'b'), "").ok);
                const auto duplicate = read_credential(std::string(32, 'c'));
                MIRAGE_CHECK(!duplicate.ok && duplicate.error.find("重复") != std::string::npos);
                MIRAGE_CHECK(!write_credential(std::string(32, 'c'), "").ok);
                MIRAGE_CHECK(!read_credential(std::string(32, 'd')).ok);
                MIRAGE_CHECK(write_credential(std::string(32, 'd'), "").ok);
            } else if (mode == "--fixture-unavailable") {
                MIRAGE_CHECK(!read_credential(ref).ok);
                MIRAGE_CHECK(!write_credential(ref, "").ok);
            } else
                MIRAGE_CHECK(false);
        });
        result.get();
        owner.shutdown(true);
    }
    if (argc == 2 && std::string(argv[1]) == "--probe-keyring") {
        kairo::Executor owner;
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 1;
        MIRAGE_CHECK(owner.initialize(config));
        auto result = owner.submit_auto([] {
            const auto identity = mira::ModelProfileId::generate().to_string();
            const auto write = write_credential(identity, "mirage-synthetic-credential-probe");
            MIRAGE_CHECK(write.ok);
            if (write.ok) {
                const auto read = read_credential(identity);
                MIRAGE_CHECK(read.ok && read.value == "mirage-synthetic-credential-probe");
                MIRAGE_CHECK(write_credential(identity, "").ok);
                MIRAGE_CHECK(!read_credential(identity).ok);
                MIRAGE_CHECK(write_credential(identity, "").ok);
            }
        });
        result.get();
        owner.shutdown(true);
    }
    return mirage::testing::finish("credential_store_test");
}
