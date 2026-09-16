#include <mirage/runtime/ipc/framing.hpp>

#include <cstdint>

namespace mirage::runtime::ipc {

std::string make_frame(std::string_view payload) {
    std::string frame;
    frame.reserve(kFrameHeaderBytes + payload.size());
    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    frame.push_back(static_cast<char>(length & 0xFFu));
    frame.push_back(static_cast<char>((length >> 8) & 0xFFu));
    frame.push_back(static_cast<char>((length >> 16) & 0xFFu));
    frame.push_back(static_cast<char>((length >> 24) & 0xFFu));
    frame.append(payload.data(), payload.size());
    return frame;
}

FrameExtraction try_extract_frame(std::string& buffer) {
    if (buffer.size() < kFrameHeaderBytes) {
        return {FrameExtract::NeedMoreData, {}, {}};
    }
    const auto byte_at = [&buffer](std::size_t index) {
        return static_cast<std::uint8_t>(buffer[index]);
    };
    const std::uint32_t length = static_cast<std::uint32_t>(byte_at(0)) |
                                 (static_cast<std::uint32_t>(byte_at(1)) << 8) |
                                 (static_cast<std::uint32_t>(byte_at(2)) << 16) |
                                 (static_cast<std::uint32_t>(byte_at(3)) << 24);
    if (length > kMaxFrameBytes) {
        return {FrameExtract::ProtocolError, {},
                "declared frame length " + std::to_string(length) +
                    " exceeds the " + std::to_string(kMaxFrameBytes) +
                    " byte cap"};
    }
    if (buffer.size() < kFrameHeaderBytes + static_cast<std::size_t>(length)) {
        return {FrameExtract::NeedMoreData, {}, {}};
    }
    FrameExtraction extraction;
    extraction.status = FrameExtract::Message;
    extraction.message.assign(buffer, kFrameHeaderBytes,
                              static_cast<std::size_t>(length));
    buffer.erase(0, kFrameHeaderBytes + static_cast<std::size_t>(length));
    return extraction;
}

} // namespace mirage::runtime::ipc
