#include "GameBackendLocal.h"
#include "NetworkManager.h"
#include "voice/gTeamVoicePackets.h"
#include <algorithm>
#include <memory>
#include <thread>
#include "master/gMasterPackets.h"
#include "znet/client.h"
#include "znet/client_events.h"
#include "znet/p2p/punch.h"
#include "znet/inet_addr.h"

constexpr uint64_t LOCAL_HOST_VOICE_CONN_ID = 0xFFFFFFFFFFFFFFFFULL;

// Handles packets arriving from a connected client.
// When a client sends its position (NodeStatePacket):
//   1. Enqueues it into the host's backend so the host sees the remote player.
//   2. Tags the session with the sender's ID (for identification on disconnect).
//   3. Rebroadcasts the packet to all OTHER connected clients.
class ServerPacketHandler : public znet::PacketHandler<ServerPacketHandler,
	NodeStatePacket, NodeLeavePacket, PlayerFirePacket, PlayerHitPacket, PlayerKilledPacket,
	ServerQueryReqPacket, LobbyJoinPacket, ToggleReadyPacket, SwitchTeamPacket, StartMatchPacket,
	KeepAlivePacket, PingPacket, PongPacket, gTeamVoiceUplinkPacket> {
public:
	ServerPacketHandler(GameBackendLocal* b, znet::PeerSession* s) : backend(b), peersession(s) {}

	void OnPacket(std::shared_ptr<NodeStatePacket> p) {
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
		peersession->SetUserPointer(std::make_shared<uint32_t>(p->netid));
		backend->broadcast(p, peersession);
	}

	void OnPacket(std::shared_ptr<PlayerFirePacket> p) {
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
		backend->broadcast(p, peersession); //except the host
	}
	void OnPacket(std::shared_ptr<PlayerHitPacket> p) {
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
		backend->broadcast(p, peersession); //except the host
	}
	void OnPacket(std::shared_ptr<PlayerKilledPacket> p) {
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
		backend->broadcast(p, peersession); //except the host
	}
	void OnPacket(std::shared_ptr<ServerQueryReqPacket> p) {
		gLogi("ServerPacketHandler") << "<<< SERVER RECEIVED PING! Sending response...";
		auto res = std::make_shared<ServerQueryResPacket>();
		res->lobbyName = backend->serverName;

		uint32_t teamSize = NetworkManager::getInstance()->getLobbyTeamSize();
		res->format = std::to_string(teamSize) + "v" + std::to_string(teamSize);
		res->sizeStr = std::to_string(backend->playerCount()) + "/" + std::to_string(teamSize * 2);
		res->isDedicated = backend->isDedicatedServer;

		if (peersession->SendPacket(res) != znet::Result::Success) {
			gLogw("GameBackendLocal") << "Failed to send the query response";
		}
	}
	void OnPacket(std::shared_ptr<LobbyJoinPacket> p) {
        if (backend->isPrivateServer && backend->serverPassword != p->password) {
            // Disconnect the player if the password is incorrect.
            auto kick = std::make_shared<LobbyKickPacket>();
            kick->reason = "PASSWORD_REQUIRED";
            peersession->SendPacket(kick);
            return;
        }
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
		// Tag the session with the sender's ID so we know who they are when they disconnect
		peersession->SetUserPointer(std::make_shared<uint32_t>(p->senderId));
		backend->syncVoicePeerStates();
	}
	void OnPacket(std::shared_ptr<ToggleReadyPacket> p) {
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
	}
	void OnPacket(std::shared_ptr<SwitchTeamPacket> p) {
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
	}
	void OnPacket(std::shared_ptr<StartMatchPacket> p) {
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
	}
	
	void OnPacket(std::shared_ptr<KeepAlivePacket> p) {
		backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));
	}

	void OnPacket(std::shared_ptr<PingPacket> p) {
		auto pong = std::make_shared<PongPacket>();
		pong->timestamp = p->timestamp;
		peersession->SendPacket(pong);
	}

	void OnPacket(std::shared_ptr<PongPacket> p) {
		backend->onPongReceived(p->timestamp);
	}

	// Voice Uplink from remote client
	void OnPacket(std::shared_ptr<gTeamVoiceUplinkPacket> p) {
		backend->handleVoiceUplinkPacket(peersession->id(), *p);
	}

	void OnUnknown(std::shared_ptr<znet::Packet>) {}

