cmake_minimum_required(VERSION 3.9)

project(warpcore VERSION 0.15.0 LANGUAGES C CXX)

# Check for dependencies before mucking with the compiler flags
include(CheckFunctionExists)
check_function_exists(recvmmsg HAVE_RECVMMSG)
check_function_exists(sendmmsg HAVE_SENDMMSG)
check_function_exists(kqueue HAVE_KQUEUE)
check_function_exists(epoll_create HAVE_EPOLL)
check_function_exists(arc4random HAVE_ARC4RANDOM)
find_package(Threads REQUIRED)

# Look for netmap
include(CheckIncludeFile)
set(CMAKE_REQUIRED_INCLUDES
    /usr/include /usr/local/include ${CMAKE_SOURCE_DIR}/lib/include
)
check_include_file(net/netmap_user.h HAVE_NETMAP_H)
include(CMakePushCheckState)
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

# Generate compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

# Use modern C and C++
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 11)

# Set general compile flags, if they are supported
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
foreach(FLAG -march=native -Wextra -Wpedantic -Weverything -Werror
        -fcolor-diagnostics -fdiagnostics-color=always
        # for older gcc:
        -Wno-missing-field-initializers
        )
  string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
  check_c_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
  check_cxx_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endforeach()

# Set CXX flags
foreach(FLAG -Wno-c++98-compat -Wno-global-constructors)
  string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
  check_cxx_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endforeach()

# flags for "Debug" and "None" builds
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

# disable IPO by default
set(IPO NO)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  # Default debug logging level
  if(NOT DEFINED DLEVEL)
    set(DLEVEL "deb")
  endif()

  # See if we can use the address sanitizer
  set(CMAKE_REQUIRED_FLAGS -fsanitize=address)
  check_c_compiler_flag("" _fsanitize_address)
  if(_fsanitize_address)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  endif()
  unset(CMAKE_REQUIRED_FLAGS)

  # Set some additional flags for Debug builds
  set(FLAGS -fno-omit-frame-pointer -fsanitize-address-use-after-scope
      -fsanitize=undefined -fsanitize=unsigned-integer-overflow
      -fno-sanitize=alignment -fno-common
  )
  foreach(FLAG ${FLAGS})
    string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
    check_c_compiler_flag(${FLAG} ${F})
    if(${F})
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
    endif()
    check_cxx_compiler_flag(${FLAG} ${F})
    if(${F})
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
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
        -google-readability-braces-around-statements
        -readability-braces-around-statements
        -google-readability-todo
        -cppcoreguidelines-pro-type-vararg
    )
    string(REPLACE ";" "," CLANG_TIDY_CHECKS "${CLANG_TIDY_CHECKS}")
    set(CMAKE_C_CLANG_TIDY ${CLANGTIDY}
        -analyze-temporary-dtors -system-headers
        -checks=${CLANG_TIDY_CHECKS}
    )
    set(CMAKE_CXX_CLANG_TIDY ${CMAKE_C_CLANG_TIDY})
  endif()
else()
  # Default logging level
  if(NOT DEFINED DLEVEL)
    set(DLEVEL "notice")
  endif()

  # check for INTERPROCEDURAL_OPTIMIZATION support
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO)
endif()

# flags for "Release" builds
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -Ofast")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

# flags for "RelWithDebInfo" builds
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -g3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

add_definitions(-DDLEVEL=${DLEVEL})

add_subdirectory(doc)
add_subdirectory(bin)
add_subdirectory(lib)

add_subdirectory(test)
enable_testing()

