enable_testing()

# See if we have google benchmark
include(CheckCXXSymbolExists)
set(CMAKE_REQUIRED_LIBRARIES benchmark)
check_cxx_symbol_exists(benchmark::Initialize benchmark/benchmark.h
                        HAVE_BENCHMARK_H)
cmake_reset_check_state()

if(NOT HAVE_BENCHMARK_H)
  include(ExternalProject)

  find_package(Threads)

  externalproject_add(google-benchmark
    UPDATE_COMMAND ""
    GIT_REPOSITORY https://github.com/google/benchmark
    GIT_TAG v1.1.0
    GIT_SHALLOW 1
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}
      -DCMAKE_BUILD_TYPE=Release
      -DBENCHMARK_ENABLE_LTO=true
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/lib/libbenchmark.a
  )

  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include ${CMAKE_BINARY_DIR}/lib)

  add_library(benchmark STATIC IMPORTED GLOBAL)

  set_target_properties(benchmark PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/lib/libbenchmark.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/include
  )

  add_dependencies(benchmark google-benchmark Threads)
  set(HAVE_BENCHMARK_H 1)
endif()

include_directories(${CMAKE_SOURCE_DIR}/lib/src)

if(HAVE_BENCHMARK_H)
  add_executable(bench_sock bench.cc common.c ${CMAKE_SOURCE_DIR}/lib/src/in_cksum.c)
  target_link_libraries(bench_sock PUBLIC benchmark sock_dyn)
  add_test(bench_sock bench_sock)

  if(HAVE_NETMAP_H)
    add_executable(bench_warp bench.cc common.c ${CMAKE_SOURCE_DIR}/lib/src/in_cksum.c)
    target_compile_definitions(bench_warp PRIVATE -DWITH_NETMAP)
    target_link_libraries(bench_warp PUBLIC benchmark warp_dyn)
    add_test(bench_warp bench_warp)
  endif()
endif()


add_executable(test_sock common.c test_sock.c)
target_link_libraries(test_sock PUBLIC sock_dyn)
add_test(test_sock test_sock)

if(HAVE_NETMAP_H)
  add_executable(test_warp common.c test_sock.c)
  target_compile_definitions(test_warp PRIVATE -DWITH_NETMAP)
  target_link_libraries(test_warp PUBLIC warp_dyn)
  add_test(test_warp test_warp)
endif()
