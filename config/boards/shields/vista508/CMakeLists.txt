if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  # ZMK's own headers (zmk/display/...) live in the app's include dir, which is
  # not on a shield's include path by default.
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(custom_status_screen.c)
  zephyr_library_sources(vista_canvas.c)

  # Naming the host on a bluetooth profile. Shared with nice_view_jjb -- the
  # code is BLE and settings, with no display in it, so it has no business
  # living inside either screen's shield.
  #
  # Central only: a peripheral half is bonded to the central, not to a host, so
  # it has nothing to name. The Rolio only puts this shield on its left half
  # anyway, but the Toucan builds this same screen and the guard keeps that
  # honest.
  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../common)
    zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/../common/bt_names.c)
  endif()
endif()
