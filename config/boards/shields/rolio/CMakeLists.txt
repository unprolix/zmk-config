if(CONFIG_ZMK_RGB_UNDERGLOW)
  zephyr_library_sources(rolio_underglow_blank.c)
endif()

# The emblem toggle lives here rather than with the vista508 display it drives.
# The keymap is shared by both halves, so `&emblem_toggle` has to resolve on the
# display-less half too -- and a devicetree node with no matching driver is a
# null behaviour binding, which faults the moment the key is pressed. Building
# it on both halves, against a weak no-op, keeps the two in step.
zephyr_library_sources(emblem_toggle.c)
