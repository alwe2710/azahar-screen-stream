// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/streaming/beacon.h"

#include <chrono>
#include <thread>

#include <boost/asio.hpp>
#include <json.hpp>

#include "core/core.h"
#include "core/loader/loader.h"
#include "core/streaming/stream_constants.h"

namespace Core::Streaming {

namespace {
// UDP "connect" (no packet actually leaves for a connectionless socket --
// it just resolves local routing) to a well-known external address, then
// reads back which local interface/address the OS picked for that route.
// Same trick as many other "what's my LAN IP" implementations; doesn't
// require the address to be reachable, only routable.
std::string ProbeLocalHost() {
    try {
        boost::asio::io_context io_context;
        boost::asio::ip::udp::socket probe(io_context, boost::asio::ip::udp::v4());
        probe.connect(
            boost::asio::ip::udp::endpoint(boost::asio::ip::make_address_v4("8.8.8.8"), 80));
        return probe.local_endpoint().address().to_string();
    } catch (const boost::system::system_error&) {
        return std::string();
    }
}
} // namespace

Beacon::Beacon(Core::System& system_, u16 handshake_port_)
    : system(system_), handshake_port(handshake_port_), local_host(ProbeLocalHost()) {
    thread = std::thread([this] { Run(); });
}

Beacon::~Beacon() {
    stop = true;
    if (thread.joinable())
        thread.join();
}

std::string Beacon::BuildMessage() const {
    // Same source the RPC/telemetry title getters use elsewhere in this
    // codebase -- the loaded game, not any concept of "what's on the bottom
    // screen right now" (there's no separate content selection here the way
    // GC_GBA_LINK has a GBA cartridge distinct from the GC game).
    std::string title;
    if (system.GetAppLoader().ReadTitle(title) != Loader::ResultStatus::Success || title.empty())
        title = EMULATOR_IDENTIFIER;

    nlohmann::json obj;
    obj["type"] = "unison_beacon";
    obj["protocol_version"] = STREAM_PROTOCOL_VERSION;
    obj["emulator_identifier"] = EMULATOR_IDENTIFIER;
    obj["game_title"] = title;
    obj["stream_type"] = STREAM_TYPE;
    obj["host"] = local_host;
    obj["handshake_port"] = handshake_port;
    return obj.dump();
}

void Beacon::Run() {
    boost::asio::io_context io_context;
    boost::asio::ip::udp::socket socket(io_context, boost::asio::ip::udp::v4());
    boost::system::error_code ec;
    socket.set_option(boost::asio::socket_base::broadcast(true), ec);

    // Bind to the specific interface local_host resolved to (see
    // ProbeLocalHost()'s own comment above) before sending. On a machine
    // with more than one active network interface (VPN, Docker/virtual
    // adapters, Ethernet + Wi-Fi both up -- not unusual for a dev/gaming
    // PC), leaving the socket unbound lets the OS pick whichever interface
    // its default route for 255.255.255.255 happens to be, which is not
    // guaranteed to be the same interface a discovering client (e.g. a 3DS
    // on Wi-Fi) is actually reachable on -- the broadcast can leave via a
    // completely different interface than the LAN the client is listening
    // on, silently going nowhere a client will ever see. Binding pins the
    // send to the interface local_host itself already names, which is also
    // the address embedded in the beacon message clients use to connect
    // back -- if that address weren't reachable, nothing would work
    // regardless, so this can't make things worse than before.
    if (!local_host.empty()) {
        boost::system::error_code bind_ec;
        socket.bind(boost::asio::ip::udp::endpoint(
                        boost::asio::ip::make_address_v4(local_host), 0),
                    bind_ec);
        // Best-effort: if this fails (e.g. local_host somehow isn't a valid
        // local address anymore), fall through and send unbound, same as
        // before this fix existed.
    }

    const boost::asio::ip::udp::endpoint broadcast_endpoint(
        boost::asio::ip::address_v4::broadcast(), BEACON_PORT);

    while (!stop) {
        const std::string message = BuildMessage();
        // Best-effort: a dropped/failed broadcast just means this tick's
        // beacon didn't go out, no different from ordinary UDP loss -- the
        // next tick covers for it.
        socket.send_to(boost::asio::buffer(message), broadcast_endpoint, 0, ec);

        // Polls `stop` every 100ms instead of sleeping the full interval in
        // one call, so the destructor doesn't have to wait out an
        // in-progress interval.
        for (auto waited = std::chrono::milliseconds::zero(); waited < BEACON_INTERVAL && !stop;
             waited += std::chrono::milliseconds(100)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace Core::Streaming
