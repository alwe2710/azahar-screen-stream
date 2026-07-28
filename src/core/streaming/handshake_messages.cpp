// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/streaming/handshake_messages.h"

#include <json.hpp>

#include "core/streaming/stream_constants.h"

namespace Core::Streaming {

namespace {
const char* ErrorCodeToString(HandshakeErrorCode code) {
    switch (code) {
    case HandshakeErrorCode::VersionMismatch:
        return "version_mismatch";
    case HandshakeErrorCode::SlotUnavailable:
        return "slot_unavailable";
    case HandshakeErrorCode::MalformedRequest:
        return "malformed_request";
    }
    return "malformed_request";
}
} // namespace

std::string BuildHelloMessage() {
    nlohmann::json obj;
    obj["message"] = "hello";
    obj["protocol_version"] = STREAM_PROTOCOL_VERSION;
    obj["stream_type"] = STREAM_TYPE;
    // Single, always-index-0 slot -- see this header's own comment on why
    // this stream type skips the multi-slot redirect dance GC_GBA_LINK uses.
    obj["slots"] = nlohmann::json::array(
        {nlohmann::json{{"index", 0}, {"label", "Bottom"}, {"occupied", false}}});
    obj["video"] = {{"width", STREAM_WIDTH},
                     {"height", STREAM_HEIGHT},
                     {"pixel_format", "rgb565"},
                     {"fps", STREAM_FPS}};
    // No "audio" field: N3DS_BOTTOM_SCREEN carries no audio (docs/protocol.md,
    // "Stream-Typen").
    obj["input_encoding"] = INPUT_ENCODING;
    return obj.dump();
}

std::optional<HandshakeAck> ParseHelloAck(const std::vector<u8>& payload) {
    nlohmann::json obj;
    try {
        obj = nlohmann::json::parse(payload.begin(), payload.end());
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    if (!obj.is_object())
        return std::nullopt;

    const auto message_it = obj.find("message");
    if (message_it == obj.end() || !message_it->is_string() || *message_it != "hello_ack")
        return std::nullopt;

    const auto version_it = obj.find("protocol_version");
    const auto slot_it = obj.find("requested_slot");
    if (version_it == obj.end() || !version_it->is_number_integer())
        return std::nullopt;
    if (slot_it == obj.end() || !slot_it->is_number_integer())
        return std::nullopt;

    HandshakeAck ack;
    ack.protocol_version = version_it->get<int>();
    ack.requested_slot = slot_it->get<int>();
    return ack;
}

std::string BuildSessionReadyMessage() {
    // This stream type doesn't implement real video negotiation the way
    // GC_GBA_LINK does: the bottom screen is a fixed 320x240, small enough
    // that no realistic client's video_limits would ever need to shrink it,
    // so session_ready always reports the native size rather than running
    // NegotiateVideo()'s downscale math against a HandshakeAck.video_limits
    // this stream type doesn't even bother parsing (see ParseHelloAck()).
    nlohmann::json obj;
    obj["message"] = "session_ready";
    obj["slot"] = 0;
    obj["video"] = {{"width", STREAM_WIDTH}, {"height", STREAM_HEIGHT}, {"fps", STREAM_FPS}};
    // No "audio", no "redirect" -- audio doesn't exist for this stream type,
    // and redirect is only ever used by multi-slot stream types.
    return obj.dump();
}

std::string BuildHandshakeErrorMessage(HandshakeErrorCode code, const std::string& detail) {
    nlohmann::json obj;
    obj["message"] = "handshake_error";
    obj["code"] = ErrorCodeToString(code);
    obj["detail"] = detail;
    return obj.dump();
}

} // namespace Core::Streaming
