# The single-wire split link. Built on both halves -- the peripheral needs the
# driver just as much as the central does.
#
# This is a Zephyr serial driver living out-of-tree rather than a patch to
# zephyr/drivers/serial: config/ is already a DTS_ROOT, so its binding resolves
# from here, and keeping it in the workspace means the pinned Zephyr stays
# untouched and updatable.
zephyr_library_sources(uart_pio_half_duplex.c)

# Held-at-power-up bootloader escape. Built on both halves, and on the
# peripheral it is the ONLY route into the bootloader that does not depend on
# the split link working -- which, with no reachable BOOT button on this board,
# is the difference between a bad flash being an inconvenience and being a
# disassembly job.
zephyr_library_sources(boot_key.c)

# ZMK headers (zmk/..., drivers/behavior.h) for the shared listener below.
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
# Re-arms a one-shot Shift after a Backspace (see ../common/sticky_shift_redo.c).
# Shared by every keyboard here; on a peripheral the file compiles to nothing,
# since only a central has keycode events to listen to.
zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/../common/sticky_shift_redo.c)
