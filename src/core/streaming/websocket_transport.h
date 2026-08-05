// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// RFC6455 WebSocket transport for the bottom-screen streaming server
// (bottom_screen_stream.h): reading/parsing the plain-HTTP upgrade request,
// computing Sec-WebSocket-Accept, sending unmasked server->client frames, and
// receiving masked client->server frames. Deliberately only the transport --
// neither the app-level handshake (handshake_messages.h) nor the Video/Input
// binary message formats (bottom_screen_stream.cpp) live here.
//
// Ported from the sibling dolphin-gba-stream project's
// Source/Core/Core/HW/GBAStreamWebSocket.h, same wire format (both
// implement Unison's docs/protocol.md), different transport primitives:
// boost::asio::ip::tcp::socket instead of SFML, CryptoPP instead of mbedtls
// for SHA1/base64. Incoming frame parsing reuses unison_ws_parse_frame()
// (core/include/unison/websocket.h) directly rather than hand-rolling it a
// third time -- its unmasking logic is generic despite being documented from
// a client's perspective (see that header's own comment).

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <cryptopp/base64.h>
#include <cryptopp/sha.h>

#include "common/common_types.h"
#include "unison/websocket.h"

namespace Core::Streaming {

struct HttpRequest {
    std::string path;
    std::map<std::string, std::string> headers; // keys lowercased
};

// Reads and minimally parses one HTTP request (request line + headers) from
// `socket`, which must already be in non-blocking mode. Returns nullopt on
// timeout-free-but-endless-wait guard (16 KiB cap), a malformed request, or
// if `stop_flag` is set while waiting.
inline std::optional<HttpRequest> ReadHttpRequest(boost::asio::ip::tcp::socket& socket,
                                                   const std::atomic_bool& stop_flag) {
    std::string request;
    std::array<char, 4096> buf{};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
        if (stop_flag)
            return std::nullopt;
        boost::system::error_code ec;
        const size_t received = socket.read_some(boost::asio::buffer(buf), ec);
        if (ec == boost::asio::error::would_block) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (ec || received == 0)
            return std::nullopt;
        request.append(buf.data(), received);
    }
    if (request.find("\r\n\r\n") == std::string::npos)
        return std::nullopt;

    HttpRequest result;
    std::istringstream stream(request);
    std::string request_line;
    std::getline(stream, request_line);
    {
        const auto first_space = request_line.find(' ');
        const auto second_space = first_space == std::string::npos
                                       ? std::string::npos
                                       : request_line.find(' ', first_space + 1);
        if (first_space != std::string::npos && second_space != std::string::npos)
            result.path = request_line.substr(first_space + 1, second_space - first_space - 1);
    }
    std::string line;
    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ')
            value.erase(value.begin());
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
            value.pop_back();
        std::transform(key.begin(), key.end(), key.begin(),
                        [](unsigned char c) { return std::tolower(c); });
        result.headers[key] = value;
    }
    return result;
}