private:
	GameBackendLocal* backend;
	znet::PeerSession* peersession;
};

// Creates a Codec that knows how to serialize/deserialize our packet types.
// Each session gets its own Codec instance so the library knows how to
// encode outgoing packets and decode incoming ones.
static std::shared_ptr<znet::Codec> makeCodec() {
	auto codec = std::make_shared<znet::Codec>();
	codec->Add(PACKET_NODE_STATE, std::make_unique<NodeStateSerializer>());
	codec->Add(PACKET_NODE_LEAVE, std::make_unique<NodeLeaveSerializer>());

	codec->Add(PACKET_NODE_FIRE, std::make_unique<PlayerFireSerializer>());
	codec->Add(PACKET_NODE_HIT, std::make_unique<PlayerHitSerializer>());
	codec->Add(PACKET_NODE_KILLED, std::make_unique<PlayerKilledSerializer>());

	codec->Add(PACKET_SERVER_QUERY_REQ, std::make_unique<ServerQueryReqSerializer>());
	codec->Add(PACKET_SERVER_QUERY_RES, std::make_unique<ServerQueryResSerializer>());
	codec->Add(PACKET_LOBBY_JOIN, std::make_unique<LobbyJoinSerializer>());
	codec->Add(PACKET_LOBBY_STATE, std::make_unique<LobbyStateSerializer>());
	codec->Add(PACKET_TOGGLE_READY, std::make_unique<ToggleReadySerializer>());
	codec->Add(PACKET_SWITCH_TEAM, std::make_unique<SwitchTeamSerializer>());
	codec->Add(PACKET_START_MATCH, std::make_unique<StartMatchSerializer>());
	codec->Add(PACKET_LOBBY_KICK, std::make_unique<LobbyKickSerializer>());
	codec->Add(PACKET_KEEPALIVE, std::make_unique<KeepAliveSerializer>());
	codec->Add(PACKET_PING, std::make_unique<PingSerializer>());
	codec->Add(PACKET_PONG, std::make_unique<PongSerializer>());

	// Voice Packets
	codec->Add(G_TEAM_VOICE_SESSION_PACKET_ID, std::make_unique<gTeamVoiceSessionSerializer>());
	codec->Add(G_TEAM_VOICE_UPLINK_PACKET_ID, std::make_unique<gTeamVoiceUplinkSerializer>());
	codec->Add(G_TEAM_VOICE_DOWNLINK_PACKET_ID, std::make_unique<gTeamVoiceDownlinkSerializer>());

	return codec;
}

GameBackendLocal::GameBackendLocal(const std::string& bindIp, uint16_t port)
	: bindip(bindIp), port(port) {
	static std::atomic<uint64_t> s_voiceSessionSeq{1000};
	voiceSessionId = s_voiceSessionSeq.fetch_add(1);
}

GameBackendLocal::~GameBackendLocal() {
	if (punchHost) {
		punchHost->Stop();
	}
	punchHost.reset();
	if (server) {
		server->Stop();
	}
	server.reset();
	if (queryServer) {
		queryServer->Stop();
	}
	queryServer.reset();
	if (masterClient) {
		masterClient->Disconnect();
	}
	masterClient.reset();

	{
		std::lock_guard<std::mutex> lk(sessionsmutex);
		for (auto& s : sessions) {
			if (s) s->Close();
		}
		sessions.clear();
	}
	voiceRouter.reset();
	if (!isDedicatedServer) {
		voiceClient.shutdown();
	}
}

