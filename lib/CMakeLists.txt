string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(version.h.in version.h)
configure_file(version.c.in version.c)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME_LC)
set(PLAT ${CMAKE_CURRENT_SOURCE_DIR}/plat_${CMAKE_SYSTEM_NAME_LC}.c)
if(NOT(EXISTS ${PLAT}))
  set(PLAT ${CMAKE_CURRENT_SOURCE_DIR}/plat_generic.c)
endif()

set(SOURCES ${SOURCES}
  ${PLAT} util.c warpcore.c ${CMAKE_CURRENT_BINARY_DIR}/version.c)

set(SHIMSOURCES ${SOURCES} backend_shim.c)
add_library(shimcore ${SHIMSOURCES})
set_property(TARGET shimcore PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
target_include_directories(shimcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS shimcore DESTINATION lib)
install(FILES warpcore.h util.h plat.h DESTINATION include)

if(${netmap_FOUND})
  set(WARPSOURCES
    ${SOURCES} arp.c eth.c icmp.c in_cksum.c ip.c udp.c backend_netmap.c)
  add_library(warpcore ${WARPSOURCES})
  target_compile_definitions(warpcore PRIVATE -DWITH_NETMAP)
  set_property(TARGET warpcore PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
  target_include_directories(warpcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

  install(TARGETS warpcore DESTINATION lib)
endif()

