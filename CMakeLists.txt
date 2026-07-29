cmake_minimum_required (VERSION 3.10.2)

##### PLUGIN NAME #####
set(pluginname gipMultiplayer)

##### PLUGIN PATHS #####
set(PLUGIN_DIR ${PLUGINS_DIR}/${pluginname})

##### GIPOPUS #####
# Existing projects may still list gipOpus explicitly. Otherwise fetch it with
# its libopus submodule at configure time.
list(FIND PLUGINS gipOpus GIPOPUS_PLUGIN_INDEX)
if(GIPOPUS_PLUGIN_INDEX EQUAL -1)
	include(${PLUGIN_DIR}/external/gipopus.cmake)
endif()
unset(GIPOPUS_PLUGIN_INDEX)

##### PLUGIN SOURCES #####
list(APPEND PLUGIN_SRCS
		${PLUGIN_DIR}/src/audio/gVoiceLoopback.cpp
)

##### PLUGIN HEADERS #####
list(APPEND PLUGIN_INCLUDES
		${PLUGIN_DIR}/src
)

##### ZNET #####
# Fetched at configure time, see external/znet.cmake for the pinned versions.
include(${PLUGIN_DIR}/external/znet.cmake)

list(APPEND PLUGIN_LINKLIBS
		znet
)
