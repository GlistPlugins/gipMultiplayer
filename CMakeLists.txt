cmake_minimum_required (VERSION 3.10.2)

##### PLUGIN NAME #####
set(pluginname gipMultiplayer)

##### PLUGIN PATHS #####
set(PLUGIN_DIR ${PLUGINS_DIR}/${pluginname})

##### PLUGIN HEADERS #####
list(APPEND PLUGIN_INCLUDES
		${PLUGIN_DIR}/src
)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

##### ZNET #####
# Fetched at configure time, see external/znet.cmake for the pinned versions.
include(${PLUGIN_DIR}/external/znet.cmake)

list(APPEND PLUGIN_LINKLIBS
		znet
)
