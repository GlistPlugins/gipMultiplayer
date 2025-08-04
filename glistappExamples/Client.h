#pragma once

#include <memory>
#include <functional>
#include <string>
#include "znet/znet.h"

std::unique_ptr<znet::Client> createGameClient(const std::string& serverIp, uint16_t port);
void setRemoteStateCallback(const std::function<void(uint32_t,float,float,bool)>& cb);
void setPlayerDisconnectCallback(const std::function<void(uint32_t)>& cb);
bool sendLocalState(uint32_t id, float x, float y, bool isGreen);