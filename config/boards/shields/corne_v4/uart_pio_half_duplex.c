/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Single-wire half-duplex UART on an RP2040 PIO block.
 *
 * The Corne v4 gives its two halves one data conductor between them, so the
 * same GPIO has to drive the line and sample it. Zephyr's stock PIO UART
 * (drivers/serial/uart_rpi_pico_pio.c) assumes a separate pin for each
 * direction and calls pio_sm_set_pindirs_with_mask() once at init to make the
 * transmit pin an output forever, so aiming both of its pins at one GPIO just
 * leaves that GPIO stuck in whichever direction happened to initialise last.
 *
 * The two PIO programs below are the stock driver's, unchanged -- what differs
 * is the ownership of the pin. Exactly one state machine runs at a time:
 *
 *   receiving (the resting state)  RX enabled, pin an input, held high by its
 *                                  pull-up, TX stopped
 *   transmitting                   RX stopped, pin an output driven by TX
 *
 * Handing the line back happens on whichever comes first: the next read, or
 * the turnaround timer. The read covers the normal request/response exchange,
 * where the sender goes straight back to listening for the reply. The timer
 * covers the half that has answered and then has nothing to say, which would
 * otherwise sit on the line as an output until its own next read tick and eat
 * the front of the other half's next message.
 *
 * Only the polling API is implemented, which is all that
 * CONFIG_ZMK_SPLIT_WIRED_UART_MODE_POLLING calls.
 *
 * THE TWO ENTRY POINTS RUN IN DIFFERENT CONTEXTS, and getting this wrong cost
 * a day. uart_poll_out() is reached from a k_work (thread context), but
 * uart_poll_in() is reached from ZMK's read timer -- a K_TIMER_DEFINE expiry
 * function, which is an ISR. So:
 *
 *   - the lock is a spinlock, not a mutex. Zephyr's k_mutex_lock() asserts
 *     !arch_is_in_isr(); with asserts compiled out it does not fail loudly, it
 *     pends the current thread from an ISR and quietly corrupts the scheduler.
 *     The symptom is a link that works for a few seconds and then stops dead,
 *     with both byte counters frozen mid-conversation.
 *   - poll_in() never blocks. It cannot wait for a transmission to drain,
 *     because it may not busy-wait in an ISR, so while transmitting it simply
 *     reports "no data" and lets the turnaround work -- which does run in a
 *     thread -- hand the line back a couple of hundred microseconds later.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>

#include <hardware/clocks.h>
#include <hardware/pio.h>

#define DT_DRV_COMPAT zmk_pico_uart_pio_half_duplex

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* PIO cycles the programs below spend on one bit. */
#define CYCLES_PER_BIT 8
/* Side-set bits the transmit program declares. */
#define SIDESET_BIT_COUNT 2
/* Start bit + eight data bits + stop bit. */
#define BITS_PER_FRAME 10
/*
 * Bit times to wait after the transmit FIFO drains before the shift register
 * is certainly empty too: one frame still in flight, plus a margin so the stop
 * bit is fully on the wire before the pin becomes an input again.
 */
#define TX_DRAIN_BITS (BITS_PER_FRAME + 2)

#define USEC_PER_SEC_F 1000000.0f

struct pio_uart_hd_config {
	const struct device *piodev;
	const struct pinctrl_dev_config *pcfg;
	uint32_t pin;
	uint32_t baudrate;
	uint32_t turnaround_delay_us;
	uint32_t tx_drain_us;
};

