// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/streaming/bottom_screen_stream.h"

#include <chrono>

#include <finlink/deflate.h>
#include <finlink/protocol.h>

#include "common/logging/log.h"
#include "core/core.h"
#include "core/streaming/beacon.h"
#include "core/streaming/handshake_messages.h"
#include "core/streaming/stream_constants.h"
#include "core/streaming/websocket_transport.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

namespace Core::Streaming {

namespace {

// Deliberately hand-built rather than reusing any of
// Layout::FrameLayoutFromResolutionScale()'s family of factory functions --
// those are all driven by the *user's own* display settings (aspect ratio,
// integer scaling, stretch padding), none of which apply to this headless,
// protocol-mandated "always exactly 320x240, bottom screen only" capture.
Layout::FramebufferLayout BuildCaptureLayout() {
    Layout::FramebufferLayout layout{};
    layout.width = STREAM_WIDTH;
    layout.height = STREAM_HEIGHT;
    layout.top_screen_enabled = false;
    layout.bottom_screen_enabled = true;
    layout.top_screen = {0, 0, 0, 0};
    layout.bottom_screen = {0, 0, STREAM_WIDTH, STREAM_HEIGHT};
    // Matches SingleFrameLayout()'s own default (!upright) value for a
    // non-portrait-mode capture -- see framebuffer_layout.cpp.
    layout.is_rotated = true;
    return layout;
}

void AppendU32LE(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value & 0xFF));
    out.push_back(static_cast<u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<u8>((value >> 16) & 0xFF));
    out.push_back(static_cast<u8>((value >> 24) & 0xFF));
}

// Converts a glReadPixels(GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV) capture
// (memory byte order B,G,R,A per pixel) into row-major u16le RGB565,
// flipping vertically: OpenGL's readback has row 0 at the bottom of the
// image (window-coordinate convention), but the wire format -- like every
// other framebuffer this protocol ever sends -- is top-down.
void ConvertBgra8ToRgb565Flipped(const std::vector<u8>& bgra8, u32 width, u32 height,
                                  std::vector<u8>& out_rgb565) {
    out_rgb565.resize(static_cast<size_t>(width) * height * 2);
    for (u32 y = 0; y < height; y++) {
        const u32 src_row = height - 1 - y;
        const u8* src = bgra8.data() + static_cast<size_t>(src_row) * width * 4;
        u8* dst = out_rgb565.data() + static_cast<size_t>(y) * width * 2;
        for (u32 x = 0; x < width; x++) {
            const u8 b = src[x * 4 + 0];
            const u8 g = src[x * 4 + 1];
            const u8 r = src[x * 4 + 2];
            const u16 pixel =
                static_cast<u16>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            dst[x * 2 + 0] = static_cast<u8>(pixel & 0xFF);
            dst[x * 2 + 1] = static_cast<u8>((pixel >> 8) & 0xFF);
        }
    }
}

bool SendVideoFrame(boost::asio::ip::tcp::socket& socket, const std::vector<u8>& bgra8,
                     const std::atomic_bool& stop_flag) {
    std::vector<u8> rgb565;
    ConvertBgra8ToRgb565Flipped(bgra8, STREAM_WIDTH, STREAM_HEIGHT, rgb565);

    std::vector<u8> compressed(finlink_deflate_max_size(rgb565.size()));
    size_t compressed_size = 0;
    if (finlink_deflate_raw(rgb565.data(), rgb565.size(), compressed.data(), compressed.size(),
                             &compressed_size) != FINLINK_DEFLATE_OK) {
        LOG_ERROR(Core, "Bottom screen stream: failed to compress video frame");
        return false;
    }
    compressed.resize(compressed_size);

    std::vector<u8> message;
    message.reserve(10 + compressed.size());
    message.push_back(static_cast<u8>(FINLINK_MSG_VIDEO));
    AppendU32LE(message, STREAM_WIDTH);
    AppendU32LE(message, STREAM_HEIGHT);
    message.push_back(0); // format = 0: full frame, raw (non-indexed, non-tiled) RGB565.
    message.insert(message.end(), compressed.begin(), compressed.end());

    return SendWebSocketBinaryFrame(socket, message, stop_flag);
}

} // namespace

Server::Server(Core::System& system_, u16 port_)
    : system(system_), port(port_),
      acceptor(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port_)),
      capture_layout(BuildCaptureLayout()) {
    capture_buffer.resize(static_cast<size_t>(STREAM_WIDTH) * STREAM_HEIGHT * 4);

    DoAccept();
    io_thread = std::thread([this] { io_context.run(); });

    beacon = std::make_unique<Beacon>(system, port);

    LOG_INFO(Core, "Bottom screen stream server listening on port {}", port);
}

Server::~Server() {
    stop = true;
    beacon.reset();

    boost::system::error_code ec;
    acceptor.close(ec);
    io_context.stop();
    if (io_thread.joinable())
        io_thread.join();

    std::vector<std::thread> threads_to_join;
    {
        std::lock_guard lock(connection_threads_mutex);
        threads_to_join = std::move(connection_threads);
    }
    for (auto& thread : threads_to_join) {
        if (thread.joinable())
            thread.join();
    }
}

