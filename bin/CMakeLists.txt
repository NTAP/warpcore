include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})

if(HAVE_NETMAP_H)
  add_executable(warpping ping.c)
  target_compile_definitions(warpping PRIVATE -DWITH_NETMAP)
  target_link_libraries(warpping PUBLIC warpcore)

  add_executable(warpinetd inetd.c)
  target_compile_definitions(warpinetd PRIVATE -DWITH_NETMAP)
  target_link_libraries(warpinetd PUBLIC warpcore)

  install(TARGETS warpping warpinetd DESTINATION bin)
endif()

add_executable(shimping ping.c)
target_link_libraries(shimping PUBLIC shimcore)

add_executable(shiminetd inetd.c)
target_link_libraries(shiminetd PUBLIC shimcore)

install(TARGETS shimping shiminetd DESTINATION bin)