struct pio_uart_hd_data {
	size_t tx_sm;
	size_t rx_sm;
	uint32_t tx_offset;
	uint32_t rx_offset;
	bool transmitting;
	/* Spinlock, not a mutex: poll_in() is called from an ISR. See the header. */
	struct k_spinlock lock;
	struct k_work_delayable turnaround;
	const struct device *dev;
	/*
	 * Bring-up instrumentation. The split protocol is silent about where it
	 * broke -- the peripheral answers only a CRC-valid poll, so "nothing
	 * happens" covers a dead wire, a wrong baud, a turnaround that never
	 * flips, and a CRC mismatch equally well. These two counters separate
	 * them: bytes leaving one half and never arriving at the other is a very
	 * different fault from bytes arriving and being rejected.
	 */
	uint32_t tx_bytes;
	uint32_t rx_bytes;
	uint32_t reported_tx;
	uint32_t reported_rx;
	/*
	 * Which half of the machine has stopped. Byte counts alone cannot say:
	 * "tx stopped climbing" covers both "ZMK stopped asking us to send" and
	 * "ZMK stopped running at all". poll_in is driven by ZMK's read timer
	 * and poll_out by its tx work, so counting calls to each separates a
	 * dead transmit path from a dead transport. sessions/turnarounds pair
	 * up: if they ever diverge by more than one, the line has been left
	 * driven and the other half cannot get a word in.
	 */
	uint32_t poll_in_calls;
	uint32_t tx_sessions;
	uint32_t turnarounds;
	/*
	 * Times the state machine tried to push a byte into a full RX FIFO, i.e.
	 * bytes silently lost. PIO reports this in FDEBUG_RXSTALL, and it is the
	 * difference between "the wire is bad" and "the wire is fine and we are
	 * too slow to empty an 8-byte FIFO" -- which look identical from the
	 * protocol's side, both arriving as an unparseable part of a message.
	 */
	uint32_t rx_overflows;
	struct k_work_delayable report;
};

/*
 * How often the bring-up counters are printed. Long enough to read, short
 * enough that a link which works only intermittently still shows up.
 */
#define REPORT_INTERVAL_MS 2000

RPI_PICO_PIO_DEFINE_PROGRAM(uart_hd_tx, 0, 3,
		/* .wrap_target */
	0x9fa0, /*  0: pull   block           side 1 [7]  */
	0xf727, /*  1: set    x, 7            side 0 [7]  */
	0x6001, /*  2: out    pins, 1                     */
	0x0642, /*  3: jmp    x--, 2                 [6]  */
		/* .wrap */
);

RPI_PICO_PIO_DEFINE_PROGRAM(uart_hd_rx, 1, 8,
	0x20a0, /*  0: wait   1 pin, 0                    */
		/* .wrap_target */
	0x2020, /*  1: wait   0 pin, 0                    */
	0xea27, /*  2: set    x, 7                   [10] */
	0x4001, /*  3: in     pins, 1                     */
	0x0643, /*  4: jmp    x--, 3                 [6]  */
	0x00c8, /*  5: jmp    pin, 8                      */
	0xc014, /*  6: irq    nowait 4 rel                */
	0x0000, /*  7: jmp    0                           */
	0x8020, /*  8: push   block                       */
		/* .wrap */
);

/*
 * Both programs are loaded once and left in PIO instruction memory; only the
 * enable bit and the pin direction move at runtime, because reloading a
 * program on every turnaround would cost far more than a frame time.
 */
static int pio_uart_hd_tx_load(PIO pio, uint32_t sm, uint32_t pin, float div, uint32_t *offset)
{
	pio_sm_config sm_config;

	if (!pio_can_add_program(pio, RPI_PICO_PIO_GET_PROGRAM(uart_hd_tx))) {
		return -EBUSY;
	}

	*offset = pio_add_program(pio, RPI_PICO_PIO_GET_PROGRAM(uart_hd_tx));
	sm_config = pio_get_default_sm_config();

	sm_config_set_sideset(&sm_config, SIDESET_BIT_COUNT, true, false);
	sm_config_set_out_shift(&sm_config, true, false, 0);
	sm_config_set_out_pins(&sm_config, pin, 1);
	sm_config_set_sideset_pins(&sm_config, pin);
	sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX);
	sm_config_set_clkdiv(&sm_config, div);
	sm_config_set_wrap(&sm_config, *offset + RPI_PICO_PIO_GET_WRAP_TARGET(uart_hd_tx),
			   *offset + RPI_PICO_PIO_GET_WRAP(uart_hd_tx));

	/*
	 * Park the output register high so that the moment the pin becomes an
	 * output it presents an idle line rather than a start bit.
	 */
	pio_sm_set_pins_with_mask(pio, sm, BIT(pin), BIT(pin));
	pio_sm_init(pio, sm, *offset, &sm_config);

	return 0;
}

