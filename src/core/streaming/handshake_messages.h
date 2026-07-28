// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// App-level handshake (hello / hello_ack / session_ready / handshake_error)
// for the N3DS_BOTTOM_SCREEN stream type, exchanged as WebSocket text frames
// before any Video/Input binary frame, per finlink's docs/protocol.md.
// Ported from the sibling dolphin-gba-stream project's
// Source/Core/Core/HW/GBAStreamHandshake.h/.cpp (same wire format, same
// message shapes), simplified for this stream type's fixed single slot and
// lack of audio -- there is no redirect step and no audio negotiation here
// (see docs/protocol.md's "Zielbildschirm auf Zweitbildschirm-Clients" and
// "Stream-Typen" sections). nlohmann::json instead of picojson, matching
// what's already linked into this codebase (json-headers).
//
// Pure message (de)serialization -- no socket I/O, mirroring
// websocket_transport.h's own separation of transport from message content.

#include <optional>
#include <string>
#include <vector>

#include "common/common_types.h"

namespace Core::Streaming {

// What the client sends back in `hello_ack`. video_limits is parsed for
// spec-completeness but not actually enforced -- see
// BuildSessionReadyMessage()'s own comment on why this stream always reports
// its fixed native size instead of implementing real negotiation.
struct HandshakeAck {
    int protocol_version;
    int requested_slot;
};

enum class HandshakeErrorCode {
    VersionMismatch,
    SlotUnavailable,
    MalformedRequest,
};

// Serializes the `hello` message body (the JSON text frame payload -- caller
// sends it via SendWebSocketTextFrame, this doesn't touch the socket).
std::string BuildHelloMessage();

// Parses a `hello_ack` text frame payload. Returns nullopt if the JSON is
// malformed or missing required fields -- caller should treat that as
// HandshakeErrorCode::MalformedRequest.
std::optional<HandshakeAck> ParseHelloAck(const std::vector<u8>& payload);

std::string BuildSessionReadyMessage();

std::string BuildHandshakeErrorMessage(HandshakeErrorCode code, const std::string& detail);

} // namespace Core::Streaming
