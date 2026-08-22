# Central only. RAW_HID depends on !ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL, and
# the peripheral side needs nothing: it already knows how to run a relayed
# &bootloader.
if(CONFIG_RAW_HID)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(dfu_hid.c)
endif()