static int pio_uart_hd_rx_load(PIO pio, uint32_t sm, uint32_t pin, float div, uint32_t *offset)
{
	pio_sm_config sm_config;

	if (!pio_can_add_program(pio, RPI_PICO_PIO_GET_PROGRAM(uart_hd_rx))) {
		return -EBUSY;
	}

	*offset = pio_add_program(pio, RPI_PICO_PIO_GET_PROGRAM(uart_hd_rx));
	sm_config = pio_get_default_sm_config();

	sm_config_set_in_pins(&sm_config, pin);
	sm_config_set_jmp_pin(&sm_config, pin);
	sm_config_set_in_shift(&sm_config, true, false, 0);
	sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_RX);
	sm_config_set_clkdiv(&sm_config, div);
	sm_config_set_wrap(&sm_config, *offset + RPI_PICO_PIO_GET_WRAP_TARGET(uart_hd_rx),
			   *offset + RPI_PICO_PIO_GET_WRAP(uart_hd_rx));

	pio_sm_init(pio, sm, *offset, &sm_config);

	return 0;
}

/* Give the line to the receiver. Caller holds the lock. */
static void pio_uart_hd_listen(const struct device *dev)
{
	const struct pio_uart_hd_config *config = dev->config;
	struct pio_uart_hd_data *data = dev->data;
	PIO pio = pio_rpi_pico_get_pio(config->piodev);

	pio_sm_set_enabled(pio, data->tx_sm, false);
	pio_sm_set_pindirs_with_mask(pio, data->tx_sm, 0, BIT(config->pin));

	/*
	 * Restart rather than merely re-enable: the receive program has to
	 * resume at its first instruction, which waits for an idle-high line,
	 * so that it synchronises on a real start bit instead of latching onto
	 * whatever the line was doing mid-turnaround.
	 */
	pio_sm_clear_fifos(pio, data->rx_sm);
	pio_sm_restart(pio, data->rx_sm);
	pio_sm_exec(pio, data->rx_sm, pio_encode_jmp(data->rx_offset));
	pio_sm_set_enabled(pio, data->rx_sm, true);

	data->transmitting = false;
}

/* Take the line for transmission. Caller holds the lock. */
static void pio_uart_hd_talk(const struct device *dev)
{
	const struct pio_uart_hd_config *config = dev->config;
	struct pio_uart_hd_data *data = dev->data;
	PIO pio = pio_rpi_pico_get_pio(config->piodev);

	pio_sm_set_enabled(pio, data->rx_sm, false);

	/* Drive high before switching direction, so no start bit is faked. */
	pio_sm_set_pins_with_mask(pio, data->tx_sm, BIT(config->pin), BIT(config->pin));
	pio_sm_set_pindirs_with_mask(pio, data->tx_sm, BIT(config->pin), BIT(config->pin));

	pio_sm_clear_fifos(pio, data->tx_sm);
	pio_sm_restart(pio, data->tx_sm);
	pio_sm_exec(pio, data->tx_sm, pio_encode_jmp(data->tx_offset));
	pio_sm_set_enabled(pio, data->tx_sm, true);

	data->transmitting = true;
	data->tx_sessions++;
}

/* Wait out the bytes still on the wire, then listen. Caller holds the lock. */
static void pio_uart_hd_finish_tx(const struct device *dev)
{
	const struct pio_uart_hd_config *config = dev->config;
	struct pio_uart_hd_data *data = dev->data;
	PIO pio = pio_rpi_pico_get_pio(config->piodev);

	while (!pio_sm_is_tx_fifo_empty(pio, data->tx_sm)) {
		/* The FIFO is being emptied by the state machine, not by us. */
	}
	k_busy_wait(config->tx_drain_us);

	pio_uart_hd_listen(dev);
	data->turnarounds++;
}

