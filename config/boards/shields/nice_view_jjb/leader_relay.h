/*
 * Wire format for leader state crossing the split link.
 *
 * The leader behaviour runs only on the central, so the peripheral's display
 * would otherwise know nothing about a sequence in progress. ZMK will relay a
 * behaviour invocation to every peripheral if the behaviour declares GLOBAL
 * locality, and that invocation carries exactly two 32-bit parameters -- which
 * is the entire budget here.
 *
 * Names are far too big for that, so only sequence *indices* are sent; the
 * peripheral turns them back into words with zmk_leader_sequence_name(), from
 * the devicetree table both halves compile (see the zmk-leader-key fork).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

/* Four one-byte indices is all param2 holds. */
#define LEADER_RELAY_MAX_INDICES 4

/* param1 */
#define LEADER_RELAY_ACTIVE_BIT  BIT(31)
#define LEADER_RELAY_COUNT_SHIFT 0
#define LEADER_RELAY_COUNT_MASK  0xFFU
#define LEADER_RELAY_PRESS_SHIFT 8
#define LEADER_RELAY_PRESS_MASK  0xFFU
/* How many of the four index slots in param2 are populated. */
#define LEADER_RELAY_LISTED_SHIFT 16
#define LEADER_RELAY_LISTED_MASK  0xFU

/*
 * Sequence indices are sent as single bytes, so a configuration with more than
 * 255 sequences cannot name them all. Anything at or above this is sent as
 * LEADER_RELAY_INDEX_NONE and simply not listed.
 */
#define LEADER_RELAY_INDEX_NONE 0xFFU
#define LEADER_RELAY_INDEX_MAX  0xFEU

static inline uint32_t leader_relay_pack_param1(bool active, uint8_t candidate_count,
                                                uint8_t press_count, uint8_t listed) {
    return (active ? LEADER_RELAY_ACTIVE_BIT : 0) |
           ((uint32_t)(candidate_count & LEADER_RELAY_COUNT_MASK) << LEADER_RELAY_COUNT_SHIFT) |
           ((uint32_t)(press_count & LEADER_RELAY_PRESS_MASK) << LEADER_RELAY_PRESS_SHIFT) |
           ((uint32_t)(listed & LEADER_RELAY_LISTED_MASK) << LEADER_RELAY_LISTED_SHIFT);
}

static inline bool leader_relay_active(uint32_t param1) {
    return (param1 & LEADER_RELAY_ACTIVE_BIT) != 0;
}

static inline uint8_t leader_relay_count(uint32_t param1) {
    return (param1 >> LEADER_RELAY_COUNT_SHIFT) & LEADER_RELAY_COUNT_MASK;
}

static inline uint8_t leader_relay_press(uint32_t param1) {
    return (param1 >> LEADER_RELAY_PRESS_SHIFT) & LEADER_RELAY_PRESS_MASK;
}

static inline uint8_t leader_relay_listed(uint32_t param1) {
    return (param1 >> LEADER_RELAY_LISTED_SHIFT) & LEADER_RELAY_LISTED_MASK;
}

static inline uint8_t leader_relay_index(uint32_t param2, uint8_t slot) {
    return (param2 >> (8 * slot)) & 0xFFU;
}

static inline uint32_t leader_relay_set_index(uint32_t param2, uint8_t slot, uint8_t index) {
    return (param2 & ~(0xFFU << (8 * slot))) | ((uint32_t)index << (8 * slot));
}

/*
 * Implemented by the peripheral's status screen. Called from the relay
 * behaviour when an invocation arrives; a build whose screen does not want it
 * simply does not provide it (the behaviour is only compiled with the screen).
 */
void jjb_leader_relay_received(uint32_t param1, uint32_t param2);
