/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "host_input.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(main);

#if !DT_HAS_CHOSEN(img_class_host_uart)
#error "Host frame input requires img-class,host-uart in devicetree (not the console)"
#endif

#define FRAME_HDR_SIZE  18
#define PAYLOAD_CHUNK   256
#define MAX_FRAME_DIM   512

static const struct device *const input_uart =
	DEVICE_DT_GET(DT_CHOSEN(img_class_host_uart));

RING_BUF_DECLARE(rx_ring, CONFIG_IMG_CLASS_RX_BUF_SIZE);
static K_SEM_DEFINE(rx_sem, 0, 1);
static atomic_t rx_dropped;

static void uart_isr(const struct device *dev, void *user_data)
{
	uint8_t buf[64];

	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		const int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}

		const uint32_t put = ring_buf_put(&rx_ring, buf, n);

		if (put < (uint32_t)n) {
			atomic_add(&rx_dropped, n - put);
		}
		k_sem_give(&rx_sem);
	}
}

static void rx_read_exact(uint8_t *dst, size_t len)
{
	while (len > 0) {
		const uint32_t n = ring_buf_get(&rx_ring, dst, len);

		if (n == 0) {
			(void)k_sem_take(&rx_sem, K_FOREVER);
			continue;
		}
		dst += n;
		len -= n;
	}
}

static uint16_t le16_get(const uint8_t *src)
{
	return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t le32_get(const uint8_t *src)
{
	return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
	       ((uint32_t)src[3] << 24);
}

static void wait_for_magic(uint8_t *hdr)
{
	uint32_t window = 0;

	while (window != HOST_INPUT_MAGIC) {
		uint8_t b;

		rx_read_exact(&b, 1);
		window = (window >> 8) | ((uint32_t)b << 24);
	}

	hdr[0] = (uint8_t)window;
	hdr[1] = (uint8_t)(window >> 8);
	hdr[2] = (uint8_t)(window >> 16);
	hdr[3] = (uint8_t)(window >> 24);
}

int host_input_init(void)
{
	uint8_t discard;

	if (!device_is_ready(input_uart)) {
		LOG_ERR("Host input UART %s not ready", input_uart->name);
		return -ENODEV;
	}

	uart_irq_rx_disable(input_uart);
	uart_irq_tx_disable(input_uart);

	while (uart_fifo_read(input_uart, &discard, 1) > 0) {
	}

	int err = uart_irq_callback_user_data_set(input_uart, uart_isr, NULL);

	if (err) {
		LOG_ERR("Host input UART IRQ setup failed (err %d)", err);
		return err;
	}

	uart_irq_rx_enable(input_uart);
	LOG_INF("Host input on %s (RX only)", input_uart->name);

	return 0;
}

void host_input_receive_frame(const struct host_input_handler *handler, uint32_t *frame_id,
			      uint16_t *width, uint16_t *height)
{
	while (true) {
		uint8_t hdr[FRAME_HDR_SIZE];
		uint8_t chunk[PAYLOAD_CHUNK];
		uint8_t crc_buf[4];
		uint32_t crc = 0U;

		wait_for_magic(hdr);
		rx_read_exact(&hdr[4], sizeof(hdr) - 4);

		const uint16_t w = le16_get(&hdr[6]);
		const uint16_t h = le16_get(&hdr[8]);
		const uint32_t len = le32_get(&hdr[10]);

		if (hdr[4] != HOST_INPUT_VERSION || hdr[5] != HOST_INPUT_TYPE_FRAME ||
		    w == 0 || h == 0 || w > MAX_FRAME_DIM || h > MAX_FRAME_DIM ||
		    len != (uint32_t)w * h * 2U) {
			continue;
		}

		const bool accepted = (handler->begin(w, h) == 0);

		if (!accepted) {
			LOG_WRN("Dropping %ux%u frame (unsupported size)", w, h);
		}

		for (uint32_t off = 0; off < len;) {
			const size_t n = MIN(len - off, sizeof(chunk));

			rx_read_exact(chunk, n);
			crc = crc32_ieee_update(crc, chunk, n);
			if (accepted) {
				handler->pixels(chunk, off / 2U, n / 2U);
			}
			off += n;
		}

		rx_read_exact(crc_buf, sizeof(crc_buf));

		const atomic_val_t dropped = atomic_set(&rx_dropped, 0);

		if (dropped) {
			LOG_WRN("Host input RX overflow, %ld bytes dropped", (long)dropped);
		}

		if (!accepted) {
			continue;
		}

		if (le32_get(crc_buf) != crc) {
			LOG_WRN("Frame %u CRC mismatch, dropped", le32_get(&hdr[14]));
			continue;
		}

		*frame_id = le32_get(&hdr[14]);
		*width = w;
		*height = h;
		return;
	}
}