/*
 * Printed at INF, on a timer, rather than per turnaround at DBG. Per-turnaround
 * logging is hopeless here: the central polls every 15ms, and the log backend
 * cannot keep up with 66 lines a second -- the boot backlog alone dropped 577
 * messages and buried the driver's own init line. Totals every couple of
 * seconds say everything a per-event line would.
 *
 *   tx flat at 0     the central never called poll_out; the fault is above this
 *                    driver, in transport selection, not on the wire
 *   tx rising, rx 0  we are talking and hearing nothing: wire, pin, or baud
 *   both rising      the wire works; anything still broken is framing or CRC
 */
static void pio_uart_hd_report(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct pio_uart_hd_data *data = CONTAINER_OF(dwork, struct pio_uart_hd_data, report);

	LOG_INF("half-duplex split: tx=%u (+%u) rx=%u (+%u) in_calls=%u sessions=%u "
		"turnarounds=%u txing=%d rxovf=%u",
		data->tx_bytes, data->tx_bytes - data->reported_tx, data->rx_bytes,
		data->rx_bytes - data->reported_rx, data->poll_in_calls, data->tx_sessions,
		data->turnarounds, (int)data->transmitting, data->rx_overflows);

	data->reported_tx = data->tx_bytes;
	data->reported_rx = data->rx_bytes;

	k_work_reschedule(&data->report, K_MSEC(REPORT_INTERVAL_MS));
}

static void pio_uart_hd_turnaround_expired(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct pio_uart_hd_data *data = CONTAINER_OF(dwork, struct pio_uart_hd_data, turnaround);
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (data->transmitting) {
		pio_uart_hd_finish_tx(data->dev);
	}

	k_spin_unlock(&data->lock, key);
}

static int pio_uart_hd_poll_in(const struct device *dev, unsigned char *c)
{
	const struct pio_uart_hd_config *config = dev->config;
	struct pio_uart_hd_data *data = dev->data;
	PIO pio = pio_rpi_pico_get_pio(config->piodev);
	io_rw_8 *rx_fifo_msb;
	int ret = -1;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	data->poll_in_calls++;

	/*
	 * Write-1-to-clear, so each overflow is counted once. Checked here rather
	 * than in the report so that a burst between reports is not missed.
	 */
	if (pio->fdebug & (1u << (PIO_FDEBUG_RXSTALL_LSB + data->rx_sm))) {
		pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + data->rx_sm);
		data->rx_overflows++;
	}

	/*
	 * Still driving the line. There is nothing to read yet and this is an
	 * ISR, so it must not wait for the transmission to drain -- the
	 * turnaround work will hand the line back shortly. Reporting "no data"
	 * is exactly right: the caller polls again.
	 */
	if (data->transmitting) {
		k_spin_unlock(&data->lock, key);
		return -1;
	}

	/* The rx FIFO is 4 bytes wide; the byte lands in the most significant. */
	rx_fifo_msb = (io_rw_8 *)&pio->rxf[data->rx_sm] + 3;
	if (!pio_sm_is_rx_fifo_empty(pio, data->rx_sm)) {
		/* Reading the FIFO pops the word. */
		*c = (unsigned char)*rx_fifo_msb;
		data->rx_bytes++;
		ret = 0;
	}

	k_spin_unlock(&data->lock, key);
	return ret;
}

static void pio_uart_hd_poll_out(const struct device *dev, unsigned char c)
{
	const struct pio_uart_hd_config *config = dev->config;
	struct pio_uart_hd_data *data = dev->data;
	PIO pio = pio_rpi_pico_get_pio(config->piodev);
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (!data->transmitting) {
		pio_uart_hd_talk(dev);
	}

	/*
	 * Blocking, but bounded and only in thread context: the caller writes a
	 * byte at a time into an 8-deep FIFO that the state machine drains at
	 * one byte per frame time, so the wait here is at most a frame -- tens
	 * of microseconds -- and only once the FIFO has filled.
	 */
	pio_sm_put_blocking(pio, data->tx_sm, (uint32_t)c);
	data->tx_bytes++;

	k_spin_unlock(&data->lock, key);

	/*
	 * Re-armed per byte, so it only ever expires once the burst has actually
	 * stopped. Outside the lock: k_work_reschedule takes locks of its own,
	 * and nesting them under ours is how lock-ordering bugs start.
	 */
	k_work_reschedule(&data->turnaround, K_USEC(config->turnaround_delay_us));
}

