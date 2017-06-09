cmake_minimum_required(VERSION 3.0)

project(warpcore VERSION 0.8.0 LANGUAGES C CXX)

# Check for dependencies before mucking with the compiler flags
include(CheckFunctionExists)
check_function_exists(recvmmsg HAVE_RECVMMSG)
check_function_exists(sendmmsg HAVE_SENDMMSG)
check_function_exists(kqueue HAVE_KQUEUE)
check_function_exists(epoll_create HAVE_EPOLL)
check_function_exists(arc4random HAVE_ARC4RANDOM)
find_package(Threads REQUIRED)

# See if we can enable testing
include(CheckCXXSymbolExists)
set(CMAKE_REQUIRED_LIBRARIES benchmark)
check_cxx_symbol_exists(benchmark::Initialize benchmark/benchmark.h
                        HAVE_BENCHMARK_H)
include(CMakePushCheckState)
cmake_reset_check_state()

# Look for netmap
include(CheckIncludeFile)
set(CMAKE_REQUIRED_INCLUDES "/usr/include /usr/local/include")
check_include_file(net/netmap_user.h HAVE_NETMAP_H)
cmake_reset_check_state()

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

# Use modern C and C++
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 11)

# Set general compile flags, if they are supported
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
foreach(FLAG -march=native -Wextra -Wpedantic -Weverything -Werror
        -fcolor-diagnostics -fdiagnostics-color=always)
  string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
  check_cxx_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endforeach()

# Set CXX flags
foreach(FLAG -Wno-c++11-extensions -Wno-c++98-compat -Wno-global-constructors)
  string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
  check_cxx_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endforeach()

# Set C flags
foreach(FLAG -Wno-missing-field-initializers -Wno-missing-braces
        -Wno-gnu-statement-expression -Wno-extended-offsetof
        -Wno-disabled-macro-expansion -Wno-cast-align)
  string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
  check_c_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
endforeach()

# flags for "Debug" and "None" builds
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

if(CMAKE_BUILD_TYPE MATCHES Debug)
  # Default debug logging level
  if(NOT DEFINED DLEVEL)
    set(DLEVEL "debug")
  endif()

  # Set some additional flags for Debug builds
  set(FLAGS -fno-omit-frame-pointer -fsanitize=leak -fsanitize=address
      -fsanitize=memory -fsanitize-memory-track-origins -fsanitize=thread
      -fsanitize=undefined -fno-sanitize=alignment)
  if(CMAKE_C_COMPILER_ID MATCHES Clang)
    # -fsanitize=undefined does not work on clang
    list(REMOVE_ITEM FLAGS -fsanitize=undefined)
  endif()
  if(CMAKE_SYSTEM_NAME MATCHES FreeBSD)
    # -fsanitize=leak does not work on clang
    list(REMOVE_ITEM FLAGS -fsanitize=leak)
  endif()
  foreach(FLAG ${FLAGS})
    string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
    check_c_compiler_flag(${FLAG} ${F})
    if(${F})
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${FLAG}")
      set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${FLAG}")
    endif()
  endforeach()

  # Use include-what-you-use to check #includes, if it's installed
  find_program(IWYU NAMES include-what-you-use iwyu)
  if(IWYU)
    message(STATUS "Found include-what-you-use: ${IWYU}")
    string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME_LC)
    set(CMAKE_C_INCLUDE_WHAT_YOU_USE
        ${IWYU} -Xiwyu
        --mapping_file=${PROJECT_SOURCE_DIR}/misc/${CMAKE_SYSTEM_NAME_LC}.imp)
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${CMAKE_C_INCLUDE_WHAT_YOU_USE})
  endif()

  # Use clang-tidy for extra warnings, if it's installed
  find_program(CLANGTIDY NAMES clang-tidy clang-tidy40 clang-tidy39
               clang-tidy38 clang-tidy37)
  if(CLANGTIDY)
    message(STATUS "Found clang-tidy: ${CLANGTIDY}")
    set(CLANG_TIDY_CHECKS *
        -clang-analyzer-alpha.deadcode.UnreachableCode
        -clang-analyzer-alpha.core.CastToStruct
        -google-readability-braces-around-statements
        -readability-braces-around-statements
        -google-readability-todo
    )
    string(REPLACE ";" "," CLANG_TIDY_CHECKS "${CLANG_TIDY_CHECKS}")
    set(CMAKE_C_CLANG_TIDY ${CLANGTIDY} -checks=${CLANG_TIDY_CHECKS})
    set(CMAKE_CXX_CLANG_TIDY ${CMAKE_C_CLANG_TIDY})
  endif()
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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
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
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -Ofast")

# flags for "RelWithDebInfo" builds
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Ofast -g")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Ofast -g")

# Extract compile options into variable, for doxygen's CLANG_ASSISTED_PARSING
get_property(COMPILE_OPTIONS DIRECTORY PROPERTY COMPILE_OPTIONS)
string(REPLACE ";" " " COMPILE_OPTIONS "${COMPILE_OPTIONS}")

add_definitions(-DDLEVEL=${DLEVEL})

add_subdirectory(doc)
add_subdirectory(bin)
add_subdirectory(lib)

if(HAVE_BENCHMARK_H)
  add_subdirectory(test)
  enable_testing()
else()
  message(STATUS "Google Benchmark not found; not enabling tests")
endif()

