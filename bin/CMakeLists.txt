# see https://cmake.org/Wiki/CMake_RPATH_handling#Always_full_RPATH
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(LIB ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES ${LIB} isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
  set(CMAKE_INSTALL_RPATH ${LIB})
endif()

if(HAVE_NETMAP_H)
  add_executable(warpping ping.c)
  target_compile_definitions(warpping PRIVATE -DWITH_NETMAP)
  target_link_libraries(warpping PUBLIC warp)

  add_executable(warpinetd inetd.c)
  target_compile_definitions(warpinetd PRIVATE -DWITH_NETMAP)
  target_link_libraries(warpinetd PUBLIC warp)

  install(TARGETS warpping warpinetd DESTINATION bin)
endif()

add_executable(sockping ping.c)
target_link_libraries(sockping PUBLIC sock)

add_executable(sockinetd inetd.c)
target_link_libraries(sockinetd PUBLIC sock)

install(TARGETS sockping sockinetd DESTINATION bin)

if(DSYMUTIL)
  foreach(TARGET sockping warpping sockinetd warpinetd)
    add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${DSYMUTIL} ARGS $<TARGET_FILE:${TARGET}>
    )
  endforeach()
endif()
