add_executable(warpping warpping.c)
target_link_libraries(warpping warpcore)

add_executable(warpinetd warpinetd.c)
target_link_libraries(warpinetd warpcore)
