string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(version.h.in version.h)
configure_file(version.c.in version.c)

include(FindPackageHandleStandardArgs)
find_path(NETMAP_INCLUDE netmap_user.h PATH_SUFFIXES net)
find_package_handle_standard_args(netmap DEFAULT_MSG NETMAP_INCLUDE)

set(SOURCES arp.c util.c eth.c icmp.c in_cksum.c ip.c udp.c warpcore.c)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
  set(SOURCES ${SOURCES} plat_linux.c)
elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "FreeBSD")
  set(SOURCES ${SOURCES} plat_freebsd.c)
else()
  message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} not supported")
endif()

add_library(warpcore ${SOURCES})
target_include_directories(warpcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package (Threads)
target_link_libraries(warpcore ${CMAKE_THREAD_LIBS_INIT})
