#
# To build against a local gipOpus checkout instead of the pinned commit, use
# a path ending in gipOpus:
#   cmake -DFETCHCONTENT_SOURCE_DIR_GIPOPUS=/path/to/gipOpus ...

include_guard(GLOBAL)

include(FetchContent)

set(GIPOPUS_GIT_REPOSITORY "https://github.com/GlistPlugins/gipOpus.git"
		CACHE STRING "Git repository to fetch gipOpus from")
set(GIPOPUS_GIT_TAG "860037a58b18768c4b5e30e97d5b43224b84a873"
		CACHE STRING "gipOpus commit, tag or branch to build against")

if(DEFINED FETCHCONTENT_SOURCE_DIR_GIPOPUS AND NOT FETCHCONTENT_SOURCE_DIR_GIPOPUS STREQUAL "")
	file(TO_CMAKE_PATH "${FETCHCONTENT_SOURCE_DIR_GIPOPUS}" _gipopus_source_dir)
	if(NOT EXISTS "${_gipopus_source_dir}/CMakeLists.txt")
		message(FATAL_ERROR
				"gipMultiplayer: FETCHCONTENT_SOURCE_DIR_GIPOPUS does not contain gipOpus: "
				"${_gipopus_source_dir}")
	endif()
else()
	# Keep fetched plugins in the standard Glist plugin directory. An existing
	# checkout is left untouched and used as-is.
	file(TO_CMAKE_PATH "${PLUGINS_DIR}/gipOpus" _gipopus_source_dir)
	if(NOT EXISTS "${_gipopus_source_dir}/CMakeLists.txt")
		FetchContent_Declare(gipopus
				GIT_REPOSITORY ${GIPOPUS_GIT_REPOSITORY}
				GIT_TAG ${GIPOPUS_GIT_TAG}
				GIT_SUBMODULES external/opus
				GIT_SUBMODULES_RECURSE TRUE
				GIT_PROGRESS TRUE
				SOURCE_DIR "${_gipopus_source_dir}"
				# Populate here, then include the plugin below so its list variables
				# stay in the same scope as the other Glist plugins.
				SOURCE_SUBDIR _gipmultiplayer_fetch_only
		)
		FetchContent_MakeAvailable(gipopus)
	endif()
endif()

if(NOT EXISTS "${_gipopus_source_dir}/external/opus/CMakeLists.txt")
	find_package(Git REQUIRED)
	execute_process(
			COMMAND ${GIT_EXECUTABLE} -C "${_gipopus_source_dir}" submodule update --init --recursive
			RESULT_VARIABLE _gipopus_submodule_result
			ERROR_VARIABLE _gipopus_submodule_error
	)
	if(NOT _gipopus_submodule_result EQUAL 0)
		message(FATAL_ERROR
				"gipMultiplayer could not initialize gipOpus dependencies: "
				"${_gipopus_submodule_error}")
	endif()
	unset(_gipopus_submodule_error)
	unset(_gipopus_submodule_result)
endif()

set(_gipopus_saved_pluginname "${pluginname}")
set(_gipopus_saved_plugin_dir "${PLUGIN_DIR}")
set(_gipopus_saved_plugins_dir "${PLUGINS_DIR}")

get_filename_component(PLUGINS_DIR "${_gipopus_source_dir}" DIRECTORY)
include("${_gipopus_source_dir}/CMakeLists.txt")

set(PLUGINS_DIR "${_gipopus_saved_plugins_dir}")
set(PLUGIN_DIR "${_gipopus_saved_plugin_dir}")
set(pluginname "${_gipopus_saved_pluginname}")

unset(_gipopus_saved_plugins_dir)
unset(_gipopus_saved_plugin_dir)
unset(_gipopus_saved_pluginname)
unset(_gipopus_source_dir)
