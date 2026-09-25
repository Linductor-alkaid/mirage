#include "bridge_core.hpp"

#include <utility>

namespace mirage::desktop_shell {
namespace {

/// Renders a wire-shaped error envelope with the correlation id `id`; used
/// for local bridge failures so the renderer transport sees protocol-v1
/// shapes instead of bridge-internal ones.
std::string error_envelope(std::uint64_t id, const std::string &code, const std::string &message) {
    ipc::Response response;
    response.ok = false;
    response.id = id;
    response.error = {code, message};
    return ipc::encode_response(response);
}

} // namespace

void BridgeCore::handle_request(const std::string &request_json,
                                const std::function<void(const std::string &)> &respond) {
    const ipc::RequestDecode decoded = ipc::decode_request(request_json);
    if (!decoded.ok) {
        // Undecodable renderer output is a bridge-local protocol violation:
        // answered with the stable wire shape (id 0 — the envelope carries no
        // decodable correlation id), the bridge stays up (the renderer is
        // in-process, not a peer to police).
        respond(error_envelope(0, "protocol_error", decoded.error));
        return;
    }
    ipc::Response response = call_(decoded.body, kCallTimeout).get();
    response.id = decoded.id; // the renderer's own echo, not the session's
    respond(ipc::encode_response(response));
}

} // namespace mirage::desktop_shell
