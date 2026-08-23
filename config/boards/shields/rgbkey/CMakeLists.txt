zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

# Both halves paint their own keys; both need the relay behaviour, one to send
# it and one to act on it.
zephyr_library_sources(rgbkey_strip.c)
zephyr_library_sources(rgbkey_relay.c)

# Working out the active layer needs the keymap, which only the central has.
if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(rgbkey_run.c)
endif()
