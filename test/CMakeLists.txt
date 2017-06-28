enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/lib/src)

if(HAVE_BENCHMARK_H)
  add_executable(in_cksum in_cksum.cc ${CMAKE_SOURCE_DIR}/lib/src/in_cksum.c)
  target_link_libraries(in_cksum PUBLIC benchmark)
  add_test(in_cksum in_cksum)
endif()

add_executable(test_sock test_sock.c)
target_link_libraries(test_sock PUBLIC sock_dyn)
add_test(test_sock test_sock)

if(HAVE_NETMAP_H)
  add_executable(test_warp test_sock.c)
  target_link_libraries(test_warp PUBLIC warp_dyn)
  add_test(test_warp test_warp)
endif()
