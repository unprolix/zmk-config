# spin_speed.c includes <drivers/behavior.h>, which lives in ZMK's app/include
# rather than on a shield's default include path.
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

# The rotation-speed keys live here rather than with the qube_display shield
# they drive. All three parts compile the same keymap, so `&spin_faster` has to
# resolve on the halves too -- and a devicetree node with no matching driver is
# a null binding, which faults the moment the key is pressed. Building it
# everywhere, against a weak no-op, keeps the three in step.
zephyr_library_sources(spin_speed.c)

# Re-arms a one-shot Shift after a Backspace (see ../common/sticky_shift_redo.c).
# Shared by every keyboard here; on a peripheral the file compiles to nothing,
# since only a central has keycode events to listen to.
zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/../common/sticky_shift_redo.c)
