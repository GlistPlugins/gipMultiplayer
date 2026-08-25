/*
 * gipMultiplayer.h
 *
 * High-level GlistEngine Multiplayer Plugin Interface.
 * Completely encapsulates underlying znet transport and provides generic
 * matchmaking, P2P NAT hole punching, and 20 Hz state replication.
 */

#ifndef GIPMULTIPLAYER_H
#define GIPMULTIPLAYER_H

#include "gBasePlugin.h"
#include "GamePackets.h"
#include "GameBackend.h"
#include "GameBackendLocal.h"
#include "GameBackendRemote.h"
#include "GameBackendServer.h"
#include "NetworkManager.h"
#include "NetworkSynchronizer.h"
#include "master/gServerBrowser.h"

#include "voice/gTeamVoicePackets.h"
#include "voice/gTeamVoiceServer.h"
#include "audio/gTeamVoice.h"

class gipMultiplayer : public gBasePlugin {
public:
	gipMultiplayer() = default;
	virtual ~gipMultiplayer() = default;

	static NetworkManager* getNetworkManager() { return NetworkManager::getInstance(); }
	static NetworkSynchronizer* getSynchronizer() { return NetworkSynchronizer::getInstance(); }
};

#endif // GIPMULTIPLAYER_H
