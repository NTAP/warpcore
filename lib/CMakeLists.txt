string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(include/warpcore/config.h.in include/warpcore/config.h)
configure_file(src/config.c.in src/config.c)

include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

set(SOURCES ${SOURCES} src/plat.c src/util.c src/warpcore.c
    ${CMAKE_CURRENT_BINARY_DIR}/src/config.c)

set(SOCKSOURCES ${SOURCES} src/backend_sock.c)
add_library(sockcore SHARED ${SOCKSOURCES})
set_target_properties(sockcore
                      PROPERTIES
                        INTERPROCEDURAL_OPTIMIZATION True
                        OUTPUT_NAME sockcore
                        VERSION ${PROJECT_VERSION}
                        SOVERSION ${PROJECT_VERSION_MAJOR})
target_include_directories(sockcore
                           SYSTEM PUBLIC
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
                             $<INSTALL_INTERFACE:include>
                           PRIVATE src)
target_link_libraries(sockcore Threads::Threads)
install(TARGETS sockcore EXPORT sockcore_config
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})


add_library(sockcore_static STATIC ${SOCKSOURCES})
set_target_properties(sockcore_static
                      PROPERTIES
                        INTERPROCEDURAL_OPTIMIZATION True
                        OUTPUT_NAME sockcore
                        VERSION ${PROJECT_VERSION}
                        SOVERSION ${PROJECT_VERSION_MAJOR})
target_link_libraries(sockcore_static Threads::Threads)
target_include_directories(sockcore_static
                           SYSTEM PUBLIC
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
                             $<INSTALL_INTERFACE:include>
                           PRIVATE src)
install(TARGETS sockcore_static EXPORT sockcore_static_config
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/warpcore
        DESTINATION include
        FILES_MATCHING PATTERN "*.h")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/warpcore/config.h
        DESTINATION include/warpcore)

if(HAVE_NETMAP_H)
  set(WARPSOURCES
      ${SOURCES} src/arp.c src/eth.c src/icmp.c src/in_cksum.c src/ip.c
      src/udp.c src/backend_netmap.c)
  add_library(warpcore SHARED ${WARPSOURCES})
  target_link_libraries(warpcore Threads::Threads)
  set_target_properties(warpcore
                        PROPERTIES
                          INTERPROCEDURAL_OPTIMIZATION True
                          OUTPUT_NAME warpcore
                          VERSION ${PROJECT_VERSION}
                          SOVERSION ${PROJECT_VERSION_MAJOR})
  target_compile_definitions(warpcore PRIVATE -DWITH_NETMAP)
  target_include_directories(warpcore
                             SYSTEM PUBLIC
                               ${CMAKE_CURRENT_SOURCE_DIR}/include
                               ${CMAKE_CURRENT_BINARY_DIR}/include
                             PRIVATE src)
  add_library(warpcore_static STATIC ${WARPSOURCES})
  target_link_libraries(warpcore_static Threads::Threads)
  set_target_properties(warpcore_static
                        PROPERTIES
                          INTERPROCEDURAL_OPTIMIZATION True
                          OUTPUT_NAME warpcore
                          VERSION ${PROJECT_VERSION}
                          SOVERSION ${PROJECT_VERSION_MAJOR})
  target_compile_definitions(warpcore_static PRIVATE -DWITH_NETMAP)
  target_include_directories(warpcore_static
                             SYSTEM PUBLIC
                               ${CMAKE_CURRENT_SOURCE_DIR}/include
                               ${CMAKE_CURRENT_BINARY_DIR}/include
                             PRIVATE src)
  install(TARGETS warpcore EXPORT warpcore_config
          ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(TARGETS warpcore_static EXPORT warpcore_static_config
          ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

