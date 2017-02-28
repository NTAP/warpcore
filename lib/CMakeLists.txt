include(CheckFunctionExists)
check_function_exists(recvmmsg HAVE_RECVMMSG)
check_function_exists(sendmmsg HAVE_SENDMMSG)

string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(config.h.in config.h)
configure_file(config.c.in config.c)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME_LC)
set(PLAT ${CMAKE_CURRENT_SOURCE_DIR}/plat_${CMAKE_SYSTEM_NAME_LC}.c)
if(NOT(EXISTS ${PLAT}))
  message(STATUS "Cannot find " ${CMAKE_SYSTEM_NAME} "-specific platform "
          "support, using generic methods")
  set(PLAT ${CMAKE_CURRENT_SOURCE_DIR}/plat_generic.c)
endif()

set(SOURCES ${SOURCES}
  ${PLAT} util.c warpcore.c ${CMAKE_CURRENT_BINARY_DIR}/config.c)

set(SHIMSOURCES ${SOURCES} backend_shim.c)
add_library(shimcore ${SHIMSOURCES})
set_property(TARGET shimcore PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
target_include_directories(shimcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                           PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS shimcore DESTINATION lib)
install(FILES warpcore.h util.h plat.h config.h DESTINATION include)

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

