#include "Client.h"
#include "PlayerPackets.h"
#include "znet/client_events.h"

using namespace znet;

namespace {
    std::shared_ptr<PeerSession> session;
    std::function<void(uint32_t,float,float,bool)> onRemote;

    class ClientHandler : public PacketHandler<ClientHandler, PlayerStatePacket> {
    public:
        explicit ClientHandler(std::shared_ptr<PeerSession> s) : peerSessionPtr(std::move(s)) {}

        void OnPacket(std::shared_ptr<PlayerStatePacket> p) {
            if (onRemote) onRemote(p->pid, p->x, p->y, p->isEmpty != 0);
        }

        void OnUnknown(std::shared_ptr<Packet>) {}
    private:
        std::shared_ptr<PeerSession> peerSessionPtr;
    };

    bool OnConnected(ClientConnectedToServerEvent& e) {
        auto sess = e.session();
        auto codec = std::make_shared<Codec>();

        codec->Add(PACKET_PLAYER_STATE, std::make_unique<PlayerStateSerializer>());
        sess->SetCodec(codec);
        sess->SetHandler(std::make_shared<ClientHandler>(sess));
        session = sess;

        return false;
    }

    void OnEvent(Event& ev) {
        EventDispatcher d{ev};
        d.Dispatch<ClientConnectedToServerEvent>(ZNET_BIND_GLOBAL_FN(OnConnected));
    }
}

std::unique_ptr<Client> createGameClient(const std::string& serverIp, uint16_t port) {
    auto cli = std::make_unique<Client>(ClientConfig{serverIp, port});
    cli->SetEventCallback(ZNET_BIND_GLOBAL_FN(OnEvent));

    if (cli->Bind()    != Result::Success) return nullptr;
    if (cli->Connect() != Result::Success) return nullptr;

    return cli;
}

void setRemoteStateCallback(const std::function<void(uint32_t,float,float,bool)>& cb) {
    onRemote = cb;
}

bool sendLocalState(uint32_t id, float x, float y, bool isGreen) {
    if (!session) return false;

    auto p = std::make_shared<PlayerStatePacket>();
    p->pid = id; p->x = x; p->y = y; p->isEmpty = isGreen ? 1 : 0;

    return session->SendPacket(p);
}