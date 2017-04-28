include(CheckFunctionExists)
check_function_exists(recvmmsg HAVE_RECVMMSG)
check_function_exists(sendmmsg HAVE_SENDMMSG)

string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UC)
configure_file(include/warpcore/config.h.in include/warpcore/config.h)
configure_file(src/config.c.in src/config.c)

include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")


set(SOURCES ${SOURCES} src/plat.c src/util.c src/warpcore.c
    ${CMAKE_CURRENT_BINARY_DIR}/src/config.c)

set(SHIMSOURCES ${SOURCES} src/backend_shim.c)
add_library(shimcore SHARED ${SHIMSOURCES})
set_property(TARGET shimcore
             PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
target_include_directories(shimcore
                           PUBLIC
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
                             $<INSTALL_INTERFACE:include>
                           PRIVATE src)
add_library(shimcore_static STATIC ${SHIMSOURCES})
set_target_properties(shimcore_static PROPERTIES OUTPUT_NAME shimcore)
set_property(TARGET shimcore_static
             PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
target_include_directories(shimcore_static
                           PUBLIC
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
                             $<INSTALL_INTERFACE:include>
                           PRIVATE src)

install(TARGETS shimcore EXPORT shimcore_config
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS shimcore_static EXPORT shimcore_static_config
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
  target_compile_definitions(warpcore PRIVATE -DWITH_NETMAP)
  set_property(TARGET warpcore
               PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
  target_include_directories(warpcore
                             PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                             PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include
                             PRIVATE src)
  add_library(warpcore_static STATIC ${WARPSOURCES})
  set_target_properties(warpcore_static PROPERTIES OUTPUT_NAME warpcore)
  target_compile_definitions(warpcore_static PRIVATE -DWITH_NETMAP)
  set_property(TARGET warpcore_static
               PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
  target_include_directories(warpcore_static
                             PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                             PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include
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

