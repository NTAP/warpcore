string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(include/warpcore/config.h.in include/warpcore/config.h)
configure_file(src/config.c.in src/config.c)

include(GNUInstallDirs)

add_library(roaring OBJECT
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>/lib/deps/roaring/roaring.c)

set_target_properties(roaring
  PROPERTIES
    C_CLANG_TIDY ""
    C_CPPCHECK ""
    C_INCLUDE_WHAT_YOU_USE ""
    COMPILE_FLAGS "-Wno-reserved-id-macro -Wno-sign-conversion -Wno-undef \
                   -Wno-implicit-int-conversion -Wno-padded -Wno-cast-align \
                   -Wno-documentation-unknown-command -Wno-missing-prototypes \
                   -Wno-unreachable-code-return -Wno-cast-qual \
                   -Wno-unknown-warning-option"
)

add_library(obj_all OBJECT src/plat.c src/util.c src/ifaddr.c)
add_library(obj_sock OBJECT src/backend_sock.c src/warpcore.c)
add_library(sockcore ${CMAKE_CURRENT_BINARY_DIR}/src/config.c
            $<TARGET_OBJECTS:obj_all> $<TARGET_OBJECTS:obj_sock>
            $<TARGET_OBJECTS:roaring>)

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
              $<TARGET_OBJECTS:obj_all> $<TARGET_OBJECTS:obj_warp>
              $<TARGET_OBJECTS:roaring>)
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
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    PRIVATE
      $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/lib/deps/roaring>
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
