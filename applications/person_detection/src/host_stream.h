/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Binary stream over UART (same wire format as app-axon USB CDC viewer).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "postprocessing.h"

#define PDET_STREAM_MAGIC       0x12345AA5U
#define PDET_STREAM_VERSION     1
#define PDET_STREAM_TYPE_FRAME  0x01
#define PDET_STREAM_TYPE_DETECT 0x02

int host_stream_init(void);

void host_stream_send_rgb565_frame(uint32_t frame_id, uint16_t width, uint16_t height,
				   const uint8_t *rgb565_be, size_t byte_len);

void host_stream_send_detections(uint32_t frame_id, uint16_t model_w, uint16_t model_h,
				 uint16_t pad_left, uint16_t pad_top,
				 const struct detection_box *boxes, size_t count);
