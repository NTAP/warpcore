if (${netmap_FOUND})
  add_executable(warpping warpping.c)
  target_link_libraries(warpping warpcore)

  add_executable(warpinetd warpinetd.c)
  target_link_libraries(warpinetd warpcore)
endif()

add_executable(shimping warpping.c)
target_link_libraries(shimping shimcore)

add_executable(shiminetd warpinetd.c)
target_link_libraries(shiminetd shimcore)
