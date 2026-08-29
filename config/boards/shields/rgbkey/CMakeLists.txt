zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)

# Both halves paint their own keys; both need the relay behaviour, one to send
# it and one to act on it.
zephyr_library_sources(rgbkey_strip.c)
zephyr_library_sources(rgbkey_relay.c)

# Working out the active layer needs the keymap, which only the central has.
if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(rgbkey_run.c)
endif()

# The holding-hand mechanism, shared with rgbzone rather than reimplemented:
# one small file that records which key opened a momentary layer, plus the
# behaviour jjb.keymap retargets &mo at. Referenced by relative path, the way
# the toucan shield borrows the rolio's emblem toggle.
#
# CENTRAL ONLY, for the same reason rgbzone builds it there: it replaces a
# keymap behaviour, and a peripheral builds no keymap behaviours at all, so on
# that half it fails to link against zmk_keymap_layer_activate. The peripheral
# does not need it anyway -- the holding side reaches it in the packed word.
#
# Guarded against rgbzone too, which would otherwise compile the same file into
# the same image and collide at link time. The two are never used together
# today -- rgbzone is the eyelash's, rgbkey the Rolio's and Corne v4's -- but
# nothing stops someone naming both on a build line.
if((NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
   AND NOT CONFIG_SHIELD_RGBZONE
   AND (CONFIG_SHIELD_CORNE_V4_LEFT OR CONFIG_SHIELD_CORNE_V4_RIGHT))
  zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/../rgbzone/rgbzone_owner.c)
endif()
