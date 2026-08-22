if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  # ZMK's own headers (zmk/display/...) live in the app's include dir, which is
  # not on a shield's include path by default.
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

  # Only one of these may define zmk_display_status_screen(). The two halves
  # have entirely different things worth showing: the central knows the layer,
  # the endpoint and both batteries, while the peripheral knows only its own
  # battery and whether the link is up.
  # Rotation machinery, needed by whichever screen is built.
  zephyr_library_sources(canvas_util.c)

  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(custom_status_screen.c)
  else()
    zephyr_library_sources(peripheral_status_screen.c)
  endif()
endif()
