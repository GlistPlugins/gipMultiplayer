cmake_minimum_required (VERSION 3.10.2)

##### PLUGIN NAME #####
set(pluginname gipMultiplayer)

##### PLUGIN PATHS #####
set(PLUGIN_DIR ${PLUGINS_DIR}/${pluginname})

##### PLUGIN SOURCES #####
list(APPEND PLUGIN_SRCS
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
