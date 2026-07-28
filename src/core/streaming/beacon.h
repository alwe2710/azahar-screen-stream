// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// UDP discovery beacon for the bottom-screen streaming server, so finlink
// clients (3DS/Switch/NDS/Android/web) can find this Azahar instance on the
// LAN the same way they find a dolphin-gba-stream instance -- both broadcast
// the same `finlink_beacon` JSON shape on the same fixed port
// (finlink/discovery.h's FINLINK_BEACON_PORT, re-declared as
// Core::Streaming::BEACON_PORT in stream_constants.h), so one client-side
// listener works against either kind of server without caring which it's
// looking at (see finlink docs/protocol.md's "Discovery-Beacon" section).
// Ported from the sibling dolphin-gba-stream project's
// Source/Core/Core/HW/GBAStreamBeacon.h/.cpp: same message shape and
// broadcast cadence, boost::asio UDP socket instead of SFML.

#include <atomic>
#include <string>
#include <thread>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace Core::Streaming {

class Beacon {
public:
    // `handshake_port` is what gets advertised as the beacon's
    // "handshake_port" field -- the port a discovering client should then
    // open its own WebSocket connection to.
    explicit Beacon(Core::System& system, u16 handshake_port);
    ~Beacon();

    Beacon(const Beacon&) = delete;
    Beacon& operator=(const Beacon&) = delete;

private:
    void Run();
    std::string BuildMessage() const;

    Core::System& system;
    const u16 handshake_port;
    // Resolved once at construction (see beacon.cpp) rather than on every
    // tick -- the local outbound-facing address essentially never changes
    // mid-session, unlike the game title, which BuildMessage() does refresh
    // every tick.
    std::string local_host;
    std::atomic_bool stop{false};
    std::thread thread;
};

} // namespace Core::Streaming
