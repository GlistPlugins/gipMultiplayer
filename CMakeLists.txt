cmake_minimum_required (VERSION 3.10.2)

##### PLUGIN NAME #####
set(pluginname gipEmpty)


##### PLUGIN PATHS #####
set(PLUGIN_DIR ${PLUGINS_DIR}/${pluginname})
set(PREBUILT_DIR ${PLUGIN_DIR}/prebuilts)


##### PLUGIN SOURCES #####
list(APPEND PLUGIN_SRCS
			${PLUGIN_DIR}/src/gipEmpty.cpp
			${PLUGIN_DIR}/photon/src/Chat-cpp/Channel.cpp
			${PLUGIN_DIR}/photon/src/Chat-cpp/Client.cpp
			${PLUGIN_DIR}/photon/src/Chat-cpp/Peer.cpp
			${PLUGIN_DIR}/photon/src/Chat-cpp/AuthenticationValues.cpp
			${PLUGIN_DIR}/photon/src/Chat-cpp/Internal/AuthenticationValuesSecretSetter.cpp
			${PLUGIN_DIR}/photon/src/Chat-cpp/Internal/ChannelFactory.cpp
			${PLUGIN_DIR}/photon/src/Chat-cpp/Internal/ChannelMessageAdder.cpp
			${PLUGIN_DIR}/photon/src/Chat-cpp/Internal/ChannelPropertiesSetter.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/AuthenticationValues.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Client.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/FriendInfo.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/LobbyStatsRequest.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/LobbyStatsResponse.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/MutablePlayer.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/MutableRoom.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Peer.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Player.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/RaiseEventOptions.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Room.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/RoomOptions.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/WebFlags.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/AuthenticationValuesSecretSetter.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/MutablePlayerFactory.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/MutableRoomFactory.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/PlayerFactory.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/PlayerMovementInformant.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/PlayerMutableRoomPointerSetter.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/PlayerPropertiesCacher.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/PlayerPropertiesUpdateInformant.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/PuncherClient.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/RoomFactory.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/RoomPropertiesCacher.cpp
			${PLUGIN_DIR}/photon/src/LoadBalancing-cpp/Internal/Utils.cpp
)


##### PLUGIN HEADERS #####
list(APPEND PLUGIN_INCLUDES
			${PLUGIN_DIR}/src
			${PLUGIN_DIR}/photon/include
)


##### PLUGIN DEPENDENCY LOCATIONS (IF ANY) #####
if(WIN32)
	list(APPEND PLUGIN_LINKDIRS
				${PREBUILT_DIR}/lib/win32
				${PREBUILT_DIR}/bin
	)
	
	#set(PLUGIN_LIBDIRS ${PREBUILT_DIR}/lib/win32)
	list(APPEND PLUGIN_LIBDIRS
				${PREBUILT_DIR}/lib/win32
	)
elseif(LINUX)
	#Linux libs here
elseif(APPLE)
	#Macos libs here
endif(WIN32)


##### PLUGIN DEPENDENCY LINKING (IF ANY) #####					
if(WIN32)
	list(APPEND PLUGIN_LINKLIBS
				#Lib names
	)
elseif(LINUX)
	list(APPEND PLUGIN_LINKLIBS
				#Lib names
	)
elseif(APPLE)
	list(APPEND PLUGIN_LINKLIBS
				#Lib names
	)
endif(WIN32)