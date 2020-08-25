string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(include/warpcore/config.h.in include/warpcore/config.h)
configure_file(src/config.c.in src/config.c)

include(GNUInstallDirs)

add_library(obj_all OBJECT src/plat.c src/util.c src/ifaddr.c)

add_library(obj_sock OBJECT src/backend_sock.c src/warpcore.c)
add_library(sockcore ${CMAKE_CURRENT_BINARY_DIR}/src/config.c
            $<TARGET_OBJECTS:obj_all> $<TARGET_OBJECTS:obj_sock>)

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
      src/arp.c src/neighbor.c src/eth.c src/icmp4.c src/icmp6.c src/ip4.c
      src/ip6.c src/in_cksum.c src/udp.c src/backend_netmap.c src/warpcore.c
  )
  target_compile_definitions(obj_warp PRIVATE -DWITH_NETMAP)
  add_library(warpcore ${CMAKE_CURRENT_BINARY_DIR}/src/config.c
              $<TARGET_OBJECTS:obj_all> $<TARGET_OBJECTS:obj_warp>)
  target_compile_definitions(warpcore PRIVATE -DWITH_NETMAP)
endif()

set(TARGETS obj_all obj_sock sockcore)
if(HAVE_NETMAP_H)
  set(TARGETS ${TARGETS} obj_warp warpcore)
endif()
foreach(TARGET ${TARGETS})
  target_include_directories(${TARGET}
    SYSTEM PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_include_directories(${TARGET}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  )
  set_target_properties(${TARGET}
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      OUTPUT_NAME ${TARGET}
      VERSION ${PROJECT_VERSION}
      SOVERSION ${PROJECT_VERSION_MAJOR}
      INTERPROCEDURAL_OPTIMIZATION ${IPO}
      INTERFACE_LINK_LIBRARIES dl
  )
  if(NOT ${TARGET} MATCHES "obj_")
    if(DSYMUTIL AND BUILD_SHARED_LIBS)
      add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${DSYMUTIL} ARGS *${CMAKE_SHARED_LIBRARY_SUFFIX}
      )
    endif()
    install(TARGETS ${TARGET}
      EXPORT ${TARGET}_config
      ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endforeach()

