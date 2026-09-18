if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  # ZMK's own headers (zmk/display/...) live in the app's include dir, which is
  # not on a shield's include path by default. Set here rather than relying on
  # another shield on the same build line to have done it.
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

  zephyr_library_sources(qube_status_screen.c)

  # Strong Glasgow at 40px, 4bpp, for the layer name. Generated with
  # lv_font_conv from ~/.fonts/StrongGlasgowRegular-ypAZ.ttf over a deliberately
  # narrow range -- lowercase, digits, space, hyphen -- because layer names are
  # all it renders. That is 8.6 KB of glyphs instead of 60 KB for full ASCII.
  zephyr_library_sources(glasgow_40.c)

  # Borrowed, not copied: the hierophant emblem bitmap and the line-art
  # modifier glyphs live with the Rolio's screen, which also serves the Toucan.
  # vista_canvas.c has no panel geometry compiled in, so it draws onto the
  # Qube's canvas as readily as onto a Sharp panel's.
  zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../vista508)
  zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/../vista508/vista_canvas.c)

  # Host names for bluetooth profiles. The Qube is always the central.
  zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../common)
  zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/../common/bt_names.c)
endif()
