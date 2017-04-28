enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/lib/src)

add_executable(in_cksum in_cksum.cc ${CMAKE_SOURCE_DIR}/lib/src/in_cksum.c)
target_link_libraries(in_cksum PUBLIC benchmark)
add_test(in_cksum in_cksum)
