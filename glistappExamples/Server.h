#pragma once

#include <memory>
#include <string>
#include "znet/znet.h"

std::unique_ptr<znet::Server> createGameServer(const std::string& bindIp, uint16_t port);
