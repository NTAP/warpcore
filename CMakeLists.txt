cmake_minimum_required(VERSION 3.0)
project(warpcore VERSION 0.0.1 LANGUAGES C)

# Force C11
add_compile_options(-std=c11)

# Warn about everything
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Optimize
add_compile_options(-march=native -Ofast -pipe)

# Debug
add_compile_options(-g)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
add_compile_options(-D _GNU_SOURCE)
endif()

# Set linker options that are valid for all linkers
# set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

# Set additional options that are compiler-specific
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  # see https://llvm.org/bugs/show_bug.cgi?id=21689
  add_compile_options(-Wno-missing-field-initializers)

  # Even more warnings for clang
  add_compile_options(-Weverything)

  # Generate profiling output
  # add_compile_options(-fprofile-instr-generate -flto)
  # set(CMAKE_SHARED_LINKER_FLAGS
  #   "-fprofile-instr-generate -flto ${CMAKE_SHARED_LINKER_FLAGS}")
  # set(CMAKE_EXE_LINKER_FLAGS
  #   "-fprofile-instr-generate -flto ${CMAKE_EXE_LINKER_FLAGS}")

elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
endif()

# Extract compile options into variable, for doxygen's CLANG_ASSISTED_PARSING
get_property(COMPILE_OPTIONS DIRECTORY PROPERTY COMPILE_OPTIONS)
string(REPLACE ";" " " COMPILE_OPTIONS "${COMPILE_OPTIONS}")

# # Make sure that a bunch of programs we depend on are installed
# foreach(PROG daemon pkill tcpdump tshark sed make sleep rm
#         vagrant cat tail tmux)
#   string(TOUPPER ${PROG} VARNAME)
#   find_program(${VARNAME} ${PROG})
#   if(NOT ${VARNAME})
#     message(WARNING "${PROG} not found; please install it")
#   else()
#     message(STATUS "Found ${PROG}: ${${VARNAME}}")
#   endif()
# endforeach()

# # find_program() for sudo is unlikely to work, hence the special case; see
# # https://cmake.org/pipermail/cmake/2010-September/039520.html
# find_program(SUDO sudo)
# if(NOT SUDO)
#   set(SUDO /usr/bin/sudo)
# endif()

add_subdirectory(examples)
# add_subdirectory(doc)
# add_subdirectory(external)
add_subdirectory(lib)
