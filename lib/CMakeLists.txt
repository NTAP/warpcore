include(CheckFunctionExists)
check_function_exists(recvmmsg HAVE_RECVMMSG)
check_function_exists(sendmmsg HAVE_SENDMMSG)

string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(config.h.in config.h)
configure_file(config.c.in config.c)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(SOURCES ${SOURCES}
  plat.c util.c warpcore.c ${CMAKE_CURRENT_BINARY_DIR}/config.c)

set(SHIMSOURCES ${SOURCES} backend_shim.c)
add_library(shimcore ${SHIMSOURCES})
set_property(TARGET shimcore PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
target_include_directories(shimcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                           PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS shimcore DESTINATION lib)
install(FILES warpcore.h util.h plat.h
        ${CMAKE_CURRENT_BINARY_DIR}/config.h DESTINATION include)

if(HAVE_NETMAP_H)
  set(WARPSOURCES
    ${SOURCES} arp.c eth.c icmp.c in_cksum.c ip.c udp.c backend_netmap.c)
  add_library(warpcore ${WARPSOURCES})
  target_compile_definitions(warpcore PRIVATE -DWITH_NETMAP)
  set_property(TARGET warpcore PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
  target_include_directories(warpcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                             PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

  install(TARGETS warpcore DESTINATION lib)
endif()

