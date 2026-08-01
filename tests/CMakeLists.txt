cmake_minimum_required(VERSION 3.29)

project(gipMultiplayerVoiceTests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(GIPMULTIPLAYER_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(PLUGINS_DIR "${GIPMULTIPLAYER_DIR}/.." ABSOLUTE)

set(pluginname gipMultiplayer)
set(PLUGIN_DIR "${GIPMULTIPLAYER_DIR}")
set(PLUGIN_SRCS)
set(PLUGIN_INCLUDES)
set(PLUGIN_LINKLIBS)

include("${GIPMULTIPLAYER_DIR}/external/gipopus.cmake")
set(ZNET_ENABLE_STRICT_WARNINGS OFF CACHE BOOL "Disable upstream -Werror in the plugin test harness" FORCE)
include("${GIPMULTIPLAYER_DIR}/external/znet.cmake")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	# znet 3.1's precompiled header does not include <cstring>, although
	# buffer.h uses std::memcpy/std::memset. GCC 15 no longer exposes those
	# declarations transitively in every translation unit.
	target_compile_options(znet PRIVATE -include cstring)
endif()

find_package(Threads REQUIRED)

add_executable(gipMultiplayerVoiceTests
		VoiceTests.cpp
		${GIPMULTIPLAYER_DIR}/src/voice/gTeamVoicePackets.cpp
		${GIPMULTIPLAYER_DIR}/src/voice/gTeamVoiceServer.cpp
		${GIPMULTIPLAYER_DIR}/src/audio/gVoiceAudioProcessor.cpp
		${PLUGIN_SRCS}
)

target_include_directories(gipMultiplayerVoiceTests PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/stubs
		${GIPMULTIPLAYER_DIR}/src
		${PLUGIN_INCLUDES}
)

target_link_libraries(gipMultiplayerVoiceTests PRIVATE
		${PLUGIN_LINKLIBS}
		znet
		Threads::Threads
)

enable_testing()
add_test(NAME gipMultiplayerVoiceTests COMMAND gipMultiplayerVoiceTests)
