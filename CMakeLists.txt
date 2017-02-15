cmake_minimum_required(VERSION 3.0)

project(warpcore VERSION 0.2.0 LANGUAGES C)

# Build "Debug" type by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug
        CACHE STRING "Set build type (default Debug)" FORCE)
endif()

# Set general compile flags, if they are supported
include(CheckCCompilerFlag)
foreach(FLAG -std=c11 -march=native -Wextra -Wpedantic -Weverything -Werror
        -Wno-missing-field-initializers -Wno-missing-braces -fcolor-diagnostics)
  string(REGEX REPLACE "[-=]" "" F ${FLAG})
  check_c_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
endforeach()

# flags for "Debug" and "None" builds
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")

if(CMAKE_BUILD_TYPE MATCHES Debug)
  # Default debug logging level
  if(NOT DEFINED DLEVEL)
    set(DLEVEL "debug")
  endif()

  # Set some additional flags for Debug builds
  set(FLAGS -fsanitize=address -fsanitize=memory -fsanitize-memory-track-origins
          -fsanitize=thread -fno-omit-frame-pointer)
  if(NOT CMAKE_SYSTEM_NAME MATCHES FreeBSD)
    # -fsanitize=leak does not work on FreeBSD
    set(FLAGS ${FLAGS} -fsanitize=leak)
  elseif(NOT CMAKE_SYSTEM_NAME MATCHES Darwin)
    # -fsanitize=undefined -fno-sanitize=alignment does not work on Darwin
    set(FLAGS ${FLAGS} -fsanitize=undefined -fno-sanitize=alignment)
  endif()
  foreach(FLAG ${FLAGS})
    string(REGEX REPLACE "[-=]" "" F ${FLAG})
    check_c_compiler_flag(${FLAG} ${F})
    if(${F})
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${FLAG}")
    endif()
  endforeach()
else()
  # Default logging level
  if(NOT DEFINED DLEVEL)
    set(DLEVEL "notice")
  endif()

  # See if we can use LTO
  check_c_compiler_flag(-flto LTO)
  if(${LTO})
    # Turn LTO on
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")
    # Use the correct toolchain for LTO, if enabled
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
      set(CMAKE_AR llvm-ar)
      set(CMAKE_RANLIB llvm-ranlib)
    elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
      set(CMAKE_AR gcc-ar)
      set(CMAKE_RANLIB gcc-ranlib)
    endif()
  endif()
endif()

# flags for "Release" builds
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -Ofast")

# flags for "RelWithDebInfo" builds
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Ofast -g")

# Check if _GNU_SOURCE is available.
include(CheckSymbolExists)
check_symbol_exists(__GNU_LIBRARY__ "features.h" _GNU_SOURCE)
if(_GNU_SOURCE)
  add_definitions(-D_GNU_SOURCE)
endif()

# Extract compile options into variable, for doxygen's CLANG_ASSISTED_PARSING
get_property(COMPILE_OPTIONS DIRECTORY PROPERTY COMPILE_OPTIONS)
string(REPLACE ";" " " COMPILE_OPTIONS "${COMPILE_OPTIONS}")

# Look for netmap
find_path(HAVE_NETMAP_H net/netmap.h PATHS /usr/local/include)
if(HAVE_NETMAP_H)
  message(STATUS "Found netmap include in ${HAVE_NETMAP_H}")
else()
  message(STATUS "netmap.h not found; building shim backend only")
endif()

string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME_LC)
if(CMAKE_BUILD_TYPE MATCHES Debug)
  # Use include-what-you-use to check #includes, if it's installed
  find_program(IWYU NAMES include-what-you-use iwyu)
  if(IWYU)
    message(STATUS "Found include-what-you-use: ${IWYU}")
    set(CMAKE_C_INCLUDE_WHAT_YOU_USE
        ${IWYU} -Xiwyu --no_comments -Xiwyu
        --mapping_file=${PROJECT_SOURCE_DIR}/misc/${CMAKE_SYSTEM_NAME_LC}.imp)
  endif()

  # Use clang-tidy for extra warnings, if it's installed
  find_program(CLANGTIDY NAMES clang-tidy)
  if(CLANGTIDY)
    message(STATUS "Found clang-tidy: ${CLANGTIDY}")
    set(CLANG_TIDY_CHECKS *
        -google-readability-braces-around-statements
        -readability-braces-around-statements
        -google-readability-function-size
        -readability-function-size
        -google-readability-todo
        -readability-else-after-return
        -clang-analyzer-alpha.deadcode.UnreachableCode
        -clang-analyzer-alpha.core.CastToStruct
        -clang-analyzer-alpha.core.CastSize)
    string(REPLACE ";" "," CLANG_TIDY_CHECKS "${CLANG_TIDY_CHECKS}")
    set(CMAKE_C_CLANG_TIDY ${CLANGTIDY} -checks=${CLANG_TIDY_CHECKS})
  endif()
endif()

add_definitions(-DDLEVEL=${DLEVEL})

add_subdirectory(doc)
add_subdirectory(bin)
add_subdirectory(lib)

# Add a "distclean" target; see http://stackoverflow.com/a/12055610/1437081
add_custom_target(distclean COMMAND git clean -fdx)
