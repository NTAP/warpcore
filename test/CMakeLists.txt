enable_testing()

if(NOT HAVE_BENCHMARK_H AND NOT DEFINED ENV{CONTINUOUS_INTEGRATION})
  include(ExternalProject)

  externalproject_add(google-benchmark
    UPDATE_COMMAND ""
    GIT_REPOSITORY https://github.com/google/benchmark
    GIT_SHALLOW 1
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}
      -DCMAKE_BUILD_TYPE=Release
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/lib/libbenchmark.a
  )

  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include ${CMAKE_BINARY_DIR}/lib)

  add_library(benchmark STATIC IMPORTED GLOBAL)

  set_target_properties(benchmark PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/lib/libbenchmark.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/include
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION ${IPO}
  )

  add_dependencies(benchmark google-benchmark)
  set(HAVE_BENCHMARK_H 1)
endif()

if(HAVE_BENCHMARK_H)
  add_executable(bench_sock bench.cc common.c ${CMAKE_SOURCE_DIR}/lib/src/in_cksum.c)
  target_link_libraries(bench_sock PUBLIC benchmark pthread sockcore)
  target_include_directories(bench_sock
    SYSTEM PRIVATE
      ${CMAKE_SOURCE_DIR}/lib/include
      ${CMAKE_BINARY_DIR}/lib/include
      ${CMAKE_SOURCE_DIR}/lib/src
    )
  set_target_properties(bench_sock
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      INTERPROCEDURAL_OPTIMIZATION ${IPO}
  )
  add_test(bench_sock bench_sock)

  if(HAVE_NETMAP_H)
    add_executable(bench_warp bench.cc common.c ${CMAKE_SOURCE_DIR}/lib/src/in_cksum.c)
    target_compile_definitions(bench_warp PRIVATE -DWITH_NETMAP)
    target_link_libraries(bench_warp PUBLIC benchmark pthread warpcore)
    target_include_directories(bench_warp
    SYSTEM PRIVATE
      ${CMAKE_SOURCE_DIR}/lib/include
      ${CMAKE_BINARY_DIR}/lib/include
      ${CMAKE_SOURCE_DIR}/lib/src
    )
    set_target_properties(bench_warp
      PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        INTERPROCEDURAL_OPTIMIZATION ${IPO}
    )
    add_test(bench_warp bench_warp)
  endif()
endif()


foreach(TARGET sock iov hexdump queue)
  add_executable(test_${TARGET} common.c test_${TARGET}.c)
  target_link_libraries(test_${TARGET} PUBLIC sockcore)
  target_include_directories(test_${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/lib/src)
  set_target_properties(test_${TARGET}
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      INTERPROCEDURAL_OPTIMIZATION ${IPO}
  )
  add_test(test_${TARGET} test_${TARGET})
endforeach()


if(HAVE_NETMAP_H)
  add_executable(test_warp common.c test_sock.c)
  target_compile_definitions(test_warp PRIVATE -DWITH_NETMAP)
  target_link_libraries(test_warp PUBLIC warpcore)
  set_target_properties(test_warp
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      INTERPROCEDURAL_OPTIMIZATION ${IPO}
  )
  add_test(test_warp test_warp)
endif()
