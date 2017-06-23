string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(include/warpcore/config.h.in include/warpcore/config.h)
configure_file(src/config.c.in src/config.c)

include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

add_library(obj_all
  OBJECT
    src/plat.c src/util.c src/warpcore.c
    ${CMAKE_CURRENT_BINARY_DIR}/src/config.c
)

add_library(obj_sock OBJECT src/backend_sock.c)

add_library(sock_dyn SHARED $<TARGET_OBJECTS:obj_all> $<TARGET_OBJECTS:obj_sock>)
add_library(sock_stat STATIC $<TARGET_OBJECTS:obj_all> $<TARGET_OBJECTS:obj_sock>)

install(DIRECTORY include/warpcore
  DESTINATION include
  FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/warpcore/config.h
  DESTINATION include/warpcore
)

if(HAVE_NETMAP_H)
  add_library(obj_warp
    OBJECT
      src/arp.c src/eth.c src/icmp.c src/in_cksum.c src/ip.c
      src/udp.c src/backend_netmap.c
  )
  target_compile_definitions(obj_warp PRIVATE -DWITH_NETMAP)

  add_library(warp_dyn SHARED $<TARGET_OBJECTS:obj_all> $<TARGET_OBJECTS:obj_warp>)
  target_compile_definitions(warp_dyn PRIVATE -DWITH_NETMAP)

  add_library(warp_stat STATIC $<TARGET_OBJECTS:obj_all> $<TARGET_OBJECTS:obj_warp>)
  target_compile_definitions(warp_stat PRIVATE -DWITH_NETMAP)
endif()


set(TARGETS obj_all obj_sock sock_dyn sock_stat)
if(HAVE_NETMAP_H)
  set(TARGETS ${TARGETS} obj_warp warp_dyn warp_stat)
endif()
foreach(TARGET ${TARGETS})
  message(STATUS "set ${TARGET}")
  target_include_directories(${TARGET}
    SYSTEM PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE src
  )
  set_target_properties(${TARGET}
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      INTERPROCEDURAL_OPTIMIZATION True
      OUTPUT_NAME ${TARGET}
      VERSION ${PROJECT_VERSION}
      SOVERSION ${PROJECT_VERSION_MAJOR}
  )
  if(NOT ${TARGET} MATCHES "obj_")
    target_link_libraries(${TARGET} Threads::Threads)
    install(TARGETS ${TARGET}
      EXPORT ${TARGET}_config
      ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endforeach()

