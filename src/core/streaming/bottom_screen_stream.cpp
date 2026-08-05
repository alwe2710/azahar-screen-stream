// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/streaming/bottom_screen_stream.h"

#include <chrono>
#include <utility>

#include <unison/deflate.h>
#include <unison/protocol.h>

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

// Converts a captured BGRA8 framebuffer (memory byte order B,G,R,A per
// pixel) into row-major u16le RGB565, optionally flipping vertically --
// `flip` should be the invert_y the capture callback reported
// (Server::OnScreenshotComplete): true for OpenGL's glReadPixels
// convention (row 0 at the bottom of the image), false for Vulkan's
// vkCmdCopyImageToBuffer (already top-down). The wire format, like every
// other framebuffer this protocol ever sends, is top-down either way.
void ConvertBgra8ToRgb565(const std::vector<u8>& bgra8, u32 width, u32 height, bool flip,
                           std::vector<u8>& out_rgb565) {
    out_rgb565.resize(static_cast<size_t>(width) * height * 2);
    for (u32 y = 0; y < height; y++) {
        const u32 src_row = flip ? height - 1 - y : y;
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
                     bool invert_y, const std::atomic_bool& stop_flag) {
    std::vector<u8> rgb565;
    ConvertBgra8ToRgb565(bgra8, STREAM_WIDTH, STREAM_HEIGHT, invert_y, rgb565);

    std::vector<u8> compressed(unison_deflate_max_size(rgb565.size()));
    size_t compressed_size = 0;
    if (unison_deflate_raw(rgb565.data(), rgb565.size(), compressed.data(), compressed.size(),
                             &compressed_size) != UNISON_DEFLATE_OK) {
        LOG_ERROR(Core, "Bottom screen stream: failed to compress video frame");
        return false;
    }
    compressed.resize(compressed_size);

    std::vector<u8> message;
    message.reserve(10 + compressed.size());
    message.push_back(static_cast<u8>(UNISON_MSG_VIDEO));
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

std::optional<unison_extended_input> Server::GetInputOverride() const {
    if (!input_active.load(std::memory_order_relaxed))
        return std::nullopt;
    std::lock_guard lock(input_mutex);
    return latest_input;
}

void Server::SetMicWanted(bool wanted, u32 sample_rate) {
    std::lock_guard lock(mic_mutex);
    mic_wanted = wanted;
    mic_wanted_sample_rate = sample_rate;
}

std::vector<u8> Server::PollMicAudio() {
    std::lock_guard lock(mic_mutex);
    return std::exchange(pending_mic_audio, std::vector<u8>{});
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
    if (!frame || frame->opcode != UNISON_WS_OPCODE_TEXT)
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

    input_active = false;
    active = false;
    // Drop any mic audio this client sent but nobody drained yet -- left
    // sitting here, it would otherwise get fed to UnisonInput::Read() as
    // if it were fresh once a later session (or a belated poll from this
    // one) reads it, mislabeling stale audio as current.
    {
        std::lock_guard lock(mic_mutex);
        pending_mic_audio.clear();
    }
}

void Server::RunSession(boost::asio::ip::tcp::socket& socket) {
    input_active = true;
    frame_id = 0;
    u64 last_sent_frame_id = 0;
    // Edge-detection state for the mic-enable signal -- RunSession-local
    // (not a member) since it only means anything for this one session,
    // same reasoning as last_sent_frame_id above. Starts at "not wanted" so
    // a session that begins with mic_wanted already true (a game started
    // sampling before this client connected) still sends an initial
    // enable=1 on its first loop iteration.
    bool last_sent_mic_wanted = false;
    u32 last_sent_mic_sample_rate = 0;
    ArmCapture();

    std::vector<u8> recv_buffer;
    std::array<u8, 4096> read_buf{};

    while (!stop) {
        std::vector<u8> frame_copy;
        bool frame_invert_y = false;
        u64 current_id = 0;
        {
            std::lock_guard lock(frame_mutex);
            current_id = frame_id;
            if (current_id != last_sent_frame_id) {
                frame_copy = latest_frame_bgra8;
                frame_invert_y = latest_frame_invert_y;
            }
        }
        if (!frame_copy.empty()) {
            if (!SendVideoFrame(socket, frame_copy, frame_invert_y, stop))
                return;
            last_sent_frame_id = current_id;
        }

        {
            bool wanted;
            u32 sample_rate;
            {
                std::lock_guard lock(mic_mutex);
                wanted = mic_wanted;
                sample_rate = mic_wanted_sample_rate;
            }
            if (wanted != last_sent_mic_wanted ||
                (wanted && sample_rate != last_sent_mic_sample_rate)) {
                const unison_mic_enable enable{wanted ? 1 : 0, sample_rate};
                u8 payload[UNISON_MIC_ENABLE_FRAME_SIZE];
                unison_build_mic_enable_frame(&enable, payload);
                std::vector<u8> message(payload, payload + UNISON_MIC_ENABLE_FRAME_SIZE);
                if (!SendWebSocketBinaryFrame(socket, message, stop))
                    return;
                last_sent_mic_wanted = wanted;
                last_sent_mic_sample_rate = sample_rate;
            }
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
                if (parsed->opcode == UNISON_WS_OPCODE_CLOSE)
                    return;
                if (parsed->opcode != UNISON_WS_OPCODE_BINARY)
                    continue;
                unison_msg_type type;
                if (unison_peek_type(parsed->payload.data(), parsed->payload.size(), &type) !=
                    UNISON_OK)
                    continue;
                if (type == UNISON_MSG_INPUT) {
                    unison_extended_input input{};
                    if (unison_parse_extended_input_frame(parsed->payload.data(),
                                                            parsed->payload.size(),
                                                            &input) == UNISON_OK) {
                        std::lock_guard lock(input_mutex);
                        latest_input = input;
                    }
                } else if (type == UNISON_MSG_MIC_AUDIO) {
                    unison_audio_frame audio;
                    if (unison_parse_mic_audio_frame(parsed->payload.data(),
                                                       parsed->payload.size(),
                                                       &audio) == UNISON_OK) {
                        std::lock_guard lock(mic_mutex);
                        // PollMicAudio()/UnisonInput::Read() only ever see
                        // raw sample bytes, not a rate -- they trust the
                        // client to always send at whatever rate the last
                        // MIC_ENABLE requested. Reject anything else here
                        // instead, rather than silently mixing differently-
                        // rated audio into one buffer that gets played back
                        // as if it were all mic_wanted_sample_rate.
                        if (audio.sample_rate != mic_wanted_sample_rate)
                            continue;
                        // ~2s cap at typical mic rates -- if UnisonInput::
                        // Read() ever falls behind that far, drop the
                        // backlog rather than grow it unboundedly (same
                        // tradeoff WiiuGamepadStream::SubmitGamepadAudio()
                        // makes for the reverse direction in Cemu).
                        constexpr size_t kMaxPendingBytes = 48000 * sizeof(s16) * 2;
                        const size_t byte_len = audio.sample_count * sizeof(s16);
                        if (pending_mic_audio.size() + byte_len > kMaxPendingBytes)
                            pending_mic_audio.clear();
                        pending_mic_audio.insert(pending_mic_audio.end(), audio.samples,
                                                  audio.samples + byte_len);
                    }
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
        capture_buffer.data(), [this](bool invert_y) { OnScreenshotComplete(invert_y); },
        capture_layout);
}

// RequestScreenshot's callback bool is *not* a success flag -- neither
// backend has a failure path of its own, they always populate the buffer
// before calling it (see RendererOpenGL::RenderScreenshot() and
// RendererVulkan::RenderScreenshot(), video_core/renderer_*/). It's
// invert_y: whether the just-captured buffer is bottom-up and needs
// flipping before use (true for OpenGL's glReadPixels convention, false for
// Vulkan's vkCmdCopyImageToBuffer, see citra_qt/bootmanager.cpp's own
// screenshot-to-file feature for the other consumer of this same signal).
// This used to be misread as a success flag here, silently discarding every
// frame on the Vulkan backend (default renderer) while leaking an
// unconditional flip into the OpenGL path (see SendVideoFrame's own flip
// argument below) -- both fixed together, since they're the same root
// misunderstanding.
void Server::OnScreenshotComplete(bool invert_y) {
    {
        std::lock_guard lock(frame_mutex);
        latest_frame_bgra8 = capture_buffer;
        latest_frame_invert_y = invert_y;
        frame_id++;
    }
    if (!stop && active)
        ArmCapture();
}

} // namespace Core::Streaming
