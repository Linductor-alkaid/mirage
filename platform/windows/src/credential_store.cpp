#include <algorithm>
#include <mirage/platform/credential_store.hpp>
#include <windows.h>

#include <wincred.h>

namespace mirage::platform {
namespace {
std::wstring target(const std::string &ref) {
    if (ref.size() != 32 || ref.find_first_not_of("0123456789abcdef") != std::string::npos)
        return {};
    return L"Mirage/model/" + std::wstring(ref.begin(), ref.end());
}
} // namespace
CredentialResult read_credential(const std::string &ref) {
    auto name = target(ref);
    PCREDENTIALW item = nullptr;
    if (name.empty() || !CredReadW(name.c_str(), CRED_TYPE_GENERIC, 0, &item))
        return {false, {}, "无法读取系统保存的 API Key，请重新配置。"};
    CredentialResult result;
    if (item->CredentialBlobSize > 0 && item->CredentialBlobSize <= 2048)
        result = {true,
                  std::string(reinterpret_cast<const char *>(item->CredentialBlob),
                              item->CredentialBlobSize),
                  {}};
    else
        result.error = "系统保存的 API Key 无效，请重新配置。";
    CredFree(item);
    return result;
}
CredentialResult write_credential(const std::string &ref, const std::string &value) {
    auto name = target(ref);
    if (name.empty() || value.size() > 2048 ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 33 || c > 126; }))
        return {false, {}, "API Key 或凭据引用无效。"};
    if (value.empty()) {
        if (CredDeleteW(name.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND)
            return {true, {}, {}};
    } else {
        CREDENTIALW item{};
        item.Type = CRED_TYPE_GENERIC;
        item.TargetName = name.data();
        item.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.data()));
        item.CredentialBlobSize = static_cast<DWORD>(value.size());
        item.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (CredWriteW(&item, 0))
            return {true, {}, {}};
    }
    return {false, {}, "无法更新系统保存的 API Key，请重试。"};
}
} // namespace mirage::platform