inline bool IsWebSocketUpgradeRequest(const HttpRequest& request) {
    auto it = request.headers.find("upgrade");
    if (it == request.headers.end() || !request.headers.count("sec-websocket-key"))
        return false;
    std::string upgrade = it->second;
    std::transform(upgrade.begin(), upgrade.end(), upgrade.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return upgrade == "websocket";
}

// Sends `size` bytes on a non-blocking socket, retrying on would_block.
// Bounded so a stalled peer can't block this thread forever; `stop_flag` is
// checked on every retry. 3 seconds turned out too tight in practice: a
// routine Wi-Fi hiccup (brief congestion, a roam between APs) can leave a
// send still buffered past that point even though the peer is still very
// much alive, and hitting the deadline kills the whole session (the client
// has to fully reconnect) rather than just this one frame arriving late --
// 10 seconds absorbs that without meaningfully changing behavior for an
// actually-dead peer, which was never going to un-stall in the next 7
// seconds either. unison_core now has this as a named constant,
// UNISON_WS_SEND_TIMEOUT_MS (core/include/unison/websocket.h) -- not
// referenced directly here yet because externals/unison tracks Unison's
// main branch (per .gitmodules), which doesn't have that commit yet (it
// landed on Unison's transcoding branch, still unmerged as of this fix).
// Switch this literal to the constant once externals/unison is updated
// past that point.
inline bool SendAllBytes(boost::asio::ip::tcp::socket& socket, const void* data, size_t size,
                          const std::atomic_bool& stop_flag) {
    const auto* bytes = static_cast<const u8*>(data);
    size_t sent_total = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (sent_total < size) {
        if (stop_flag || std::chrono::steady_clock::now() > deadline)
            return false;
        boost::system::error_code ec;
        const size_t sent = socket.write_some(
            boost::asio::buffer(bytes + sent_total, size - sent_total), ec);
        if (!ec) {
            sent_total += sent;
            continue;
        }
        if (ec == boost::asio::error::would_block) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        return false;
    }
    return true;
}

// Computes and sends the 101 Switching Protocols response. `request` must
// satisfy IsWebSocketUpgradeRequest(). Returns false if the write failed.
inline bool SendWebSocketUpgradeResponse(boost::asio::ip::tcp::socket& socket,
                                          const HttpRequest& request,
                                          const std::atomic_bool& stop_flag) {
    const std::string concatenated =
        request.headers.at("sec-websocket-key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    std::array<CryptoPP::byte, CryptoPP::SHA1::DIGESTSIZE> digest{};
    CryptoPP::SHA1().CalculateDigest(digest.data(),
                                      reinterpret_cast<const CryptoPP::byte*>(concatenated.data()),
                                      concatenated.size());

    std::string accept_b64;
    CryptoPP::StringSource(digest.data(), digest.size(), true,
                            new CryptoPP::Base64Encoder(new CryptoPP::StringSink(accept_b64),
                                                         /*insertLineBreaks=*/false));

    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_b64 << "\r\n\r\n";
    const std::string response_str = response.str();
    return SendAllBytes(socket, response_str.data(), response_str.size(), stop_flag);
}

constexpr u8 WS_OPCODE_TEXT = 0x1;
constexpr u8 WS_OPCODE_BINARY = 0x2;
constexpr u8 WS_OPCODE_CLOSE = 0x8;

// Sends one unmasked, unfragmented server->client frame (server frames in
// this protocol are never masked, see unison/websocket.h's own header
// comment on what it assumes of us).
inline bool SendWebSocketFrame(boost::asio::ip::tcp::socket& socket, u8 opcode,
                                const std::vector<u8>& payload, const std::atomic_bool& stop_flag) {
    std::vector<u8> frame;
    frame.reserve(payload.size() + 10);
    frame.push_back(static_cast<u8>(0x80 | (opcode & 0x0F))); // FIN=1, given opcode.

    const size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<u8>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<u8>((len >> 8) & 0xFF));
        frame.push_back(static_cast<u8>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.push_back(static_cast<u8>((static_cast<u64>(len) >> shift) & 0xFF));
    }
    frame.insert(frame.end(), payload.begin(), payload.end());

    return SendAllBytes(socket, frame.data(), frame.size(), stop_flag);
}

inline bool SendWebSocketBinaryFrame(boost::asio::ip::tcp::socket& socket,
                                      const std::vector<u8>& payload,
                                      const std::atomic_bool& stop_flag) {
    return SendWebSocketFrame(socket, WS_OPCODE_BINARY, payload, stop_flag);
}

inline bool SendWebSocketTextFrame(boost::asio::ip::tcp::socket& socket, const std::string& payload,
                                    const std::atomic_bool& stop_flag) {
    return SendWebSocketFrame(socket, WS_OPCODE_TEXT, std::vector<u8>(payload.begin(), payload.end()),
                               stop_flag);
}

struct ReceivedFrame {
    unison_ws_opcode opcode;
    std::vector<u8> payload;
};

// Tries to parse one client->server (masked) frame from the front of `buf`
// via unison_ws_parse_frame(), consuming those bytes from `buf` on success.
// Returns nullopt (leaving `buf` untouched) if there isn't a full frame yet;
// sets `*protocol_error` if the frame was malformed/oversized/fragmented
// (buf is fully consumed in that case too, since there's nothing more useful
// to do with it -- caller should treat this as a disconnect).
inline std::optional<ReceivedFrame> TryParseOneFrame(std::vector<u8>& buf, bool* protocol_error) {
    *protocol_error = false;
    if (buf.empty())
        return std::nullopt;
    unison_ws_frame frame{};
    const auto status = unison_ws_parse_frame(buf.data(), buf.size(), &frame);
    if (status == UNISON_WS_FRAME_INCOMPLETE)
        return std::nullopt;
    if (status == UNISON_WS_FRAME_ERR) {
        *protocol_error = true;
        buf.clear();
        return std::nullopt;
    }
    ReceivedFrame result;
    result.opcode = frame.opcode;
    result.payload.assign(frame.payload, frame.payload + frame.payload_size);
    buf.erase(buf.begin(), buf.begin() + static_cast<long>(frame.frame_size));
    return result;
}

// Reads off `socket` (already upgraded, non-blocking) until one full
// WebSocket frame has been received or `timeout` elapses. Used for the
// app-level handshake (handshake_messages.h), where exactly one text frame
// (hello_ack) is expected before any Video/Input binary frame.
inline std::optional<ReceivedFrame> ReceiveOneWebSocketFrame(boost::asio::ip::tcp::socket& socket,
                                                               const std::atomic_bool& stop_flag,
                                                               std::chrono::milliseconds timeout) {
    std::vector<u8> recv_buffer;
    std::array<u8, 4096> read_buf{};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (stop_flag)
            return std::nullopt;
        boost::system::error_code ec;
        const size_t received = socket.read_some(boost::asio::buffer(read_buf), ec);
        if (ec == boost::asio::error::would_block) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (ec)
            return std::nullopt;
        if (received > 0) {
            recv_buffer.insert(recv_buffer.end(), read_buf.begin(), read_buf.begin() + received);
            bool protocol_error = false;
            auto frame = TryParseOneFrame(recv_buffer, &protocol_error);
            if (frame)
                return frame;
            if (protocol_error)
                return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace Core::Streaming
