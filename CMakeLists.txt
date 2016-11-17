cmake_minimum_required(VERSION 3.0)

# we use some custom compiler flags
set(CMAKE_USER_MAKE_RULES_OVERRIDE "CMakeInit.txt")

project(warpcore VERSION 0.0.1 LANGUAGES C)

# Check if _GNU_SOURCE is available.
include(CheckSymbolExists)
CHECK_SYMBOL_EXISTS(__GNU_LIBRARY__ "features.h" _GNU_SOURCE)
if(_GNU_SOURCE)
  add_definitions(-D_GNU_SOURCE)
endif()

# Build "Debug" type by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug
        CACHE STRING "Set build type (default Debug)" FORCE)
endif()

# Extract compile options into variable, for doxygen's CLANG_ASSISTED_PARSING
get_property(COMPILE_OPTIONS DIRECTORY PROPERTY COMPILE_OPTIONS)
string(REPLACE ";" " " COMPILE_OPTIONS "${COMPILE_OPTIONS}")

include(FindPackageHandleStandardArgs)
find_path(NETMAP_INCLUDE netmap_user.h PATH_SUFFIXES net)
find_package_handle_standard_args(netmap DEFAULT_MSG NETMAP_INCLUDE)

add_subdirectory(doc)
add_subdirectory(examples)
add_subdirectory(lib)

# Add a "distclean" target; see http://stackoverflow.com/a/12055610/1437081
add_custom_target(distclean COMMAND git clean -fdx)