static int pio_uart_hd_init(const struct device *dev)
{
	const struct pio_uart_hd_config *config = dev->config;
	struct pio_uart_hd_data *data = dev->data;
	PIO pio = pio_rpi_pico_get_pio(config->piodev);
	float sm_clock_div;
	int ret;

	data->dev = dev;
	k_work_init_delayable(&data->turnaround, pio_uart_hd_turnaround_expired);
	k_work_init_delayable(&data->report, pio_uart_hd_report);

	sm_clock_div = (float)clock_get_hz(clk_sys) / (CYCLES_PER_BIT * config->baudrate);

	ret = pio_rpi_pico_allocate_sm(config->piodev, &data->tx_sm);
	if (ret < 0) {
		return ret;
	}

	ret = pio_rpi_pico_allocate_sm(config->piodev, &data->rx_sm);
	if (ret < 0) {
		return ret;
	}

	ret = pio_uart_hd_tx_load(pio, data->tx_sm, config->pin, sm_clock_div, &data->tx_offset);
	if (ret < 0) {
		return ret;
	}

	ret = pio_uart_hd_rx_load(pio, data->rx_sm, config->pin, sm_clock_div, &data->rx_offset);
	if (ret < 0) {
		return ret;
	}

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	/* Come up listening; nothing drives the line until there is a byte to send. */
	pio_sm_set_pindirs_with_mask(pio, data->tx_sm, 0, BIT(config->pin));
	pio_sm_set_enabled(pio, data->rx_sm, true);
	data->transmitting = false;

	LOG_INF("half-duplex split up: pin %u, %u baud, clkdiv %d.%02d, sm tx=%u rx=%u",
		config->pin, config->baudrate, (int)sm_clock_div,
		(int)((sm_clock_div - (int)sm_clock_div) * 100), (unsigned)data->tx_sm,
		(unsigned)data->rx_sm);

	k_work_reschedule(&data->report, K_MSEC(REPORT_INTERVAL_MS));

	return 0;
}

static DEVICE_API(uart, pio_uart_hd_driver_api) = {
	.poll_in = pio_uart_hd_poll_in,
	.poll_out = pio_uart_hd_poll_out,
};

#define PIO_UART_HD_INIT(idx)                                                                      \
	PINCTRL_DT_INST_DEFINE(idx);                                                               \
	static const struct pio_uart_hd_config pio_uart_hd##idx##_config = {                       \
		.piodev = DEVICE_DT_GET(DT_INST_PARENT(idx)),                                      \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(idx),                                       \
		.pin = DT_INST_RPI_PICO_PIO_PIN_BY_NAME(idx, default, 0, data_pins, 0),            \
		.baudrate = DT_INST_PROP(idx, current_speed),                                      \
		.turnaround_delay_us = DT_INST_PROP(idx, turnaround_delay_us),                     \
		.tx_drain_us = (uint32_t)((TX_DRAIN_BITS * USEC_PER_SEC_F) /                       \
					  (float)DT_INST_PROP(idx, current_speed)) +               \
			       1,                                                                  \
	};                                                                                         \
	static struct pio_uart_hd_data pio_uart_hd##idx##_data;                                    \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, pio_uart_hd_init, NULL, &pio_uart_hd##idx##_data,               \
			      &pio_uart_hd##idx##_config, POST_KERNEL,                             \
			      CONFIG_SERIAL_INIT_PRIORITY, &pio_uart_hd_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PIO_UART_HD_INIT)
