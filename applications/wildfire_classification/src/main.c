/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <axon/nrf_axon_platform.h>
#include <drivers/axon/nrf_axon_driver.h>
#include <drivers/axon/nrf_axon_nn_infer.h>

#include "classification.h"
#include "host_input.h"

#include "generated/nrf_axon_model_wildfire_.h"

#define AXON_MODEL         model_wildfire
#define MODEL_WIDTH        96
#define MODEL_HEIGHT       96
#define MODEL_OUTPUT_SIZE  NRF_AXON_MODEL_WILDFIRE_PACKED_OUTPUT_SIZE
/* Model input is pixel / 127.5 - 1 (ImageNet MobileNet preprocessing). */
#define INPUT_PIXEL_SCALE  (1.0f / 127.5f)
#define INPUT_PIXEL_OFFSET (-1.0f)
/* Output classes: 0 = NO_FIRE, 1 = FIRE. */
#define FIRE_CLASS_IDX     1

/* downscale_acc is uint16_t: at most 16x16 source pixels of 255 per bin. */
#define MAX_DOWNSCALE 16

LOG_MODULE_REGISTER(main);

static int8_t input_buf[MODEL_WIDTH * MODEL_HEIGHT * 3];
static int8_t output_buf[MODEL_OUTPUT_SIZE];

static uint16_t downscale_acc[3][MODEL_WIDTH * MODEL_HEIGHT];
static uint8_t col_bin_cnt[MODEL_WIDTH];
static uint8_t row_bin_cnt[MODEL_HEIGHT];
static size_t frame_width;
static size_t frame_height;

static const struct gpio_dt_spec led_frame = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_result = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

