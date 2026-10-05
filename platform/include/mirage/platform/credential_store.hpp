#pragma once

#include <string>

namespace mirage::platform {
struct CredentialResult {
    bool ok = false;
    std::string value; // Only read returns plaintext; never a diagnostic.
    std::string error;
};
// Finite platform calls, made from an Executor-owned context. Empty value
// removes the item; references are exactly 32 lowercase hexadecimal bytes.
CredentialResult read_credential(const std::string &reference);
CredentialResult write_credential(const std::string &reference, const std::string &value);
} // namespace mirage::platform
