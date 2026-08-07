#include "gDedicatedServer.h"
#include "gMasterPackets.h"
#include "znet/client_events.h"

gDedicatedServer::gDedicatedServer(const std::string& bindIp, uint16_t port) 
    : GameBackendLocal(bindIp, port) {}

void gDedicatedServer::enableMasterServerRegistration(const std::string& mIp, uint16_t mPort, const std::string& sName, uint32_t maxP) {
    useMasterServer = true;
    masterIp = mIp;
    masterPort = mPort;
    serverName = sName;
    maxPlayers = maxP;
    
    masterClient = std::make_shared<znet::Client>(znet::ClientConfig{masterIp, masterPort, std::chrono::seconds(2), znet::ConnectionType::TCP});
    
    masterClient->SetEventCallback([this](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([this](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            
            auto codec = std::make_shared<znet::Codec>();
            codec->Add(PACKET_GIP_MASTER_REGISTER, std::make_unique<gMasterRegisterSerializer>());
            codec->Add(PACKET_GIP_MASTER_HEARTBEAT, std::make_unique<gMasterHeartbeatSerializer>());
            sess->SetCodec(codec);
            
            auto reg = std::make_shared<gMasterRegisterPacket>();
            reg->ip = this->bindip + ":" + std::to_string(this->port);
            reg->name = this->serverName;
            
            std::lock_guard<std::mutex> lk(this->sessionsmutex);
            reg->currentPlayers = this->sessions.size();
            reg->maxPlayers = this->maxPlayers;
            
            sess->SendPacket(reg);
            return false;
        });
    });
    
    masterClient->Bind();
    masterClient->Connect();
}

void gDedicatedServer::tickMasterServer(float deltaTime) {
    if (useMasterServer) {
        heartbeatTimer += deltaTime;
        if (heartbeatTimer >= 10.0f) {
            heartbeatTimer = 0.0f;
            
            if (masterClient && masterClient->client_session()) {
                auto hb = std::make_shared<gMasterHeartbeatPacket>();
                hb->ip = this->bindip + ":" + std::to_string(this->port);
                masterClient->client_session()->SendPacket(hb);
                
                auto reg = std::make_shared<gMasterRegisterPacket>();
                reg->ip = this->bindip + ":" + std::to_string(this->port);
                reg->name = this->serverName;
                
                std::lock_guard<std::mutex> lk(this->sessionsmutex);
                reg->currentPlayers = this->sessions.size();
                reg->maxPlayers = this->maxPlayers;
                masterClient->client_session()->SendPacket(reg);
            } else if (masterClient) {
                // Try reconnecting if disconnected
                masterClient->Connect();
            }
        }
    }
}
