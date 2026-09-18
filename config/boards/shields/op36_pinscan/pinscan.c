/*
 * DIAGNOSTIC ONLY: find the OP36 key matrix by brute force.
 *
 * Both vendor firmwares give the same pin map, and with it neither direction
 * of the matrix shows a held key (driving columns and sensing rows, and the
 * reverse, over the GPIO shell). So stop trusting the map: every candidate pin
 * rests as an input with pull-down; each in turn is driven high and every
 * other pin is read. A key held on a diode matrix connects exactly one pair,
 * in one direction. The scan logs the set of connected pairs whenever it
 * changes, so pressing a key prints "+ P0.13 -> P1.09" and releasing it
 * prints the matching "-".
 *
 * Pairs already connected with no key held (pins tied together on the board)
 * are reported once as the baseline.
 *
 * Excluded: P0.18, which is nRESET when the reset pin is enabled -- driving it
 * would reboot the chip mid-scan.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/bitarray.h>

LOG_MODULE_REGISTER(pinscan, LOG_LEVEL_INF);

#define P0_PINS        32
#define P1_PINS        16
#define NRESET_P0_PIN  18
#define CANDIDATES_MAX (P0_PINS + P1_PINS)

/* Give the USB console time to enumerate before the first report. */
#define START_DELAY_MS 5000
/* Between sweeps. A key press lasts far longer than this. */
#define SWEEP_PERIOD_MS 50
/* After driving a pin high, before reading the others: the pull-downs are
   ~13 k and the traces are short, so a few hundred microseconds is plenty. */
#define SETTLE_US 300
/* Remind the log that the scan is alive, with the current pair count. */
#define HEARTBEAT_MS 10000

#define STACK_SIZE 2048
#define THREAD_PRIORITY 7

struct candidate {
    const struct device *port;
    uint8_t port_num;
    uint8_t pin;
};

static struct candidate candidates[CANDIDATES_MAX];
static size_t candidate_count;

/* connected[d][s]: driving d high reads s high. */
static bool connected[CANDIDATES_MAX][CANDIDATES_MAX];
static bool previous[CANDIDATES_MAX][CANDIDATES_MAX];

static void add_port(const struct device *port, uint8_t port_num, uint8_t pins) {
    for (uint8_t pin = 0; pin < pins; pin++) {
        if (port_num == 0 && pin == NRESET_P0_PIN) {
            continue;
        }
        candidates[candidate_count++] = (struct candidate){port, port_num, pin};
    }
}

static int rest(const struct candidate *c) {
    return gpio_pin_configure(c->port, c->pin, GPIO_INPUT | GPIO_PULL_DOWN);
}

static void sweep(void) {
    for (size_t d = 0; d < candidate_count; d++) {
        const struct candidate *drv = &candidates[d];
        if (gpio_pin_configure(drv->port, drv->pin, GPIO_OUTPUT_HIGH) != 0) {
            continue;
        }
        k_busy_wait(SETTLE_US);
        for (size_t s = 0; s < candidate_count; s++) {
            if (s == d) {
                connected[d][s] = false;
                continue;
            }
            const struct candidate *sns = &candidates[s];
            connected[d][s] = gpio_pin_get_raw(sns->port, sns->pin) > 0;
        }
        rest(drv);
    }
}

static size_t report_changes(bool baseline) {
    size_t total = 0;
    for (size_t d = 0; d < candidate_count; d++) {
        for (size_t s = 0; s < candidate_count; s++) {
            total += connected[d][s];
            if (connected[d][s] == previous[d][s]) {
                continue;
            }
            const struct candidate *drv = &candidates[d];
            const struct candidate *sns = &candidates[s];
            LOG_INF("%s P%u.%02u -> P%u.%02u", baseline ? "baseline" : (connected[d][s] ? "+" : "-"),
                    drv->port_num, drv->pin, sns->port_num, sns->pin);
            previous[d][s] = connected[d][s];
        }
    }
    return total;
}

static void pinscan_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    add_port(DEVICE_DT_GET(DT_NODELABEL(gpio0)), 0, P0_PINS);
    add_port(DEVICE_DT_GET(DT_NODELABEL(gpio1)), 1, P1_PINS);

    k_msleep(START_DELAY_MS);

    for (size_t i = 0; i < candidate_count; i++) {
        rest(&candidates[i]);
    }
    LOG_INF("pinscan: %u candidate pins; hold one key at a time", (unsigned)candidate_count);

    sweep();
    size_t pairs = report_changes(true);
    LOG_INF("pinscan: baseline has %u connected pairs", (unsigned)pairs);

    int64_t next_heartbeat = k_uptime_get() + HEARTBEAT_MS;
    for (;;) {
        k_msleep(SWEEP_PERIOD_MS);
        sweep();
        pairs = report_changes(false);
        if (k_uptime_get() >= next_heartbeat) {
            LOG_INF("pinscan: alive, %u connected pairs", (unsigned)pairs);
            next_heartbeat = k_uptime_get() + HEARTBEAT_MS;
        }
    }
}

K_THREAD_DEFINE(pinscan, STACK_SIZE, pinscan_thread, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