void GameBackendLocal::start() {
	server = std::make_unique<znet::Server>(znet::ServerConfig{bindip, port, std::chrono::seconds(10), znet::ConnectionType::ZDT});
	// znet fires events on a background network thread.
	// We dispatch them to our member functions using ZNET_BIND_FN.
	server->SetEventCallback([this](znet::Event& ev) {
	    znet::EventDispatcher d{ev};
	    d.Dispatch<znet::IncomingClientConnectedEvent>(ZNET_BIND_FN(onPeerConnected));
	    d.Dispatch<znet::IncomingClientDisconnectedEvent>(ZNET_BIND_FN(onPeerDisconnected));
	});

	server->Bind();
	server->Listen(); // Non-blocking, starts accepting connections in the background
	
	queryServer = std::make_unique<znet::Server>(znet::ServerConfig{bindip, static_cast<uint16_t>(port + 1), std::chrono::seconds(10), znet::ConnectionType::TCP});
	// Create dedicated TCP server for queries to bypass Hamachi UDP issues
	queryServer->SetEventCallback([this](znet::Event& ev) {
	    znet::EventDispatcher d{ev};
	    d.Dispatch<znet::IncomingClientConnectedEvent>([this](znet::IncomingClientConnectedEvent& e) {
	        auto sess = e.session();
	        sess->SetCodec(makeCodec());
	        sess->SetHandler(std::make_shared<ServerPacketHandler>(this, sess.get()));
	        return false;
	    });
	});
	queryServer->Bind();
	queryServer->Listen();

	if (!isDedicatedServer) {
		initializeVoice();
		voiceRouter.addPeer(LOCAL_HOST_VOICE_CONN_ID, [this](const std::shared_ptr<znet::Packet>& p, const znet::SendOptions&) {
			if (auto down = std::dynamic_pointer_cast<gTeamVoiceDownlinkPacket>(p)) {
				voiceClient.handleVoicePacket(*down);
			} else if (auto sess = std::dynamic_pointer_cast<gTeamVoiceSessionPacket>(p)) {
				voiceClient.handleSessionPacket(*sess);
			}
			return true;
		});
		syncVoicePeerStates();
	}

	notifyConnected();
}

znet::p2p::Host* GameBackendLocal::ensurePunchHost() {
	if (punchHost) return punchHost.get();

	znet::p2p::Host::Config config;
	config.bind_address = "0.0.0.0";
	config.bind_port = advertisedPort();

	auto host = std::make_unique<znet::p2p::Host>(config);
	if (host->Start() != znet::Result::Success) {
		gLogw("GameBackendLocal") << "[Host] Could not open the punch socket on port " << config.bind_port;
		return nullptr;
	}
	punchHost = std::move(host);
	gLogi("GameBackendLocal") << "[Host] Punch socket open on port " << punchHost->punch_port();
	return punchHost.get();
}

void GameBackendLocal::gatherCandidates() {
	auto* host = ensurePunchHost();
	if (!host) return;
	std::vector<std::shared_ptr<znet::InetAddress>> reflectors;
	std::shared_ptr<znet::InetAddress> reflector = znet::InetAddress::from(targetMasterIp, targetMasterRelayPort);
	if (reflector && reflector->is_valid()) reflectors.push_back(reflector);
	host->Gather(reflectors, std::chrono::seconds(2), [this](znet::Result result, std::vector<znet::p2p::Candidate> candidates) {
		if (result != znet::Result::Success) {
			gLogw("GameBackendLocal") << "[Host] Gather: " << znet::GetResultString(result) << ", registering the local addresses";
		}
		{
			std::lock_guard<std::mutex> lk(gatherMutex);
			gatheredCandidates = std::move(candidates);
		}
		gLogi("GameBackendLocal") << "[Host] Gathered " << gatheredCandidates.size() << " candidates";
		// the register that went out on connect had none of these
		auto session = masterClient ? masterClient->client_session() : nullptr;
		if (isConnectedToMaster && session) session->SendPacket(makeRegisterPacket());
	});
}

std::shared_ptr<znet::InetAddress> GameBackendLocal::atMaster(const std::shared_ptr<znet::InetAddress>& address) const {
	if (!address || !znet::p2p::IsUnspecifiedHost(*address)) return address;
	return znet::InetAddress::from(targetMasterIp, address->port());
}

void GameBackendLocal::onPunchResolved(znet::Result result, std::shared_ptr<znet::PeerSession> sess) {
	if (result != znet::Result::Success || !sess) {
		gLogw("GameBackendLocal") << "[Host] Punch failed: " << znet::GetResultString(result);
		return;
	}
	adoptSession(sess);
	gLogi("GameBackendLocal") << "[Host] Punch successful via " << sess->remote_address()->readable();
}