static int led_init(const struct gpio_dt_spec *spec)
{
	int err;

	if (!gpio_is_ready_dt(spec)) {
		LOG_ERR("GPIO %s is not ready", spec->port->name);
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(spec, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Failed to configure %s pin %u (err %d)", spec->port->name, spec->pin, err);
		return err;
	}

	return 0;
}

static inline int8_t quantize(const float value, const nrf_axon_nn_compiled_model_input_s *in)
{
	const float scale = (float)in->quant_mult / (float)(1 << in->quant_round);
	const float scaled = value * scale;
	const int32_t quantized =
		(int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f) + in->quant_zp;

	return (int8_t)__ssat(quantized, 8);
}

static int input_frame_begin(uint16_t width, uint16_t height)
{
	if (width < MODEL_WIDTH || height < MODEL_HEIGHT || width > MAX_DOWNSCALE * MODEL_WIDTH ||
	    height > MAX_DOWNSCALE * MODEL_HEIGHT) {
		return -EINVAL;
	}

	frame_width = width;
	frame_height = height;
	memset(downscale_acc, 0, sizeof(downscale_acc));
	memset(col_bin_cnt, 0, sizeof(col_bin_cnt));
	memset(row_bin_cnt, 0, sizeof(row_bin_cnt));

	for (size_t x = 0; x < width; x++) {
		col_bin_cnt[x * MODEL_WIDTH / width]++;
	}
	for (size_t y = 0; y < height; y++) {
		row_bin_cnt[y * MODEL_HEIGHT / height]++;
	}

	return 0;
}

static void input_frame_pixels(const uint8_t *rgb565_be, size_t pixel_start, size_t pixel_count)
{
	for (size_t p = 0; p < pixel_count; p++) {
		const size_t pixel_idx = pixel_start + p;
		const size_t src_row = pixel_idx / frame_width;
		const size_t src_col = pixel_idx % frame_width;
		const size_t dst = (src_row * MODEL_HEIGHT / frame_height) * MODEL_WIDTH +
				   (src_col * MODEL_WIDTH / frame_width);

		const uint16_t pixel = (uint16_t)rgb565_be[2 * p] << 8 | rgb565_be[2 * p + 1];
		const uint8_t r5 = (pixel >> 11) & 0x1f;
		const uint8_t g6 = (pixel >> 5) & 0x3f;
		const uint8_t b5 = pixel & 0x1f;

		downscale_acc[0][dst] += (r5 << 3) | (r5 >> 2);
		downscale_acc[1][dst] += (g6 << 2) | (g6 >> 4);
		downscale_acc[2][dst] += (b5 << 3) | (b5 >> 2);
	}
}

static void input_frame_end(const nrf_axon_nn_compiled_model_input_s *in)
{
	for (size_t c = 0; c < ARRAY_SIZE(downscale_acc); c++) {
		for (size_t i = 0; i < ARRAY_SIZE(downscale_acc[0]); i++) {
			const uint32_t area =
				row_bin_cnt[i / MODEL_WIDTH] * col_bin_cnt[i % MODEL_WIDTH];
			const uint32_t avg = (downscale_acc[c][i] + (area / 2)) / area;

			input_buf[c * MODEL_WIDTH * MODEL_HEIGHT + i] =
				quantize((float)avg * INPUT_PIXEL_SCALE + INPUT_PIXEL_OFFSET, in);
		}
	}
}

static const struct host_input_handler input_handler = {
	.begin = input_frame_begin,
	.pixels = input_frame_pixels,
};

static int classify(const nrf_axon_nn_compiled_model_s *model)
{
	struct classification_result res;
	nrf_axon_result_e result;
	int err;

	result = nrf_axon_nn_model_infer_sync(model, input_buf, output_buf);
	if (result != NRF_AXON_RESULT_SUCCESS) {
		LOG_ERR("Inference failed (result %d)", result);
		(void)gpio_pin_set_dt(&led_result, 0);
		return -1;
	}

	err = classification_decode(model, output_buf, &res);
	if (err) {
		LOG_ERR("Classification decode failed (err %d)", err);
		(void)gpio_pin_set_dt(&led_result, 0);
		return -1;
	}

	const float top = res.probs[res.class_idx];

	(void)gpio_pin_set_dt(&led_result,
			      (res.probs[FIRE_CLASS_IDX] * 1000.f) >= CONFIG_WILDFIRE_SCORE_THRESHOLD);
	LOG_INF("Class %d: %s (%.1f%%)", res.class_idx, res.label, (double)(top * 100.f));

	return 0;
}

int main(void)
{
	const nrf_axon_nn_compiled_model_s *model = &AXON_MODEL;
	const nrf_axon_nn_compiled_model_input_s *model_input = &model->inputs[0];
	nrf_axon_result_e result;
	int err;

	__ASSERT(model_input->dimensions.width == MODEL_WIDTH &&
		 model_input->dimensions.height == MODEL_HEIGHT &&
		 model_input->dimensions.channel_cnt == 3,
		 "Model input does not match MODEL_WIDTH x MODEL_HEIGHT x 3");

	if (led_init(&led_frame) != 0 || led_init(&led_result) != 0) {
		return -1;
	}

	result = nrf_axon_platform_init();
	if (result != NRF_AXON_RESULT_SUCCESS) {
		LOG_ERR("Axon platform init failed (result %d)", result);
		return -1;
	}

	result = nrf_axon_nn_model_validate(model);
	if (result != NRF_AXON_RESULT_SUCCESS) {
		LOG_ERR("Model validation failed (result %d)", result);
		return -1;
	}

	err = host_input_init();
	if (err) {
		LOG_ERR("Host input init failed (err %d)", err);
		return -1;
	}

	LOG_INF("Model %s (%dx%d) waiting for frames from host", model->model_name, MODEL_WIDTH,
		MODEL_HEIGHT);

	while (true) {
		uint32_t frame_id;
		uint16_t width;
		uint16_t height;

		host_input_receive_frame(&input_handler, &frame_id, &width, &height);
		(void)gpio_pin_toggle_dt(&led_frame);
		input_frame_end(model_input);

		LOG_INF("Frame %u (%ux%u) received", frame_id, width, height);

		if (classify(model) != 0) {
			return -1;
		}
	}

	return 0;
}
