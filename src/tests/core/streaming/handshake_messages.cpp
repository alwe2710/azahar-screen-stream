// core/streaming/handshake_messages.{h,cpp} had zero test coverage before
// this file, despite being exactly where hello_ack.video_mode gets parsed
// and session_ready.video_mode gets reported -- the two fields the
// "Video-mode fallback" negotiation feature (unison/docs/protocol.md)
// actually runs on.
//
// externals/unison (a git submodule tracking Unison's main branch) still
// predates session_ready.video_mode entirely as of this file's own writing
// (Unison's main is itself well behind its transcoding branch, unmerged)
// -- unison_session_ready here has no video_mode field to read at all, so
// that one assertion below is a plain JSON substring check instead of a
// round-trip through unison_parse_session_ready(), which is used for
// everything else this stream type's session_ready reply carries (a field
// that already existed before video_mode was appended, so its layout is
// unaffected either way). Switch it to a real round-trip once
// externals/unison is updated past that point (same situation as
// UnisonWebSocket.h's UNISON_WS_SEND_TIMEOUT_MS comment).

#include <catch2/catch_test_macros.hpp>
#include "core/streaming/handshake_messages.h"
#include "core/streaming/stream_constants.h"

#include "unison/handshake.h"

using namespace Core::Streaming;

namespace {
std::vector<u8> ToBytes(const std::string& s) {
    return std::vector<u8>(s.begin(), s.end());
}
} // namespace

TEST_CASE("Streaming::BuildHelloMessage", "[core][streaming]") {
    const std::string hello = BuildHelloMessage();
    REQUIRE(hello.find("\"message\":\"hello\"") != std::string::npos);
    REQUIRE(hello.find("\"stream_type\":\"N3DS_BOTTOM_SCREEN\"") != std::string::npos);
    // N3DS_BOTTOM_SCREEN carries no audio (see BuildHelloMessage's own
    // comment) -- must never claim otherwise.
    REQUIRE(hello.find("\"audio\"") == std::string::npos);

    // unison_hello's own shape is unaffected by the video_mode feature (it
    // never had that field to begin with, unlike session_ready), so this
    // round-trip is safe even against the stale externals/unison build.
    unison_hello parsed;
    REQUIRE(unison_parse_hello(reinterpret_cast<const uint8_t*>(hello.data()), hello.size(),
                               &parsed) == UNISON_HANDSHAKE_OK);
    REQUIRE(parsed.protocol_version == STREAM_PROTOCOL_VERSION);
    REQUIRE(std::string(parsed.stream_type) == STREAM_TYPE);
    REQUIRE(!parsed.has_audio);
}

TEST_CASE("Streaming::ParseHelloAck video_mode", "[core][streaming]") {
    // No video_mode field at all -- must stay empty, not garbage/
    // uninitialized (HandshakeAck::video_mode is only ever meant to carry
    // "what was actually requested, if anything" through to the honest
    // fallback report -- see this header's own comment).
    const auto no_mode = ParseHelloAck(
        ToBytes(R"({"message":"hello_ack","protocol_version":2,"requested_slot":0})"));
    REQUIRE(no_mode.has_value());
    REQUIRE(no_mode->protocol_version == 2);
    REQUIRE(no_mode->requested_slot == 0);
    REQUIRE(no_mode->video_mode.empty());

    // This stream type has no TILES/H264/H265 encoder at all (see
    // BuildSessionReadyMessage()'s own comment) -- ParseHelloAck() never
    // acts on this value, only carries it through for reporting, so
    // (unlike Cemu's stricter whitelist-and-default-to-tiles) it accepts
    // and stores whatever string was actually sent, verbatim. This part is
    // azahar's own HandshakeAck struct, unrelated to unison_core's
    // (unpatched) one, so no staleness concern here.
    for (const std::string mode : {"tiles", "legacy", "h264", "h265", "vp9"}) {
        const std::string json =
            R"({"message":"hello_ack","protocol_version":2,"requested_slot":0,"video_mode":")" +
            mode + "\"}";
        const auto ack = ParseHelloAck(ToBytes(json));
        REQUIRE(ack.has_value());
        REQUIRE(ack->video_mode == mode);
    }
}

TEST_CASE("Streaming::ParseHelloAck rejects malformed input", "[core][streaming]") {
    REQUIRE(!ParseHelloAck({}).has_value());
    REQUIRE(!ParseHelloAck(ToBytes(R"({"message":"session_ready"})")).has_value());
    REQUIRE(!ParseHelloAck(ToBytes(R"({"message":"hello_ack","requested_slot":0})")).has_value());
    REQUIRE(!ParseHelloAck(ToBytes(R"({"message":"hello_ack","protocol_version":2})")).has_value());
}

TEST_CASE("Streaming::BuildSessionReadyMessage echoes the videoMode argument", "[core][streaming]") {
    // This stream type now has a real h264/h265 SoftwareVideoEncoder (see
    // bottom_screen_stream.cpp's SendVideoFrame) -- BuildSessionReadyMessage()
    // just reports back whatever ServeConnection() decided to attempt, it
    // doesn't itself decide "legacy" vs. anything else.
    for (const std::string& mode : {std::string("legacy"), std::string("h264"), std::string("h265")}) {
        const std::string ready_json = BuildSessionReadyMessage(mode);
        REQUIRE(ready_json.find("\"message\":\"session_ready\"") != std::string::npos);
        // Plain substring check, not a round-trip -- see this file's own top
        // comment on why unison_session_ready.video_mode isn't safe to read
        // via the currently-vendored unison_core here.
        REQUIRE(ready_json.find("\"video_mode\":\"" + mode + "\"") != std::string::npos);

        // width/height/audio/redirect all predate video_mode's addition to
        // this struct, at unchanged offsets -- safe to round-trip.
        unison_session_ready parsed;
        REQUIRE(unison_parse_session_ready(reinterpret_cast<const uint8_t*>(ready_json.data()),
                                           ready_json.size(), &parsed) == UNISON_HANDSHAKE_OK);
        REQUIRE(parsed.video.width == STREAM_WIDTH);
        REQUIRE(parsed.video.height == STREAM_HEIGHT);
        // No audio, no redirect for this stream type -- see
        // BuildSessionReadyMessage()'s own comment.
        REQUIRE(!parsed.has_audio);
        REQUIRE(!parsed.has_redirect);
    }
}

TEST_CASE("Streaming::BuildHandshakeErrorMessage", "[core][streaming]") {
    const std::string err_json = BuildHandshakeErrorMessage(
        HandshakeErrorCode::SlotUnavailable, "Bottom screen stream already has an active client");

    unison_handshake_error parsed;
    REQUIRE(unison_parse_handshake_error(reinterpret_cast<const uint8_t*>(err_json.data()),
                                         err_json.size(), &parsed) == UNISON_HANDSHAKE_OK);
    REQUIRE(std::string(parsed.code) == "slot_unavailable");
    REQUIRE(std::string(parsed.detail) == "Bottom screen stream already has an active client");
}
