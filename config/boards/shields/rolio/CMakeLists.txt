# emblem_toggle.c includes <drivers/behavior.h>, which lives in ZMK's app/include
# rather than on the default shield include path. The rgbkey shield sets this too,
# which is why rolio_*-rgbkey built while the PLAIN rolio_right did not -- both
# shields contribute to the same Zephyr library, so rgbkey was supplying the path
# on rolio's behalf whenever it happened to be on the build line.
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

if(CONFIG_ZMK_RGB_UNDERGLOW)
  zephyr_library_sources(rolio_underglow_blank.c)
endif()

# The emblem toggle lives here rather than with the vista508 display it drives.
# The keymap is shared by both halves, so `&emblem_toggle` has to resolve on the
# display-less half too -- and a devicetree node with no matching driver is a
# null behaviour binding, which faults the moment the key is pressed. Building
# it on both halves, against a weak no-op, keeps the two in step.
zephyr_library_sources(emblem_toggle.c)
