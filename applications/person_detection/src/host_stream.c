/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "host_stream.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(main);

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_STREAM)

#if DT_HAS_CHOSEN(person_det_host_stream_uart)
static const struct device *const stream_uart =
	DEVICE_DT_GET(DT_CHOSEN(person_det_host_stream_uart));
#elif DT_HAS_CHOSEN(zephyr_console)
static const struct device *const stream_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#else
#error "PERSON_DET_HOST_STREAM requires person-det,host-stream-uart or zephyr,console in devicetree"
#endif

static void tx_bytes(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(stream_uart, data[i]);
		if ((i & 0x7fU) == 0x7fU) {
			k_yield();
		}
	}
}

static void le16_put(uint8_t *dst, uint16_t v)
{
	dst[0] = (uint8_t)v;
	dst[1] = (uint8_t)(v >> 8);
}

static void le32_put(uint8_t *dst, uint32_t v)
{
	dst[0] = (uint8_t)v;
	dst[1] = (uint8_t)(v >> 8);
	dst[2] = (uint8_t)(v >> 16);
	dst[3] = (uint8_t)(v >> 24);
}

static void float_put(uint8_t *dst, float v)
{
	uint32_t u;

	memcpy(&u, &v, sizeof(u));
	le32_put(dst, u);
}

int host_stream_init(void)
{
	if (!device_is_ready(stream_uart)) {
		LOG_ERR("Host stream UART %s not ready", stream_uart->name);
		return -ENODEV;
	}

#if DT_HAS_CHOSEN(person_det_host_stream_uart)
	LOG_INF("Host stream on %s (dedicated UART)", stream_uart->name);
#else
	LOG_INF("Host stream on %s (console)", stream_uart->name);
#endif

	return 0;
}

void host_stream_send_rgb565_frame(uint32_t frame_id, uint16_t width, uint16_t height,
				   const uint8_t *rgb565_be, size_t byte_len)
{
	uint8_t hdr[18];
	uint32_t crc = 0U;
	uint8_t crc_buf[4];

	le32_put(&hdr[0], PDET_STREAM_MAGIC);
	hdr[4] = PDET_STREAM_VERSION;
	hdr[5] = PDET_STREAM_TYPE_FRAME;
	le16_put(&hdr[6], width);
	le16_put(&hdr[8], height);
	le32_put(&hdr[10], (uint32_t)byte_len);
	le32_put(&hdr[14], frame_id);

	tx_bytes(hdr, sizeof(hdr));

	for (size_t off = 0; off < byte_len;) {
		const size_t chunk = MIN(byte_len - off, 512U);

		crc = crc32_ieee_update(crc, rgb565_be + off, chunk);
		tx_bytes(rgb565_be + off, chunk);
		off += chunk;
	}

	le32_put(crc_buf, crc);
	tx_bytes(crc_buf, sizeof(crc_buf));
}

void host_stream_send_detections(uint32_t frame_id, uint16_t model_w, uint16_t model_h,
				 uint16_t pad_left, uint16_t pad_top,
				 const struct detection_box *boxes, size_t count)
{
	uint8_t hdr[15];
	uint8_t magic_buf[4];
	uint8_t crc_buf[4];
	uint32_t crc;

	if (count > 255) {
		count = 255;
	}

	hdr[0] = PDET_STREAM_VERSION;
	hdr[1] = PDET_STREAM_TYPE_DETECT;
	le32_put(&hdr[2], frame_id);
	le16_put(&hdr[6], model_w);
	le16_put(&hdr[8], model_h);
	le16_put(&hdr[10], pad_left);
	le16_put(&hdr[12], pad_top);
	hdr[14] = (uint8_t)count;

	crc = crc32_ieee(hdr, sizeof(hdr));

	le32_put(magic_buf, PDET_STREAM_MAGIC);
	tx_bytes(magic_buf, sizeof(magic_buf));
	tx_bytes(hdr, sizeof(hdr));

	for (size_t i = 0; i < count; i++) {
		uint8_t bp[21];

		float_put(&bp[0], boxes[i].x1);
		float_put(&bp[4], boxes[i].y1);
		float_put(&bp[8], boxes[i].x2);
		float_put(&bp[12], boxes[i].y2);
		float_put(&bp[16], boxes[i].score);
		bp[20] = (uint8_t)boxes[i].head_id;

		crc = crc32_ieee_update(crc, bp, sizeof(bp));
		tx_bytes(bp, sizeof(bp));
	}

	le32_put(crc_buf, crc);
	tx_bytes(crc_buf, sizeof(crc_buf));
}

#else /* !CONFIG_PERSON_DET_HOST_STREAM */

int host_stream_init(void)
{
	return -ENOTSUP;
}

void host_stream_send_rgb565_frame(uint32_t frame_id, uint16_t width, uint16_t height,
				   const uint8_t *rgb565_be, size_t byte_len)
{
	ARG_UNUSED(frame_id);
	ARG_UNUSED(width);
	ARG_UNUSED(height);
	ARG_UNUSED(rgb565_be);
	ARG_UNUSED(byte_len);
}

void host_stream_send_detections(uint32_t frame_id, uint16_t model_w, uint16_t model_h,
				 uint16_t pad_left, uint16_t pad_top,
				 const struct detection_box *boxes, size_t count)
{
	ARG_UNUSED(frame_id);
	ARG_UNUSED(model_w);
	ARG_UNUSED(model_h);
	ARG_UNUSED(pad_left);
	ARG_UNUSED(pad_top);
	ARG_UNUSED(boxes);
	ARG_UNUSED(count);
}

#endif /* CONFIG_PERSON_DET_HOST_STREAM */
