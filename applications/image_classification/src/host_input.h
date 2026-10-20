/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Receive RGB565 frames from the host over UART.
 *
 * Frame message (little-endian):
 *   [4B magic 0x12345AA5] [1B version] [1B type=0x01] [2B width] [2B height]
 *   [4B payload_len] [4B frame_id] [payload: RGB565 big-endian] [4B crc32(payload)]
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define HOST_INPUT_MAGIC      0x12345AA5U
#define HOST_INPUT_VERSION    1
#define HOST_INPUT_TYPE_FRAME 0x01

struct host_input_handler {
	/** Called when a frame header arrives. Return non-zero to reject the frame. */
	int (*begin)(uint16_t width, uint16_t height);
	/** Called with consecutive RGB565 big-endian pixels of the current frame. */
	void (*pixels)(const uint8_t *rgb565_be, size_t pixel_start, size_t pixel_count);
};

int host_input_init(void);

/**
 * @brief Block until one complete frame with a valid CRC has been received.
 *
 * Frames that are rejected by @p handler->begin or fail the CRC check are
 * dropped and reception continues.
 *
 * @param handler Frame callbacks.
 * @param[out] frame_id Host frame ID of the received frame.
 * @param[out] width Frame width.
 * @param[out] height Frame height.
 */
void host_input_receive_frame(const struct host_input_handler *handler, uint32_t *frame_id,
			      uint16_t *width, uint16_t *height);
