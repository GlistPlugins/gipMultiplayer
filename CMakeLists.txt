cmake_minimum_required (VERSION 3.10.2)

##### PLUGIN NAME #####
set(pluginname gipNetcode)

##### PLUGIN PATHS #####
set(PLUGIN_DIR ${PLUGINS_DIR}/${pluginname})

##### PLUGIN SOURCES #####
list(APPEND PLUGIN_SRCS
		${PLUGIN_DIR}/src/gipServerHandler.cpp
		${PLUGIN_DIR}/src/gipClientHandler.cpp
)

##### PLUGIN HEADERS #####
list(APPEND PLUGIN_INCLUDES
		${PLUGIN_DIR}/src
)

##### znet under plugin/external #####
set(ZNET_DIR ${PLUGIN_DIR}/external/znet)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
	set(OPENSSL_ROOT_DIR "C:/dev/glist/zbin/glistzbin-win64/clang64")
	set(OPENSSL_INCLUDE_DIR "C:/dev/glist/zbin/glistzbin-win64/clang64/include")
	set(OPENSSL_CRYPTO_LIBRARY "C:/dev/glist/zbin/glistzbin-win64/clang64/lib/libcrypto.lib")
endif()
add_subdirectory(${ZNET_DIR}/vendor/fmt ${CMAKE_CURRENT_BINARY_DIR}/fmt)
add_subdirectory(${ZNET_DIR}/znet ${CMAKE_CURRENT_BINARY_DIR}/znet)
set_target_properties(znet PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
list(APPEND PLUGIN_LINKLIBS
		znet
)
