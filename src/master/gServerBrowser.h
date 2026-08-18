#pragma once
#include "gMasterPackets.h"
#include "znet/client.h"
#include <functional>
#include <vector>
#include <memory>
#include <string>

class gServerBrowser {
public:
    gServerBrowser();
    
    // Connects to master server and asks for a list
    void refreshServers(const std::string& masterIp, uint16_t masterPort, int matchStateFilter = -1);
    
    // Set callback triggered when the list arrives
    void setOnServersReceived(std::function<void(const std::vector<gServerInfo>&)> callback);

private:
    std::shared_ptr<znet::Client> masterQueryClient;
    std::function<void(const std::vector<gServerInfo>&)> onServersReceived;
};
