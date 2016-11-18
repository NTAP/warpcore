string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME_LC)
set(PLAT ${CMAKE_CURRENT_SOURCE_DIR}/plat_${CMAKE_SYSTEM_NAME_LC}.c)
if(NOT(EXISTS ${PLAT}))
  set(PLAT ${CMAKE_CURRENT_SOURCE_DIR}/plat_generic.c)
endif()

if (${netmap_FOUND})
  add_executable(warpping warpping.c ${PLAT})
  target_link_libraries(warpping warpcore)

  add_executable(warpinetd warpinetd.c ${PLAT})
  target_link_libraries(warpinetd warpcore)
endif()

add_executable(shimping warpping.c ${PLAT})
target_link_libraries(shimping shimcore)

add_executable(shiminetd warpinetd.c ${PLAT})
target_link_libraries(shiminetd shimcore)
