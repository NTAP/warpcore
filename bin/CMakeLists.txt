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
  foreach(TARGET ping inetd)
    add_executable(warp${TARGET} ${TARGET}.c)
    target_compile_definitions(warp${TARGET} PRIVATE -DWITH_NETMAP)
    target_link_libraries(warp${TARGET} PUBLIC warpcore)
    install(TARGETS warp${TARGET} DESTINATION bin)
    if(DSYMUTIL)
      add_custom_command(TARGET warp${TARGET} POST_BUILD
        COMMAND ${DSYMUTIL} ARGS $<TARGET_FILE:warp${TARGET}>
      )
    endif()
  endforeach()
endif()

foreach(TARGET ping inetd)
  add_executable(sock${TARGET} ${TARGET}.c)
  target_link_libraries(sock${TARGET} PUBLIC sockcore)
  install(TARGETS sock${TARGET} DESTINATION bin)
  if(DSYMUTIL)
    add_custom_command(TARGET sock${TARGET} POST_BUILD
      COMMAND ${DSYMUTIL} ARGS $<TARGET_FILE:sock${TARGET}>
    )
  endif()
endforeach()
