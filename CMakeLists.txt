cmake_minimum_required(VERSION 3.0)
project(warpcore VERSION 0.0.1 LANGUAGES C)

# Force C11
add_compile_options(-std=c11)

# Warn about everything
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Customize the compile options
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pipe -march=native")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
# set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")
# set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
# set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

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

add_subdirectory(examples)
add_subdirectory(lib)

# Add a "distclean" target; see http://stackoverflow.com/a/12055610/1437081
add_custom_target(distclean COMMAND git clean -fdx)
