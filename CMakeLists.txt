cmake_minimum_required(VERSION 3.0)

project(warpcore VERSION 0.0.1 LANGUAGES C)

# Set general compile flags, if they are supported
include(CheckCCompilerFlag)
foreach(FLAG -std=c11 -march=native -Wextra -Wpedantic -Weverything -Werror
        -Wno-missing-field-initializers -Wno-missing-braces)
  string(REGEX REPLACE "[-=]" "" F ${FLAG})
  check_c_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
endforeach()

# flags for "Debug" and "None" builds
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")

# flags for "Release" builds
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -Ofast")

# flags for "RelWithDebInfo" builds
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-DNDEBUG -Ofast -g")

# Check if _GNU_SOURCE is available.
include(CheckSymbolExists)
check_symbol_exists(__GNU_LIBRARY__ "features.h" _GNU_SOURCE)
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

# Look for netmap
include(CheckIncludeFiles)
check_include_files (net/netmap_user.h HAVE_NETMAP_H)

add_subdirectory(doc)
add_subdirectory(examples)
add_subdirectory(lib)

# Add a "distclean" target; see http://stackoverflow.com/a/12055610/1437081
add_custom_target(distclean COMMAND git clean -fdx)
