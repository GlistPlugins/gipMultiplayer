#
# To build against a local znet checkout instead of the pinned commit:
#   cmake -DFETCHCONTENT_SOURCE_DIR_ZNET=/path/to/znet ...

include_guard(GLOBAL)

include(FetchContent)

# Fail here rather than somewhere inside the fetched project.
if(CMAKE_VERSION VERSION_LESS 3.29)
	message(FATAL_ERROR
			"gipMultiplayer needs CMake 3.29 or newer to build znet (found ${CMAKE_VERSION}).")
endif()

set(ZNET_GIT_REPOSITORY "https://github.com/teoncreative/znet.git"
		CACHE STRING "Git repository to fetch znet from")
set(ZNET_GIT_TAG "0976ef132eb1d033e925b59b70215105be224788"
		CACHE STRING "znet commit, tag or branch to build against")
set(ZNET_ZSTD_GIT_TAG "48c0ed73625272cb7445183b5e256b5d0a130316"
		CACHE STRING "zstd commit to build znet's compression against")

# znet defaults to C++20. Build it at the same standard as the engine and the
# app so its public headers compile the same way on both sides of the link.
set(ZNET_CXX_STANDARD "14"
		CACHE STRING "C++ standard used to build znet (14, 17, 20 or 23)")

##### OPENSSL #####
# znet does find_package(OpenSSL REQUIRED). On Windows OpenSSL ships with the
# glist toolchain, which is not a place CMake looks by default.
if(WIN32)
	set(OPENSSL_ROOT_DIR "C:/dev/glist/zbin/glistzbin-win64/clang64")
	set(OPENSSL_INCLUDE_DIR "C:/dev/glist/zbin/glistzbin-win64/clang64/include")
	set(OPENSSL_CRYPTO_LIBRARY "C:/dev/glist/zbin/glistzbin-win64/clang64/lib/libcrypto.lib")
endif()

##### ZSTD #####
# Pinned to the same commit as znet's vendor/zstd submodule and added before
# znet, which turns compression on with `if(TARGET libzstd)`.
#
# Set as cache entries because zstd's CMakeLists asks for policy version 3.10,
# under which option() ignores a plain variable of the same name (CMP0077 OLD).
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)

FetchContent_Declare(zstd
		GIT_REPOSITORY "https://github.com/facebook/zstd.git"
		GIT_TAG ${ZNET_ZSTD_GIT_TAG}
		SOURCE_SUBDIR build/cmake
)

# zstd force-sets CMAKE_BUILD_TYPE to Release when it is empty, which would
# quietly change how the engine and the app get compiled.
set(_znet_saved_build_type "${CMAKE_BUILD_TYPE}")
FetchContent_MakeAvailable(zstd)
if(NOT _znet_saved_build_type AND CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "${_znet_saved_build_type}"
			CACHE STRING "Choose the type of build." FORCE)
endif()
unset(_znet_saved_build_type)

##### ZNET #####
# GIT_SUBMODULES "" skips znet's own vendor/zstd submodule, which the fetch
# above replaces. CMP0097 is what makes an empty value mean "no submodules"
# rather than "all of them".
if(POLICY CMP0097)
	cmake_policy(SET CMP0097 NEW)
endif()

# SOURCE_SUBDIR points at the library; the repository root also builds znet's
# examples, tests and rendezvous server.
FetchContent_Declare(znet
		GIT_REPOSITORY ${ZNET_GIT_REPOSITORY}
		GIT_TAG ${ZNET_GIT_TAG}
		GIT_SUBMODULES ""
		SOURCE_SUBDIR znet
)
FetchContent_MakeAvailable(znet)

set_target_properties(znet PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
