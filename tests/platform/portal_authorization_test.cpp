#include "../../platform/linux/src/portal_authorization.hpp"
#include "../support/test.hpp"
#include <stdexcept>

int main() {
    using namespace mirage;
    using platform::linux_backend::PortalAuthorization;
    std::string token;
    bool read_error = false, write_error = false, throw_error = false;
    unsigned writes = 0;
    auto state = std::make_shared<PortalAuthorization>(PortalAuthorization::Store{
        [&] {
            if (throw_error)
                throw std::runtime_error("fixture");
            return read_error
                       ? platform::CredentialResult{false, {}, "fixture failure"}
                       : platform::CredentialResult{!token.empty(), token, {}, token.empty()};
        },
        [&](const std::string &value) {
            ++writes;
            if (write_error)
                return platform::CredentialResult{};
            token = value;
            return platform::CredentialResult{true, {}, {}};
        }});
    MIRAGE_CHECK(state->view().state == "ask" && writes == 0);
    const auto generation = state->generation();
    state->configure(true);
    MIRAGE_CHECK(state->generation() != generation && state->view().state == "needs_authorization");
    MIRAGE_CHECK(state->begin() && !state->begin() && state->view().state == "authorizing");
    std::string restored;
    MIRAGE_CHECK(state->consume(restored).code.empty() && restored.empty() && writes == 0);
    MIRAGE_CHECK(state->save("fixture-token-1").code.empty());
    state->finish();
    MIRAGE_CHECK(state->view().remembered);
    MIRAGE_CHECK(state->begin());
    MIRAGE_CHECK(state->consume(restored).code.empty() && restored == "fixture-token-1" &&
                 token.empty());
    MIRAGE_CHECK(state->save("fixture-token-2").code.empty());
    const auto active_generation = state->generation();
    MIRAGE_CHECK(state->forget().code == "desktop_authorization_busy" && !state->enabled());
    MIRAGE_CHECK(state->generation() != active_generation);
    MIRAGE_CHECK(state->save("late-token").code == "desktop_authorization_changed" &&
                 token == "fixture-token-2");
    state->finish();
    MIRAGE_CHECK(state->forget().code.empty() && token.empty() && state->view().state == "ask");
    state->configure(true);
    MIRAGE_CHECK(state->save("").code == "persistence_not_granted");
    MIRAGE_CHECK(!PortalAuthorization::valid_token(std::string(2049, 'a')));
    MIRAGE_CHECK(!PortalAuthorization::valid_token("bad\nvalue"));
    read_error = true;
    MIRAGE_CHECK(state->view().state == "store_unavailable");
    MIRAGE_CHECK(state->consume(restored).code == "authorization_store_failed");
    read_error = false;
    token = "original";
    write_error = true;
    restored.clear();
    MIRAGE_CHECK(state->consume(restored).code == "authorization_store_failed" &&
                 restored.empty() && token == "original");
    MIRAGE_CHECK(state->save("replacement").code == "authorization_store_failed" &&
                 token == "original");
    MIRAGE_CHECK(state->forget().code == "authorization_store_failed" && !state->enabled());
    write_error = false;
    MIRAGE_CHECK(state->forget().code.empty());
    state->configure(true);
    throw_error = true;
    try {
        (void)state->view();
        MIRAGE_CHECK(false);
    } catch (const std::runtime_error &) {
    }
    throw_error = false;
    MIRAGE_CHECK(state->begin()); // Exception did not leak the rotation admission.
    state->finish();
    return testing::finish("portal_authorization");
}