std::optional<TouchOverride> Server::GetTouchOverride() const {
    if (!touch_active.load(std::memory_order_relaxed))
        return std::nullopt;
    TouchOverride result;
    result.pressed = touch_pressed.load(std::memory_order_relaxed);
    result.x = touch_x.load(std::memory_order_relaxed);
    result.y = touch_y.load(std::memory_order_relaxed);
    return result;
}

void Server::DoAccept() {
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context);
    acceptor.async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        if (!ec) {
            std::lock_guard lock(connection_threads_mutex);
            connection_threads.emplace_back([this, socket] { ServeConnection(socket); });
        }
        if (!stop)
            DoAccept();
    });
}

void Server::ServeConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    boost::system::error_code ec;
    socket->non_blocking(true, ec);
    socket->set_option(boost::asio::ip::tcp::no_delay(true), ec);

    const auto request = ReadHttpRequest(*socket, stop);
    if (!request || !IsWebSocketUpgradeRequest(*request))
        return;
    if (!SendWebSocketUpgradeResponse(*socket, *request, stop))
        return;

    if (!SendWebSocketTextFrame(*socket, BuildHelloMessage(), stop))
        return;

    const auto frame = ReceiveOneWebSocketFrame(*socket, stop, std::chrono::seconds(5));
    if (!frame || frame->opcode != FINLINK_WS_OPCODE_TEXT)
        return;

    const auto ack = ParseHelloAck(frame->payload);
    if (!ack) {
        SendWebSocketTextFrame(
            *socket,
            BuildHandshakeErrorMessage(HandshakeErrorCode::MalformedRequest, "Malformed hello_ack"),
            stop);
        return;
    }
    if (ack->protocol_version != STREAM_PROTOCOL_VERSION) {
        SendWebSocketTextFrame(*socket,
                                BuildHandshakeErrorMessage(HandshakeErrorCode::VersionMismatch,
                                                            "Protocol version mismatch"),
                                stop);
        return;
    }

    bool expected = false;
    if (!active.compare_exchange_strong(expected, true)) {
        SendWebSocketTextFrame(
            *socket,
            BuildHandshakeErrorMessage(HandshakeErrorCode::SlotUnavailable,
                                        "Bottom screen stream already has an active client"),
            stop);
        return;
    }

    if (!SendWebSocketTextFrame(*socket, BuildSessionReadyMessage(), stop)) {
        active = false;
        return;
    }

    RunSession(*socket);

    touch_active = false;
    touch_pressed = false;
    active = false;
}

void Server::RunSession(boost::asio::ip::tcp::socket& socket) {
    touch_active = true;
    frame_id = 0;
    u64 last_sent_frame_id = 0;
    ArmCapture();

    std::vector<u8> recv_buffer;
    std::array<u8, 4096> read_buf{};

    while (!stop) {
        std::vector<u8> frame_copy;
        u64 current_id = 0;
        {
            std::lock_guard lock(frame_mutex);
            current_id = frame_id;
            if (current_id != last_sent_frame_id)
                frame_copy = latest_frame_bgra8;
        }
        if (!frame_copy.empty()) {
            if (!SendVideoFrame(socket, frame_copy, stop))
                return;
            last_sent_frame_id = current_id;
        }

        boost::system::error_code ec;
        const size_t received = socket.read_some(boost::asio::buffer(read_buf), ec);
        if (ec && ec != boost::asio::error::would_block)
            return; // Disconnected or errored.
        if (!ec && received > 0) {
            recv_buffer.insert(recv_buffer.end(), read_buf.begin(), read_buf.begin() + received);
            for (;;) {
                bool protocol_error = false;
                auto parsed = TryParseOneFrame(recv_buffer, &protocol_error);
                if (!parsed) {
                    if (protocol_error)
                        return;
                    break;
                }
                if (parsed->opcode == FINLINK_WS_OPCODE_CLOSE)
                    return;
                if (parsed->opcode != FINLINK_WS_OPCODE_BINARY)
                    continue;
                finlink_touch_state touch{};
                if (finlink_parse_touch_frame(parsed->payload.data(), parsed->payload.size(),
                                               &touch) == FINLINK_OK) {
                    touch_pressed = touch.pressed != 0;
                    touch_x = touch.x;
                    touch_y = touch.y;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}

void Server::ArmCapture() {
    if (stop || !active)
        return;
    auto& renderer = system.GPU().Renderer();
    if (renderer.IsScreenshotPending())
        return;
    renderer.RequestScreenshot(
        capture_buffer.data(), [this](bool result) { OnScreenshotComplete(result); },
        capture_layout);
}

void Server::OnScreenshotComplete(bool result) {
    if (result) {
        std::lock_guard lock(frame_mutex);
        latest_frame_bgra8 = capture_buffer;
        frame_id++;
    }
    if (!stop && active)
        ArmCapture();
}

} // namespace Core::Streaming
