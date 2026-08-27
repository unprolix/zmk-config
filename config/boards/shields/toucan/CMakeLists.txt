# The emblem toggle lives with the KEYBOARD, not with toucan_display, because
# both halves compile the same keymap: `&emblem_toggle` has to resolve on the
# display-less half too, and a devicetree node with no matching driver is a null
# behaviour binding that faults the moment the key is pressed.
#
# The driver itself is shared with the Rolio rather than copied -- it contains
# no Rolio specifics, only a call to the weak vista_emblem_next() that the
# status screen overrides. On the right half nothing overrides it and the key
# does nothing, which is the intent.
# drivers/behavior.h lives in ZMK's app include dir, which is not on a shield's
# include path by default. The left half happens to get it from toucan_display's
# CMakeLists; the right half has no display shield, so it must be asked for here
# or that half alone fails to compile.
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/../rolio/emblem_toggle.c)
