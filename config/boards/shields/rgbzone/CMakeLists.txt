zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

# Both halves light their own strip; both need the relay behaviour, one to
# send and one to act on it.
zephyr_library_sources(rgbzone_strip.c)
zephyr_library_sources(rgbzone_relay.c)

# Deciding what a column is for needs the keymap, which only the central has.
if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(rgbzone_run.c)
endif()
