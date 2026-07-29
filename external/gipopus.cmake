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

# Use a source directory named gipOpus because the fetched plugin follows the
# Glist convention of resolving itself as ${PLUGINS_DIR}/gipOpus.
set(_gipopus_source_dir "${FETCHCONTENT_BASE_DIR}/gipOpus")
FetchContent_Declare(gipopus
		GIT_REPOSITORY ${GIPOPUS_GIT_REPOSITORY}
		GIT_TAG ${GIPOPUS_GIT_TAG}
		GIT_SUBMODULES external/opus
		GIT_SUBMODULES_RECURSE TRUE
		GIT_PROGRESS TRUE
		SOURCE_DIR "${_gipopus_source_dir}"
		# Populate here, then include the plugin below so its list variables stay
		# in the same scope as the other Glist plugins.
		SOURCE_SUBDIR _gipmultiplayer_fetch_only
)
FetchContent_MakeAvailable(gipopus)

set(_gipopus_saved_pluginname "${pluginname}")
set(_gipopus_saved_plugin_dir "${PLUGIN_DIR}")
set(_gipopus_saved_plugins_dir "${PLUGINS_DIR}")

get_filename_component(PLUGINS_DIR "${gipopus_SOURCE_DIR}" DIRECTORY)
include("${gipopus_SOURCE_DIR}/CMakeLists.txt")

set(PLUGINS_DIR "${_gipopus_saved_plugins_dir}")
set(PLUGIN_DIR "${_gipopus_saved_plugin_dir}")
set(pluginname "${_gipopus_saved_pluginname}")

unset(_gipopus_saved_plugins_dir)
unset(_gipopus_saved_plugin_dir)
unset(_gipopus_saved_pluginname)
unset(_gipopus_source_dir)
