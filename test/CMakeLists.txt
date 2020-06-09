enable_testing()

if(HAVE_BENCHMARK_H)
  add_executable(bench_sock bench.cc common.c ${PROJECT_SOURCE_DIR}/lib/src/in_cksum.c)
  target_link_libraries(bench_sock PUBLIC benchmark pthread sockcore)
  target_compile_options(bench_sock PRIVATE -Wno-poison-system-directories)
  target_include_directories(bench_sock
    SYSTEM PRIVATE
      ${PROJECT_SOURCE_DIR}/lib/include
      ${PROJECT_BINARY_DIR}/lib/include
      ${PROJECT_SOURCE_DIR}/lib/src
      /usr/local/include
    )
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND CMAKE_COMPILER_IS_GNUCC)
    target_link_options(bench_sock PUBLIC -lc++)
  endif()
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
    target_compile_options(bench_warp PRIVATE -Wno-poison-system-directories)
    target_include_directories(bench_warp
    SYSTEM PRIVATE
      ${PROJECT_SOURCE_DIR}/lib/include
      ${PROJECT_BINARY_DIR}/lib/include
      ${PROJECT_SOURCE_DIR}/lib/src
      /usr/local/include
    )
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND CMAKE_COMPILER_IS_GNUCC)
      target_link_options(bench_warp PUBLIC -lc++)
    endif()
    set_target_properties(bench_warp
      PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        INTERPROCEDURAL_OPTIMIZATION ${IPO}
    )
    add_test(bench_warp bench_warp)
  endif()
endif()


foreach(TARGET sock iov hexdump queue many ecn)
  add_executable(test_${TARGET} common.c test_${TARGET}.c)
  target_link_libraries(test_${TARGET} PUBLIC sockcore)
  target_include_directories(test_${TARGET}
    PRIVATE ${PROJECT_SOURCE_DIR}/lib/src
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

  add_executable(test_many_warp common.c test_many.c)
  target_compile_definitions(test_many_warp PRIVATE -DWITH_NETMAP)
  target_link_libraries(test_many_warp PUBLIC warpcore)
  set_target_properties(test_many_warp
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      INTERPROCEDURAL_OPTIMIZATION ${IPO}
  )
  if(DSYMUTIL)
    add_custom_command(TARGET test_many_warp POST_BUILD
      COMMAND ${DSYMUTIL} ARGS $<TARGET_FILE:test_many_warp>
    )
  endif()
  add_test(test_many_warp test_many_warp)

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
        )
      set_target_properties(${TARGET}
        PROPERTIES
          POSITION_INDEPENDENT_CODE ON
          INTERPROCEDURAL_OPTIMIZATION ${IPO}
          LINK_FLAGS -fsanitize=fuzzer,address,undefined
      )
    endforeach()
  endif()
endif()
