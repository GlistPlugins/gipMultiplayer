cmake_minimum_required (VERSION 3.10.2)

##### PLUGIN NAME #####
set(pluginname gipNetcode)

##### PLUGIN PATHS #####
set(PLUGIN_DIR ${PLUGINS_DIR}/${pluginname})

##### PLUGIN SOURCES #####
list(APPEND PLUGIN_SRCS
		${PLUGIN_DIR}/src/gipEmptyPlugin.cpp
)

##### PLUGIN HEADERS #####
list(APPEND PLUGIN_INCLUDES
		${PLUGIN_DIR}/src
)

##### znet under plugin/external #####
set(ZNET_DIR ${PLUGIN_DIR}/external/znet)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(${ZNET_DIR}/vendor/fmt ${CMAKE_CURRENT_BINARY_DIR}/fmt)
add_subdirectory(${ZNET_DIR}/znet ${CMAKE_CURRENT_BINARY_DIR}/znet)

list(APPEND PLUGIN_LINKLIBS
		znet
)
