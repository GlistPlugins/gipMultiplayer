#include "Server.h"
#include "PlayerPackets.h"
#include "znet/server_events.h"
#include <algorithm>
#include <mutex>
#include <vector>

using namespace znet;

namespace {

std::mutex sessionMutex;
std::vector<std::shared_ptr<PeerSession>> sessions;

class ServerHandler : public PacketHandler<ServerHandler, PlayerStatePacket> {
public:
    explicit ServerHandler(std::shared_ptr<PeerSession> s) : peerSessionPtr(std::move(s)) {}

    void OnPacket(std::shared_ptr<PlayerStatePacket> p) {
        std::lock_guard<std::mutex> lk(sessionMutex);

        for (auto& s : sessions) {
            if (!s || s.get() == peerSessionPtr.get()) continue;
            s->SendPacket(p);
        }
    }

    void OnUnknown(std::shared_ptr<Packet>) {}

private:
    std::shared_ptr<PeerSession> peerSessionPtr;
};

bool OnIncoming(ServerClientConnectedEvent& e) {
    auto sess = e.session();
    auto codec = std::make_shared<Codec>();

    codec->Add(PACKET_PLAYER_STATE, std::make_unique<PlayerStateSerializer>());
    sess->SetCodec(codec);
    sess->SetHandler(std::make_shared<ServerHandler>(sess));
    std::lock_guard<std::mutex> lk(sessionMutex);
    sessions.push_back(sess);

    return false;
}

bool OnDisconnect(ServerClientDisconnectedEvent& e) {
    std::lock_guard<std::mutex> lk(sessionMutex);

    sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
        [&](const std::shared_ptr<PeerSession>& s){ return s.get() == e.session().get(); }),
        sessions.end());

    return false;
}

void OnEvent(Event& ev) {
    EventDispatcher d{ev};

    d.Dispatch<ServerClientConnectedEvent>(ZNET_BIND_GLOBAL_FN(OnIncoming));
    d.Dispatch<ServerClientDisconnectedEvent>(ZNET_BIND_GLOBAL_FN(OnDisconnect));
}
}

std::unique_ptr<Server> createGameServer(const std::string& bindIp, uint16_t port) {
    auto srv = std::make_unique<Server>(ServerConfig{bindIp, port});
    srv->SetEventCallback(ZNET_BIND_GLOBAL_FN(OnEvent));

    if (srv->Bind()   != Result::Success) return nullptr;
    if (srv->Listen() != Result::Success) return nullptr;

    return srv;
}