uint16_t GameBackendLocal::advertisedPort() const {
	// Direct joins use the match port; punches use their own socket.
	if (isDedicatedServer && !useP2P) return port;
	return static_cast<uint16_t>(port + 2);
}

std::string GameBackendLocal::advertisedAddress() const {
	return publicIp + ":" + std::to_string(advertisedPort());
}

std::vector<std::string> GameBackendLocal::localAddresses() const {
	const std::string suffix = ":" + std::to_string(advertisedPort());
	std::vector<std::string> out;
	for (const auto& local : znet::GetLocalAddresses(znet::InetProtocolVersion::IPv4)) {
		out.push_back(local + suffix);
	}
	return out;
}

std::string GameBackendLocal::roomCode() const {
    std::lock_guard<std::mutex> lk(roomCodeMutex);
    return assignedRoomCode;
}

std::shared_ptr<gMasterRegisterPacket> GameBackendLocal::makeRegisterPacket() const {
    auto reg = std::make_shared<gMasterRegisterPacket>();
    reg->ip = advertisedAddress();
    reg->localIps = localAddresses();
    reg->name = serverName;
    reg->currentPlayers = playerCount();
    reg->maxPlayers = NetworkManager::getInstance()->getLobbyTeamSize() * 2;
    reg->matchState = 0;
    reg->isPrivate = isPrivateServer;
    reg->hasPassword = !serverPassword.empty();
    reg->isDedicated = isDedicatedServer;
    reg->useP2P = useP2P;
    {
        std::lock_guard<std::mutex> lk(gatherMutex);
        reg->candidates = gatheredCandidates;
    }
    return reg;
}

void GameBackendLocal::registerWithMasterServer(const std::string& name, bool isPrivate, const std::string& password, const std::string& masterIp, uint16_t masterPort, uint16_t masterRelayPort, const std::string& pubIp, bool useP2P) {
    this->serverName = name;
    this->isPrivateServer = isPrivate;
    this->serverPassword = password;
    this->targetMasterIp = masterIp;
    this->targetMasterPort = masterPort;
    this->targetMasterRelayPort = masterRelayPort;
    this->publicIp = pubIp;
    this->useP2P = useP2P;

    // Only a host that punches has candidates to gather; the register that
    // goes out on connect carries whatever is in by then, and the gather
    // callback sends another once the rest arrives.
    if (useP2P) gatherCandidates();

    masterClient = std::make_unique<znet::Client>(znet::ClientConfig{targetMasterIp, targetMasterPort, std::chrono::seconds(5), znet::ConnectionType::TCP});
    
    masterClient->SetEventCallback([this, useP2P](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([this, useP2P](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            
            auto codec = std::make_shared<znet::Codec>();
            codec->Add(PACKET_GIP_MASTER_REGISTER, std::make_unique<gMasterRegisterSerializer>());
            codec->Add(PACKET_GIP_MASTER_HEARTBEAT, std::make_unique<gMasterHeartbeatSerializer>());
            codec->Add(PACKET_GIP_MASTER_REGISTER_RES, std::make_unique<gMasterRegisterResponseSerializer>());
            codec->Add(PACKET_GIP_MASTER_PUNCH_EXEC, std::make_unique<gMasterPunchExecuteSerializer>());
            
            sess->SetCodec(codec);
            
            class MasterHandler : public znet::PacketHandler<MasterHandler, gMasterRegisterResponsePacket, gMasterPunchExecutePacket> {
            public:
                MasterHandler(GameBackendLocal* be) : backend(be) {}
                void OnPacket(std::shared_ptr<gMasterRegisterResponsePacket> p) {
                    gLogi("GameBackendLocal") << "[Host] Master assigned Room Code: " << p->roomCode;
                    {
                        std::lock_guard<std::mutex> lk(backend->roomCodeMutex);
                        backend->assignedRoomCode = p->roomCode;
                    }
                    // Both the singleton and the lobby broadcast are main thread only.
                    GameBackendLocal* b = backend;
                    std::string code = p->roomCode;
                    backend->runOnMainThread([b, code]() {
                        NetworkManager::getInstance()->currentRoomCode = code;
                        b->broadcastLobbyState();
                    });
                }
                void OnPacket(std::shared_ptr<gMasterPunchExecutePacket> p) {
                    gLogi("GameBackendLocal") << "[Host] Master requested punch to " << p->candidates.size() << " candidates.";

                    auto* host = backend->ensurePunchHost();
                    if (!host) return;

                    znet::p2p::PunchOffer offer;
                    for (znet::p2p::Candidate candidate : p->candidates) {
                        candidate.address = backend->atMaster(candidate.address);
                        if (candidate.address && candidate.address->is_valid()) offer.candidates.push_back(std::move(candidate));
                    }
                    if (offer.candidates.empty()) return;
                    // the host accepts, so its options decide encryption and compression
                    offer.is_initiator = !p->isHost;
                    offer.timeout = std::chrono::seconds(10);

                    // Asynchronous, so no thread of our own and nothing blocked here.
                    GameBackendLocal* b = backend;
                    host->Punch(std::move(offer),
                        [b](znet::Result result, std::shared_ptr<znet::PeerSession> sess) {
                            b->onPunchResolved(result, std::move(sess));
                        });
                }
                void OnUnknown(std::shared_ptr<znet::Packet>) {}
            private:
                GameBackendLocal* backend;
            };
            sess->SetHandler(std::make_shared<MasterHandler>(this));
            
            isConnectedToMaster = true;

            gLogi("GameBackendLocal") << "Registering with the master server as " << advertisedAddress()
                                      << " (dedicated: " << isDedicatedServer << ", p2p: " << useP2P << ")";
            sess->SendPacket(makeRegisterPacket());
            return false;
        });
        d.Dispatch<znet::ClientDisconnectedFromServerEvent>([this](znet::ClientDisconnectedFromServerEvent& e) {
            isConnectedToMaster = false;
            return false;
        });
    });

    masterClient->Bind();
    masterClient->Connect();
}

