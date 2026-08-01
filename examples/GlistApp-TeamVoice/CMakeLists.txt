cmake_minimum_required(VERSION 3.29)

set(APP_NAME GlistApp)
set(PLUGINS gipMultiplayer)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_TYPE SHARED)

if(NOT DEFINED CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
	set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "No dev warnings")
endif()

# Support both a generated myglistapps location and this repository location.
get_filename_component(TOP_DIR "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${TOP_DIR}/GlistEngine/engine/CMakeLists.txt")
	get_filename_component(TOP_DIR "${CMAKE_SOURCE_DIR}/../../../.." ABSOLUTE)
endif()
if(NOT EXISTS "${TOP_DIR}/GlistEngine/engine/CMakeLists.txt")
	message(FATAL_ERROR "Could not locate GlistEngine from ${CMAKE_SOURCE_DIR}")
endif()

if(WIN32)
	set(CMAKE_C_COMPILER "${TOP_DIR}/zbin/glistzbin-win64/clang64/bin/clang.exe")
	set(CMAKE_CXX_COMPILER "${TOP_DIR}/zbin/glistzbin-win64/clang64/bin/clang++.exe")
endif()

set(APP_DIR ${CMAKE_SOURCE_DIR})
project(${APP_NAME} LANGUAGES C CXX)

set(MAIN_DIR ${TOP_DIR}/GlistEngine)
set(ENGINE_DIR ${MAIN_DIR}/engine)

if(APPLE)
	add_subdirectory(${ENGINE_DIR} ./_build)
else()
	include(${ENGINE_DIR}/CMakeLists.txt)
endif()

set(GlistApp_SOURCES
		${APP_DIR}/src/main.cpp
		${APP_DIR}/src/gApp.cpp
		${APP_DIR}/src/gCanvas.cpp
		${APP_DIR}/src/voice/VoiceDemoClient.cpp
		${APP_DIR}/src/voice/VoiceDemoServer.cpp
)

set(GlistApp_HEADERS
		${APP_DIR}/src/gApp.h
		${APP_DIR}/src/gCanvas.h
		${APP_DIR}/src/voice/VoiceDemoPackets.h
		${APP_DIR}/src/voice/VoiceDemoClient.h
		${APP_DIR}/src/voice/VoiceDemoServer.h
)

add_executable(${APP_NAME}
		${GlistApp_SOURCES}
		${GlistApp_HEADERS}
)

target_include_directories(${APP_NAME} PRIVATE ${APP_DIR}/src)
target_link_libraries(${APP_NAME} PRIVATE GlistEngine)

install(
		DIRECTORY assets
		DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
)
