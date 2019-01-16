enable_testing()

if(NOT HAVE_BENCHMARK_H AND (DEFINED ENV{BENCHMARKING} OR BENCHMARKING))
  include(ExternalProject)

  externalproject_add(google-benchmark
    UPDATE_COMMAND ""
    GIT_REPOSITORY https://github.com/google/benchmark
    GIT_SHALLOW 1
    GIT_TAG v1.4.1
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}
      -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_ENABLE_GTEST_TESTS=OFF
    BUILD_BYPRODUCTS ${PROJECT_BINARY_DIR}/lib/libbenchmark.a
  )

  file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/include ${PROJECT_BINARY_DIR}/lib)

  add_library(benchmark STATIC IMPORTED GLOBAL)

  set_target_properties(benchmark PROPERTIES
    IMPORTED_LOCATION ${PROJECT_BINARY_DIR}/lib/libbenchmark.a
    INTERFACE_INCLUDE_DIRECTORIES ${PROJECT_BINARY_DIR}/include
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION ${IPO}
  )

  add_dependencies(benchmark google-benchmark)
  set(HAVE_BENCHMARK_H 1)
endif()

if(HAVE_BENCHMARK_H)
  add_executable(bench_sock bench.cc common.c ${PROJECT_SOURCE_DIR}/lib/src/in_cksum.c)
  target_link_libraries(bench_sock PUBLIC benchmark pthread sockcore)
  target_include_directories(bench_sock
    SYSTEM PRIVATE
      ${PROJECT_SOURCE_DIR}/lib/include
      ${PROJECT_BINARY_DIR}/lib/include
      ${PROJECT_SOURCE_DIR}/lib/src
      ${PROJECT_SOURCE_DIR}/lib/src/klib
    )
  set_target_properties(bench_sock
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      INTERPROCEDURAL_OPTIMIZATION ${IPO}
  )
  add_test(bench_sock bench_sock)

  if(HAVE_NETMAP_H)
    add_executable(bench_warp bench.cc common.c ${PROJECT_SOURCE_DIR}/lib/src/in_cksum.c)
    target_compile_definitions(bench_warp PRIVATE -DWITH_NETMAP)
    target_link_libraries(bench_warp PUBLIC benchmark pthread warpcore)
    target_include_directories(bench_warp
    SYSTEM PRIVATE
      ${PROJECT_SOURCE_DIR}/lib/include
      ${PROJECT_BINARY_DIR}/lib/include
      ${PROJECT_SOURCE_DIR}/lib/src
      ${PROJECT_SOURCE_DIR}/lib/src/klib
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
  target_include_directories(test_${TARGET}
    PRIVATE ${PROJECT_SOURCE_DIR}/lib/src
    SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/lib/src/klib
  )
  set_target_properties(test_${TARGET}
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      INTERPROCEDURAL_OPTIMIZATION ${IPO}
  )
  if(DSYMUTIL)
    add_custom_command(TARGET test_${TARGET} POST_BUILD
      COMMAND ${DSYMUTIL} ARGS $<TARGET_FILE:test_${TARGET}>
    )
  endif()
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
  if(DSYMUTIL)
    add_custom_command(TARGET test_warp POST_BUILD
      COMMAND ${DSYMUTIL} ARGS $<TARGET_FILE:test_warp>
    )
  endif()
  add_test(test_warp test_warp)
endif()


if(HAVE_FUZZER)
  foreach(TARGET fuzz)
    add_executable(${TARGET} ${TARGET}.c)
    target_compile_definitions(${TARGET} PRIVATE -DWITH_NETMAP)
    target_link_libraries(${TARGET} PUBLIC warpcore)
    target_include_directories(${TARGET}
      SYSTEM PRIVATE
        ${PROJECT_SOURCE_DIR}/lib/include
        ${PROJECT_BINARY_DIR}/lib/include
        ${PROJECT_SOURCE_DIR}/lib/src
        ${PROJECT_SOURCE_DIR}/lib/src/klib
      )
    set_target_properties(${TARGET}
      PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        INTERPROCEDURAL_OPTIMIZATION ${IPO}
        LINK_FLAGS -fsanitize=fuzzer,address,undefined
    )
  endforeach()
endif()
