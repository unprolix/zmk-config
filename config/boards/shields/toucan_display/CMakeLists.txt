# The Toucan's panel is the same 144x168 Sharp memory-in-pixel part as the
# Rolio's Vista508, so it gets the same status screen rather than a second
# copy of one. The sources live under ../vista508 because that is where they
# were written; nothing in them is Rolio-specific (they draw through LVGL and
# never touch the panel or its bus), only the names are.
if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  set(VISTA_DIR ${CMAKE_CURRENT_LIST_DIR}/../vista508)
  # ZMK's own headers (zmk/display/...) live in the app's include dir, which is
  # not on a shield's include path by default. The second entry is for
  # vista_canvas.h and hierophant_img.h, which sit beside the sources.
  # The third entry is ../common, for bt_names.h, which the shared screen
  # includes. Anything custom_status_screen.c starts depending on has to be
  # added HERE as well as in vista508/CMakeLists.txt -- this shield builds the
  # same source but not the same CMakeLists, which is how montserrat_18 broke
  # the Toucan on 2026-08-27.
  set(COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/../common)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include ${VISTA_DIR} ${COMMON_DIR})
  zephyr_library_sources(${VISTA_DIR}/custom_status_screen.c)
  zephyr_library_sources(${VISTA_DIR}/vista_canvas.c)

  # Naming the host on a bluetooth profile; central only, since a peripheral is
  # bonded to the central rather than to a host.
  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(${COMMON_DIR}/bt_names.c)
  endif()
endif()
