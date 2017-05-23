include(GNUInstallDirs)

# see https://cmake.org/Wiki/CMake_RPATH_handling#Always_full_RPATH
set(LIB ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH ${LIB})
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES ${LIB} isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
   set(CMAKE_INSTALL_RPATH ${LIB})
endif()

if(HAVE_NETMAP_H)
  add_executable(warpping ping.c)
  target_compile_definitions(warpping PRIVATE -DWITH_NETMAP)
  target_link_libraries(warpping PUBLIC warpcore)

  add_executable(warpinetd inetd.c)
  target_compile_definitions(warpping PRIVATE -DWITH_NETMAP)
  target_link_libraries(warpinetd PUBLIC warpcore)

  install(TARGETS warpping warpinetd DESTINATION bin)
endif()

add_executable(shimping ping.c)
target_link_libraries(shimping PUBLIC shimcore)

add_executable(shiminetd inetd.c)
target_link_libraries(shiminetd PUBLIC shimcore)

install(TARGETS shimping shiminetd DESTINATION bin)