void GameBackendLocal::update(float deltaTime) {
    GameBackend::update(deltaTime);

    std::vector<uint32_t> deadIds;
    {
        std::lock_guard<std::mutex> lk(sessionsmutex);
        for (auto it = sessions.begin(); it != sessions.end(); ) {
            if (*it && !(*it)->IsAlive()) {
                auto idptr = (*it)->template user_pointer<uint32_t>();
                if (idptr && *idptr != 0) {
                    deadIds.push_back(*idptr);
                    *idptr = 0; // Clear the ID so we don't send duplicate leave messages later
                }
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (uint32_t deadId : deadIds) {
        auto lp = std::make_shared<NodeLeavePacket>();
        lp->netid = deadId;
        enqueuePacket(std::static_pointer_cast<znet::Packet>(lp));
        broadcast(lp);
    }

    if (!isDedicatedServer) {
        voiceClient.updateNetwork([this](const gTeamVoiceUplinkPacket& packet) {
            voiceRouter.handleVoicePacket(LOCAL_HOST_VOICE_CONN_ID, packet);
            return true;
        });
    }

    masterHeartbeatTimer += deltaTime;
    if (masterHeartbeatTimer >= 10.0f) {
        masterHeartbeatTimer = 0.0f;
        auto session = masterClient ? masterClient->client_session() : nullptr;
        if (isConnectedToMaster && session) {
            auto hb = std::make_shared<gMasterHeartbeatPacket>();
            hb->ip = advertisedAddress();
            session->SendPacket(hb);
            // The register doubles as the update: player counts change.
            session->SendPacket(makeRegisterPacket());
        }
    }
}

void GameBackendLocal::sendPacket(std::shared_ptr<znet::Packet> packet) {
	broadcast(packet);
}

// Sends a packet to all connected clients, optionally excluding one (the sender).
void GameBackendLocal::broadcast(const std::shared_ptr<znet::Packet>& packet, znet::PeerSession* exclude) {
	std::lock_guard<std::mutex> lk(sessionsmutex);
	for (auto& s : sessions) {
		if (s && s.get() != exclude) s->SendPacket(packet);
	}
}

// Called on the network thread when a new client connects.
// Sets up the session with our codec and packet handler, then adds it to the list.
void GameBackendLocal::adoptSession(const std::shared_ptr<znet::PeerSession>& sess) {
	if (!sess) return;
	sess->SetCodec(makeCodec());
	sess->SetHandler(std::make_shared<ServerPacketHandler>(this, sess.get()));
	{
		std::lock_guard<std::mutex> lk(sessionsmutex);
		sessions.push_back(sess);
	}
	voiceRouter.addPeer(sess);
	syncVoicePeerStates();
}

bool GameBackendLocal::onPeerConnected(znet::IncomingClientConnectedEvent& e) {
	adoptSession(e.session());
	return false;
}

// Called on the network thread when a client disconnects.
// Retrieves the node ID we stored on the session, removes the session,
// and notifies both the host backend and remaining clients.
bool GameBackendLocal::onPeerDisconnected(znet::IncomingClientDisconnectedEvent& e) {
	// Retrieve the node ID we tagged this session with in ServerPacketHandler
	uint32_t leavingid = 0;
	if (e.session()->template user_pointer<uint32_t>()) {
		auto idptr = e.session()->template user_pointer<uint32_t>();
		if (idptr) leavingid = *idptr;
	}

	voiceRouter.removePeer(e.session()->id());

	// Remove the disconnected session from our list
	{
		std::lock_guard<std::mutex> lk(sessionsmutex);
		sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
			[&](const std::shared_ptr<znet::PeerSession>& s) { return s.get() == e.session().get(); }),
			sessions.end());
	}

	if (leavingid != 0) {
		// Queue a leave packet for the host's game loop
		auto lp = std::make_shared<NodeLeavePacket>();
		lp->netid = leavingid;
		enqueuePacket(std::static_pointer_cast<znet::Packet>(lp));
		// Tell all remaining clients to remove this node
		broadcast(lp);
	}
	return false;
}

void GameBackendLocal::kickPlayer(uint32_t playerId) {
	{
		std::lock_guard<std::mutex> lk(sessionsmutex);
		for (auto& s : sessions) {
			auto idptr = s->template user_pointer<uint32_t>();
			if (idptr && *idptr == playerId) {
				s->SendPacket(std::make_shared<LobbyKickPacket>());
				break;
			}
		}
	}

	// Instantly remove the player from the host's room list and broadcast the new state
	auto it = std::remove_if(roomPlayers.begin(), roomPlayers.end(), [playerId](const auto& p) { return p.id == playerId; });
	if (it != roomPlayers.end()) {
		roomPlayers.erase(it, roomPlayers.end());
		publishPlayerCount();
		broadcastLobbyState();
	}
}

// Called by GameBackend::update() for each local node every frame.
// Sends the node's current position to all connected clients.
void GameBackendLocal::broadcastState(uint32_t netid, float x, float y, float z, float yaw, uint8_t team, uint8_t animState) {
	auto p = std::make_shared<NodeStatePacket>();
	p->netid = netid;
	p->x = x;
	p->y = y;
	p->z = z;
	p->yaw = yaw;
	p->team = team;
	p->animState = animState;
	broadcast(p);
}

void GameBackendLocal::broadcastFireEvent(uint32_t shooterId, uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz) {
	auto p = std::make_shared<PlayerFirePacket>();
	p->shooterId = shooterId; p->gunType = gunType;
	p->originX = ox; p->originY = oy; p->originZ = oz;
	p->dirX = dx; p->dirY = dy; p->dirZ = dz;
	broadcast(p);
}

void GameBackendLocal::broadcastHitEvent(uint32_t attackerId, uint32_t victimId, float damage) {
	auto p = std::make_shared<PlayerHitPacket>();
	p->attackerId = attackerId;
	p->victimId = victimId;
	p->damage = damage;
	broadcast(p);
}

void GameBackendLocal::broadcastKillEvent(uint32_t killerId, uint32_t victimId) {
	auto p = std::make_shared<PlayerKilledPacket>();
	p->killerId = killerId;
	p->victimId = victimId;
	broadcast(p);
}

void GameBackendLocal::broadcastLobbyState() {
	auto p = std::make_shared<LobbyStatePacket>();
	p->isGlobalServer = this->isDedicatedServer;
	p->roomCode = roomCode();
	for (auto& rp : roomPlayers) {
		p->playerIds.push_back(rp.id);
		p->playerNames.push_back(rp.name);
		p->playerTeams.push_back(rp.team);
		p->playerReadys.push_back(rp.isReady ? 1 : 0);
	}
	syncVoicePeerStates();
	broadcast(p); // broadcasts to clients
	if (onLobbyStateUpdated) onLobbyStateUpdated(p); // update local host UI
}

void GameBackendLocal::syncVoicePeerStates() {
	if (voiceSessionId == 0) return;
	if (!isDedicatedServer) {
		uint32_t hostId = 1;
		std::string hostName = NetworkManager::getInstance()->loggedInUsername();
		for (const auto& rp : roomPlayers) {
			if (!hostName.empty() && rp.name == hostName) {
				hostId = rp.id;
				break;
			}
		}
		voiceRouter.setPeerState(LOCAL_HOST_VOICE_CONN_ID, {hostId, static_cast<uint64_t>(localTeam), voiceSessionId, true, true});
	}

	std::lock_guard<std::mutex> lk(sessionsmutex);
	for (auto& s : sessions) {
		if (!s || !s->IsAlive()) continue;
		uint32_t pid = static_cast<uint32_t>(s->id());
		auto idptr = s->template user_pointer<uint32_t>();
		if (idptr && *idptr != 0) {
			pid = *idptr;
		}
		uint8_t team = 0;
		for (const auto& rp : roomPlayers) {
			if (rp.id == pid || rp.id == s->id()) {
				team = rp.team;
				pid = rp.id;
				break;
			}
		}
		voiceRouter.setPeerState(s->id(), {pid, static_cast<uint64_t>(team), voiceSessionId, true, true});
	}
}

void GameBackendLocal::handleVoiceUplinkPacket(gTeamVoiceServer::ConnectionId connId, const gTeamVoiceUplinkPacket& p) {
	voiceRouter.handleVoicePacket(connId, p);
}

void GameBackendLocal::handleVoiceSessionPacket(const gTeamVoiceSessionPacket& p) {
	if (!isDedicatedServer) {
		voiceClient.handleSessionPacket(p);
	}
}

void GameBackendLocal::handleVoiceDownlinkPacket(const gTeamVoiceDownlinkPacket& p) {
	if (!isDedicatedServer) {
		voiceClient.handleVoicePacket(p);
	}
}

bool GameBackendLocal::initializeVoice() {
	if (isDedicatedServer) return true;
	return voiceClient.initialize();
}

void GameBackendLocal::shutdownVoice() {
	if (!isDedicatedServer) {
		voiceClient.shutdown();
	}
	voiceRouter.reset();
}

void GameBackendLocal::startVoiceTransmission() {
	if (isDedicatedServer) return;
	if (!voiceClient.isInitialized()) {
		initializeVoice();
	}
	voiceClient.startTransmitting();
}

void GameBackendLocal::stopVoiceTransmission() {
	if (!isDedicatedServer) {
		voiceClient.stopTransmitting();
	}
}

bool GameBackendLocal::isVoiceTransmitting() const {
	if (isDedicatedServer) return false;
	return voiceClient.isTransmitting();
}

bool GameBackendLocal::isPlayerTalking(uint32_t playerId) const {
	if (isDedicatedServer) return false;
	auto stats = voiceClient.getSpeakerStats();
	for (const auto& s : stats) {
		if (s.speakerid == playerId && s.jitterdepth > 0) {
			return true;
		}
	}
	return false;
}

void GameBackendLocal::setSpeakerMuted(uint32_t playerId, bool muted) {
	if (!isDedicatedServer) {
		voiceClient.setSpeakerMuted(playerId, muted);
	}
}

void GameBackendLocal::setSpeakerVolume(uint32_t playerId, float volume) {
	if (!isDedicatedServer) {
		voiceClient.setSpeakerVolume(playerId, volume);
	}
}

void GameBackendLocal::setVoiceEnabled(bool enabled) {
	if (!isDedicatedServer) {
		voiceClient.setEnabled(enabled);
	}
}

bool GameBackendLocal::isVoiceEnabled() const {
	if (isDedicatedServer) return true;
	return voiceClient.isEnabled();
}

void GameBackendLocal::setHearEnemiesVoice(bool hear) {
	voiceRouter.setHearEnemiesVoice(hear);
}

bool GameBackendLocal::canHearEnemiesVoice() const {
	return voiceRouter.canHearEnemiesVoice();
}
