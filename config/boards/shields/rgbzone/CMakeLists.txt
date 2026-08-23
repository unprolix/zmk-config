zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

# Both halves light their own strip; both need the relay behaviour, one to
# send and one to act on it.
zephyr_library_sources(rgbzone_strip.c)
zephyr_library_sources(rgbzone_relay.c)
# Host-driven diagnostics; the relay half is needed on both sides.
zephyr_library_sources(rgbzone_diag.c)

# Deciding what a column is for needs the keymap, which only the central has --
# and so does the momentary-layer behaviour that records who opened a layer:
# a peripheral builds no keymap behaviours at all, ZMK's own included.
if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(rgbzone_run.c)
  zephyr_library_sources(rgbzone_owner.c)
  # Brightness is decided here and sent packed with the colours, so the far
  # half needs none of it.
  zephyr_library_sources(rgbzone_level.c)
endif()
