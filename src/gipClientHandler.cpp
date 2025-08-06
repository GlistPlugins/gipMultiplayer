//
// Created by Yusuf Ustaoglu on 5.08.2025.
//

#include "gipClientHandler.h"

std::shared_ptr<PeerSession> session;
std::function<void(uint32_t, float, float, float)> onRemote;
std::function<void(uint32_t)> onDisconnect;
std::function<void()> onServerDisconnected;

void gipClientHandler::OnPacket(std::shared_ptr<PlayerStatePacket> p) {
    if (onRemote) onRemote(p->pid, p->x, p->y, p->z);
}

void gipClientHandler::OnPacket(std::shared_ptr<PlayerDisconnectPacket> p) {
    if (onDisconnect) onDisconnect(p->pid);
}

void gipClientHandler::OnUnknown(std::shared_ptr<Packet> p) {
    ZNET_LOG_INFO("Unknown packet");
}

namespace {
bool OnConnected(ClientConnectedToServerEvent& e) {
    auto sess = e.session();
    auto codec = std::make_shared<Codec>();

    codec->Add(PACKET_PLAYER_STATE, std::make_unique<PlayerStateSerializer>());
    codec->Add(PACKET_PLAYER_DISCONNECT, std::make_unique<PlayerDisconnectSerializer>());
    sess->SetCodec(codec);
    sess->SetHandler(std::make_shared<gipClientHandler>(sess));
    session = sess;

    return false;
}

bool OnDisconnected(ClientDisconnectedFromServerEvent& e) {
	if (session && session.get() == e.session().get()) {
		session.reset();
	}
	if (onServerDisconnected) onServerDisconnected();

	return false;
}

void OnEvent(Event& ev) {
    EventDispatcher d{ev};
    d.Dispatch<ClientConnectedToServerEvent>(ZNET_BIND_GLOBAL_FN(OnConnected));
	d.Dispatch<ClientDisconnectedFromServerEvent>(ZNET_BIND_GLOBAL_FN(OnDisconnected));
}
}
std::unique_ptr<Client> createGameClient(const std::string& serverIp, uint16_t port) {
    auto cli = std::make_unique<Client>(ClientConfig{serverIp, port});
    cli->SetEventCallback(ZNET_BIND_GLOBAL_FN(OnEvent));

    if (cli->Bind()    != Result::Success) return nullptr;
    if (cli->Connect() != Result::Success) return nullptr;
    return cli;
}

void setRemoteStateCallback(const std::function<void(uint32_t, float, float, float)>& cb) {
    onRemote = cb;
}

void setPlayerDisconnectCallback(const std::function<void(uint32_t)>& cb) {
    onDisconnect = cb;
}

void setServerDisconnectedCallback(const std::function<void()>& cb) {
	onServerDisconnected = cb;
}

bool sendLocalState(uint32_t id, float x, float y, float z) {
    if (!session) return false;
    auto p = std::make_shared<PlayerStatePacket>();
    p->pid = id; p->x = x; p->y = y; p->z = z;
    return session->SendPacket(p);
}