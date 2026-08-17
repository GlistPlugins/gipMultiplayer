#include "gServerBrowser.h"
#include "znet/client_events.h"

class gBrowserPacketHandler : public znet::PacketHandler<gBrowserPacketHandler, gMasterSendListPacket> {
public:
    gBrowserPacketHandler(std::function<void(const std::vector<gServerInfo>&)> cb) : cb(cb) {}

    void OnPacket(std::shared_ptr<gMasterSendListPacket> p) {
        if (cb) cb(p->servers);
    }
    void OnUnknown(std::shared_ptr<znet::Packet>) {}
private:
    std::function<void(const std::vector<gServerInfo>&)> cb;
};

gServerBrowser::gServerBrowser() {}

void gServerBrowser::setOnServersReceived(std::function<void(const std::vector<gServerInfo>&)> callback) {
    onServersReceived = callback;
}

void gServerBrowser::refreshServers(const std::string& masterIp, uint16_t masterPort, int matchStateFilter) {
    masterQueryClient = std::make_shared<znet::Client>(znet::ClientConfig{masterIp, masterPort, std::chrono::seconds(2), znet::ConnectionType::TCP});
    
    masterQueryClient->SetEventCallback([this, matchStateFilter](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([this, matchStateFilter](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            
            auto codec = std::make_shared<znet::Codec>();
            codec->Add(PACKET_GIP_MASTER_GET_LIST, std::make_unique<gMasterGetListSerializer>());
            codec->Add(PACKET_GIP_MASTER_SEND_LIST, std::make_unique<gMasterSendListSerializer>());
            
            sess->SetCodec(codec);
            sess->SetHandler(std::make_shared<gBrowserPacketHandler>(onServersReceived));
            
            auto req = std::make_shared<gMasterGetListPacket>();
            req->matchStateFilter = matchStateFilter;
            sess->SendPacket(req);
            return false;
        });
    });
    
    masterQueryClient->Bind();
    masterQueryClient->Connect();
}
