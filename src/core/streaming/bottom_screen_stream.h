// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// Server implementation for the N3DS_BOTTOM_SCREEN finlink stream type: a
// single-slot WebSocket server that streams the bottom screen to one remote
// client and accepts touch input back, per finlink's docs/protocol.md.
//
// Lifecycle mirrors Core::RPC::Server (src/core/rpc/server.h): constructed
// in System::Init() (after `gpu`, since capture needs
// system.GPU().Renderer()) when Settings::values.enable_bottom_screen_streaming
// is set, destroyed at the top of System::Shutdown() (before `gpu.reset()`,
// so no capture callback can ever observe a torn-down renderer).
//
// Networking/threading structure ported from the sibling dolphin-gba-stream
// project's GBAStreamHost -- one accept thread (boost::asio, async) that
// hands each accepted connection to its own std::thread rather than serving
// it inline (see that project's AcceptLoop() comment on why: serving inline
// would starve every other connection attempt, e.g. a second client probing
// while the first is still streaming, for the whole session's duration).
// Only ever one *active* streaming session at a time here (single slot,
// enforced in ServeConnection via `active`), but connections are still
// served off the accept thread so a second, doomed-to-be-rejected connection
// attempt gets its handshake_error promptly instead of queueing behind an
// open session that might last hours.

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "common/common_types.h"
#include "core/frontend/framebuffer_layout.h"

namespace Core {
class System;
}

namespace Core::Streaming {

class Beacon;

// Current remote touch state, read by the HID module's UpdatePadCallback
// (src/core/hle/service/hid/hid.cpp) once per pad update while a client is
// actively streaming; see Server::GetTouchOverride().
struct TouchOverride {
    bool pressed;
    u16 x;
    u16 y;
};

class Server {
public:
    explicit Server(Core::System& system, u16 port);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // nullopt whenever no client is in an active (post-session_ready)
    // session -- callers should fall back to local touch input in that case,
    // not just leave the last remote position sitting there un-refreshed.
    [[nodiscard]] std::optional<TouchOverride> GetTouchOverride() const;

private:
    // Arms the next async_accept(); re-arms itself from within the
    // completion handler, so the whole accept loop lives on io_thread
    // without ever blocking it.
    void DoAccept();
    // Runs on its own thread, one per accepted connection -- never called
    // inline from DoAccept()'s completion handler itself (see this file's
    // top comment).
    void ServeConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void RunSession(boost::asio::ip::tcp::socket& socket);

    // Arms the next screenshot capture. Safe to call from any thread --
    // RequestScreenshot() itself just sets a few fields the render thread
    // later picks up (see video_core/renderer_base.h); the actual pixel
    // readback happens on the render thread in OnScreenshotComplete().
    void ArmCapture();
    // Runs on the render thread, called synchronously from inside
    // RendererXXX::RenderScreenshot() -- must stay fast (copy out, re-arm),
    // per this project's own renderer_opengl.cpp::RenderScreenshot()
    // implementation, which calls this callback directly from GL code.
    void OnScreenshotComplete(bool result);

    Core::System& system;
    const u16 port;

    boost::asio::io_context io_context;
    boost::asio::ip::tcp::acceptor acceptor;
    std::thread io_thread;
    std::atomic_bool stop{false};

    std::mutex connection_threads_mutex;
    std::vector<std::thread> connection_threads;

    // Claimed by the one session currently allowed to stream (this stream
    // type has exactly one slot, see handshake_messages.cpp). Compare-
    // exchanged in ServeConnection(); cleared when that session ends.
    std::atomic_bool active{false};

    // The fixed 320x240 bottom-screen-only layout every capture uses,
    // constructed once in the constructor (see bottom_screen_stream.cpp --
    // deliberately hand-built rather than reusing
    // Layout::FrameLayoutFromResolutionScale(), which is driven by the
    // user's own display settings and has no "just the bottom screen, at its
    // native size, for a headless capture" mode).
    Layout::FramebufferLayout capture_layout;
    // Persistent buffer RequestScreenshot() writes into (BGRA8, one entry
    // per pixel) -- must stay alive and untouched between ArmCapture() and
    // OnScreenshotComplete() firing.
    std::vector<u8> capture_buffer;

    std::mutex frame_mutex;
    std::vector<u8> latest_frame_bgra8;
    u64 frame_id = 0;

    std::atomic_bool touch_active{false};
    std::atomic_bool touch_pressed{false};
    std::atomic<u16> touch_x{0};
    std::atomic<u16> touch_y{0};

    std::unique_ptr<Beacon> beacon;
};

} // namespace Core::Streaming
