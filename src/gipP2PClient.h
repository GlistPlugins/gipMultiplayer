#pragma once

#include "znet/p2p/host.h"
#include "znet/peer_session.h"
#include <cstdint>
#include <memory>
#include <string>

// A punched connection: the socket it lives on and the session over it. The
// host drives the session, so it has to outlive it.
struct gipP2PSession {
    std::unique_ptr<znet::p2p::Host> host;
    std::shared_ptr<znet::PeerSession> session;

    explicit operator bool() const { return session != nullptr; }
};

class gipP2PClient {
public:
    gipP2PClient();
    ~gipP2PClient();

    // Gathers this socket's candidates against the master's relay, asks the
    // master to broker roomCode/IP, punches on what comes back (the relay
    // included) and returns the ready session. Blocks for the whole flow.
    // extraReflector is an optional second reflector on a distinct IP; gathering
    // from both classifies the NAT. Null when the master runs none.
    gipP2PSession joinSession(const std::string& masterIp, uint16_t masterPort, uint16_t masterRelayPort,
                              std::shared_ptr<znet::InetAddress> extraReflector,
                              const std::string& targetIdentifier, uint16_t localGamePort);
};
