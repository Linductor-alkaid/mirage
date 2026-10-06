#include "../support/test.hpp"
#include <executor/executor.hpp>
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
    if (argc == 2 && std::string(argv[1]) == "--probe-keyring") {
        executor::Executor owner;
        executor::ExecutorConfig config;
        config.min_threads = config.max_threads = 1;
        MIRAGE_CHECK(owner.initialize_ex(config));
        auto result = owner.submit_auto([] {
            const auto identity = mira::ModelProfileId::generate().to_string();
            const auto write = write_credential(identity, "mirage-synthetic-credential-probe");
            MIRAGE_CHECK(write.ok);
            if (write.ok) {
                const auto read = read_credential(identity);
                MIRAGE_CHECK(read.ok && read.value == "mirage-synthetic-credential-probe");
                MIRAGE_CHECK(write_credential(identity, "").ok);
                MIRAGE_CHECK(!read_credential(identity).ok);
            }
        });
        result.get();
        owner.shutdown(true);
    }
    return mirage::testing::finish("credential_store_test");
}
