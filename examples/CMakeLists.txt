if (${netmap_FOUND})
  add_executable(warpping warpping.c)
  target_link_libraries(warpping PUBLIC warpcore)

  add_executable(warpinetd warpinetd.c)
  target_link_libraries(warpinetd PUBLIC warpcore)

  install(TARGETS warpping warpinetd DESTINATION bin)
endif()

add_executable(shimping warpping.c)
target_link_libraries(shimping PUBLIC shimcore)

add_executable(shiminetd warpinetd.c)
target_link_libraries(shiminetd PUBLIC shimcore)

install(TARGETS shimping shiminetd DESTINATION bin)
