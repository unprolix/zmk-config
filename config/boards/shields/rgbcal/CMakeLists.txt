zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

# Both halves need the strip driver and the relay behaviour: the central lights
# its own chain, the peripheral lights its own when told to.
zephyr_library_sources(rgbcal_strip.c)
zephyr_library_sources(rgbcal_behavior.c)

# The state machine and the typing only make sense where the keymap runs.
if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(rgbcal_run.c)
  zephyr_library_sources(rgbcal_type.c)
endif()
