/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if !IS_ENABLED(CONFIG_PERSON_DET_STATIC_INPUT)
#include <zephyr/drivers/video.h>
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_STREAM)
#include "host_stream.h"
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_INPUT)
#include "host_input.h"
#endif

#include <axon/nrf_axon_platform.h>
#include <drivers/axon/nrf_axon_driver.h>
#include <drivers/axon/nrf_axon_nn_infer.h>

#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
#include "classification.h"
#include "generated/nrf_axon_model_tinyml_ic_.h"
#else
#include "postprocessing.h"
#include "generated/nrf_axon_model_person_det_.h"
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_STATIC_EMBEDDED_IMAGE)
#include "generated/static_image_rgb565.h"
#endif

LOG_MODULE_REGISTER(main);

#define CAM_WIDTH    128
#define CAM_HEIGHT   128

#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
#define AXON_MODEL        model_tinyml_ic
#define MODEL_WIDTH       32
#define MODEL_HEIGHT      32
#define MODEL_OUTPUT_SIZE NRF_AXON_MODEL_TINYML_IC_PACKED_OUTPUT_SIZE
/* Model input is raw 0..255 pixel values (quantized as pixel - 128). */
#define INPUT_GRAY_LEVEL  128.0f
#define DOWNSCALE         ((CAM_WIDTH) / (MODEL_WIDTH))

BUILD_ASSERT(CAM_WIDTH == DOWNSCALE * MODEL_WIDTH && CAM_HEIGHT == DOWNSCALE * MODEL_HEIGHT,
	     "Camera frame must be an integer multiple of the model input");
#else
#define AXON_MODEL        model_person_det
#define MODEL_WIDTH       160
#define MODEL_HEIGHT      128
#define MODEL_OUTPUT_SIZE NRF_AXON_MODEL_PERSON_DET_PACKED_OUTPUT_SIZE
/* Model input is symmetric [-1, 1]. */
#define INPUT_GRAY_LEVEL  0.0f
#define PAD_LEFT     (((MODEL_WIDTH) - (CAM_WIDTH)) / 2)
#define PAD_TOP	     (((MODEL_HEIGHT) - (CAM_HEIGHT)) / 2)

#define LUT_SIZE_5_BITS 32
#define LUT_SIZE_6_BITS 64

#define MAX_BOXES_LOG 8
#endif

#define FRAME_RGB565_BYTES ((CAM_WIDTH) * (CAM_HEIGHT) * 2)

#define HAS_RGB565_INPUT                                                                           \
	(!IS_ENABLED(CONFIG_PERSON_DET_STATIC_INPUT) ||                                            \
	 IS_ENABLED(CONFIG_PERSON_DET_STATIC_EMBEDDED_IMAGE) ||                                    \
	 IS_ENABLED(CONFIG_PERSON_DET_HOST_INPUT))

static int8_t input_buf[MODEL_WIDTH * MODEL_HEIGHT * 3];
static int8_t output_buf[MODEL_OUTPUT_SIZE];

#if HAS_RGB565_INPUT
#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
static uint16_t downscale_acc[3][MODEL_WIDTH * MODEL_HEIGHT];
#else
static int8_t lut_red_blue[LUT_SIZE_5_BITS];
static int8_t lut_green[LUT_SIZE_6_BITS];
#endif
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_STATIC_EMBEDDED_IMAGE)
static uint8_t static_rgb565[FRAME_RGB565_BYTES];
#endif

static const struct gpio_dt_spec led_capture = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_detection = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

#if !IS_ENABLED(CONFIG_PERSON_DET_HOST_INPUT)
static void capture_timer_expiry(struct k_timer *timer);
static K_TIMER_DEFINE(capture_timer, capture_timer_expiry, NULL);
static K_SEM_DEFINE(capture_sem, 0, 1);

static void capture_timer_expiry(struct k_timer *timer)
{
	k_sem_give(&capture_sem);
}
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_STREAM)
static uint32_t stream_frame_id;
#endif

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
	const uint32_t quant_mult = in->quant_mult;
	const uint8_t quant_round = in->quant_round;
	const int8_t quant_zp = in->quant_zp;

	const float scale = (float)quant_mult / (float)(1 << quant_round);
	const int32_t quantized = (int32_t)(value * scale) + quant_zp;

	return (int8_t)__ssat(quantized, 8);
}

static void prefill_input_buf(const nrf_axon_nn_compiled_model_input_s *in)
{
	const int8_t gray = quantize(INPUT_GRAY_LEVEL, in);

	memset(input_buf, gray, sizeof(input_buf));
}

#if HAS_RGB565_INPUT

static inline uint16_t extract_pixel(const uint8_t *data, const size_t pixel)
{
	const size_t offset = pixel * 2;

	return (uint16_t)((uint16_t)data[offset] << 8 | data[offset + 1]);
}

#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)

static size_t frame_width;
static size_t frame_scale;

static int input_frame_begin(uint16_t width, uint16_t height)
{
	const size_t scale = width / MODEL_WIDTH;

	/* downscale_acc is uint16_t: at most 16x16 pixels of 255 per bin. */
	if (scale == 0 || scale > 16 || width != scale * MODEL_WIDTH ||
	    height != scale * MODEL_HEIGHT) {
		return -EINVAL;
	}

	frame_width = width;
	frame_scale = scale;
	memset(downscale_acc, 0, sizeof(downscale_acc));

	return 0;
}

static void convert_chunk_to_model_input(const uint8_t *chunk_buf, const size_t pixel_start,
					 const size_t pixel_count)
{
	for (size_t p = 0; p < pixel_count; p++) {
		const size_t pixel_idx = pixel_start + p;
		const size_t src_row = pixel_idx / frame_width;
		const size_t src_col = pixel_idx % frame_width;
		const size_t dst_offset =
			(src_row / frame_scale) * MODEL_WIDTH + (src_col / frame_scale);

		const uint16_t pixel = extract_pixel(chunk_buf, p);
		const uint8_t r5 = (pixel >> 11) & 0x1f;
		const uint8_t g6 = (pixel >> 5) & 0x3f;
		const uint8_t b5 = pixel & 0x1f;

		downscale_acc[0][dst_offset] += (r5 << 3) | (r5 >> 2);
		downscale_acc[1][dst_offset] += (g6 << 2) | (g6 >> 4);
		downscale_acc[2][dst_offset] += (b5 << 3) | (b5 >> 2);
	}
}

static void input_frame_end(const nrf_axon_nn_compiled_model_input_s *in)
{
	const uint32_t area = frame_scale * frame_scale;

	for (size_t c = 0; c < ARRAY_SIZE(downscale_acc); c++) {
		for (size_t i = 0; i < ARRAY_SIZE(downscale_acc[0]); i++) {
			const uint32_t avg = (downscale_acc[c][i] + (area / 2)) / area;

			input_buf[c * MODEL_WIDTH * MODEL_HEIGHT + i] = quantize((float)avg, in);
		}
	}
}

#else /* person detection */

static int input_frame_begin(uint16_t width, uint16_t height)
{
	return (width == CAM_WIDTH && height == CAM_HEIGHT) ? 0 : -EINVAL;
}

static void input_frame_end(const nrf_axon_nn_compiled_model_input_s *in)
{
	ARG_UNUSED(in);
}

static void prefill_luts(const nrf_axon_nn_compiled_model_input_s *in)
{
	for (size_t i = 0; i < ARRAY_SIZE(lut_red_blue); i++) {
		const float value = (float)i / 32.f;
		const float value_sym = (value * 2.f) - 1.f;

		lut_red_blue[i] = quantize(value_sym, in);
	}

	for (size_t i = 0; i < ARRAY_SIZE(lut_green); i++) {
		const float value = (float)i / 64.f;
		const float value_sym = (value * 2.f) - 1.f;

		lut_green[i] = quantize(value_sym, in);
	}
}

static void convert_chunk_to_model_input(const uint8_t *chunk_buf, const size_t pixel_start,
					 const size_t pixel_count)
{
	for (size_t p = 0; p < pixel_count; p++) {
		const size_t pixel_idx = pixel_start + p;
		const size_t cam_row = pixel_idx / CAM_WIDTH;
		const size_t cam_col = pixel_idx % CAM_WIDTH;
		const size_t model_row = PAD_TOP + cam_row;
		const size_t model_col = PAD_LEFT + cam_col;
		const size_t dst_offset = model_row * MODEL_WIDTH + model_col;

		const uint16_t pixel = extract_pixel(chunk_buf, p);
		const uint8_t r5 = (pixel >> 11) & 0x1f;
		const uint8_t g6 = (pixel >> 5) & 0x3f;
		const uint8_t b5 = pixel & 0x1f;

		input_buf[0 * MODEL_WIDTH * MODEL_HEIGHT + dst_offset] = (int8_t)lut_red_blue[r5];
		input_buf[1 * MODEL_WIDTH * MODEL_HEIGHT + dst_offset] = (int8_t)lut_green[g6];
		input_buf[2 * MODEL_WIDTH * MODEL_HEIGHT + dst_offset] = (int8_t)lut_red_blue[b5];
	}
}

#endif /* CONFIG_PERSON_DET_MODEL_CIFAR10_IC */

#endif /* HAS_RGB565_INPUT */

#if IS_ENABLED(CONFIG_PERSON_DET_STATIC_EMBEDDED_IMAGE)

static void load_embedded_rgb565_into_model(const nrf_axon_nn_compiled_model_input_s *in)
{
	__ASSERT_NO_MSG(STATIC_IMAGE_RGB565_BYTES == FRAME_RGB565_BYTES);

	memcpy(static_rgb565, static_image_rgb565, sizeof(static_rgb565));
	prefill_input_buf(in);
	(void)input_frame_begin(CAM_WIDTH, CAM_HEIGHT);
	convert_chunk_to_model_input(static_rgb565, 0, (CAM_WIDTH * CAM_HEIGHT));
	input_frame_end(in);
}

#endif

#if !IS_ENABLED(CONFIG_PERSON_DET_STATIC_INPUT)

static int capture_one_frame(const struct device *video,
			     const nrf_axon_nn_compiled_model_input_s *in)
{
	size_t total = 0;

	(void)input_frame_begin(CAM_WIDTH, CAM_HEIGHT);

	while (total < FRAME_RGB565_BYTES) {
		struct video_buffer *vbuf;
		int err = video_dequeue(video, &vbuf, K_FOREVER);

		if (err == -EAGAIN) {
			continue;
		}
		if (err) {
			LOG_ERR("video_dequeue failed: %d", err);
			return err;
		}

		const size_t room = FRAME_RGB565_BYTES - total;
		const size_t chunk = min(vbuf->bytesused, room);

		convert_chunk_to_model_input(vbuf->buffer, total / 2, chunk / 2);

		total += chunk;

		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		video_enqueue(video, vbuf);
	}

	input_frame_end(in);

	return 0;
}

#endif /* !CONFIG_PERSON_DET_STATIC_INPUT */

#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)

static int run_classification(const nrf_axon_nn_compiled_model_s *model,
			      struct classification_result *res)
{
	nrf_axon_result_e result;
	int err;

	result = nrf_axon_nn_model_infer_sync(model, input_buf, output_buf);
	if (result != NRF_AXON_RESULT_SUCCESS) {
		LOG_ERR("Inference failed (result %d)", result);
		(void)gpio_pin_set_dt(&led_detection, 0);
		return -1;
	}

	err = classification_decode(model, output_buf, res);
	if (err) {
		LOG_ERR("Classification decode failed (err %d)", err);
		(void)gpio_pin_set_dt(&led_detection, 0);
		return -1;
	}

	const float top = res->probs[res->class_idx];

	(void)gpio_pin_set_dt(&led_detection, (top * 1000.f) >= CONFIG_SCORE_THRESHOLD);
	LOG_INF("Class %d: %s (%.1f%%)", res->class_idx, res->label, (double)(top * 100.f));

	return 0;
}

#else /* person detection */

static void log_bounding_boxes(const struct detection_box *boxes, const size_t n)
{
	for (size_t i = 0; i < n; i++) {
		LOG_INF("Bounding box %d: head %s, [%.1f, %.1f, %.1f, %.1f] score %.3f", i,
			model_head_name(boxes[i].head_id), (double)boxes[i].x1, (double)boxes[i].y1,
			(double)boxes[i].x2, (double)boxes[i].y2, (double)boxes[i].score);
	}
}

static int run_detection(const nrf_axon_nn_compiled_model_s *model, struct detection_box *boxes,
			   size_t max_boxes, size_t *out_n)
{
	nrf_axon_result_e result;

	result = nrf_axon_nn_model_infer_sync(model, input_buf, output_buf);
	if (result != NRF_AXON_RESULT_SUCCESS) {
		LOG_ERR("Inference failed (result %d)", result);
		(void)gpio_pin_set_dt(&led_detection, 0);
		return -1;
	}

	const size_t n = decode_output(model, output_buf, boxes, max_boxes);

	if (out_n != NULL) {
		*out_n = n;
	}

	if (n > 0) {
		(void)gpio_pin_set_dt(&led_detection, 1);
		log_bounding_boxes(boxes, n);
	} else {
		(void)gpio_pin_set_dt(&led_detection, 0);
		LOG_INF("No detections");
	}

	return 0;
}

#endif /* CONFIG_PERSON_DET_MODEL_CIFAR10_IC */

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_INPUT)

static const struct host_input_handler input_handler = {
	.begin = input_frame_begin,
	.pixels = convert_chunk_to_model_input,
};

static int host_input_and_infer(const nrf_axon_nn_compiled_model_s *model,
				const nrf_axon_nn_compiled_model_input_s *model_inputs)
{
	uint32_t frame_id;
	uint16_t width;
	uint16_t height;

	host_input_receive_frame(&input_handler, &frame_id, &width, &height);
	(void)gpio_pin_toggle_dt(&led_capture);
	input_frame_end(model_inputs);

	LOG_INF("Frame %u (%ux%u) received", frame_id, width, height);

#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
	struct classification_result res;

	return run_classification(model, &res);
#else
	struct detection_box boxes[MAX_BOXES_LOG];

	return run_detection(model, boxes, MAX_BOXES_LOG, NULL);
#endif
}

#elif IS_ENABLED(CONFIG_PERSON_DET_STATIC_INPUT)

static int static_input_and_detect(const nrf_axon_nn_compiled_model_s *model,
				     const nrf_axon_nn_compiled_model_input_s *model_inputs)
{
#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
	struct classification_result res;
#else
	struct detection_box boxes[MAX_BOXES_LOG];
	size_t n = 0;
#endif

	(void)gpio_pin_toggle_dt(&led_capture);

#if IS_ENABLED(CONFIG_PERSON_DET_STATIC_EMBEDDED_IMAGE)
	load_embedded_rgb565_into_model(model_inputs);
#else
	ARG_UNUSED(model_inputs);
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
	if (run_classification(model, &res) != 0) {
		return -1;
	}
#else
	if (run_detection(model, boxes, MAX_BOXES_LOG, &n) != 0) {
		return -1;
	}
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_STREAM)
	stream_frame_id++;
	host_stream_send_rgb565_frame(stream_frame_id, CAM_WIDTH, CAM_HEIGHT, static_rgb565,
				      FRAME_RGB565_BYTES);
#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
	host_stream_send_classification(stream_frame_id, (uint8_t)res.class_idx, res.probs,
					res.class_cnt);
#else
	host_stream_send_detections(stream_frame_id, MODEL_WIDTH, MODEL_HEIGHT, PAD_LEFT, PAD_TOP,
				    boxes, n);
#endif
#endif

	return 0;
}

#else

static int capture_and_detect(const struct device *video, const nrf_axon_nn_compiled_model_s *model)
{
	int err;
#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
	struct classification_result res;
#else
	struct detection_box boxes[MAX_BOXES_LOG];
#endif

	(void)gpio_pin_toggle_dt(&led_capture);

	err = video_stream_start(video, VIDEO_BUF_TYPE_OUTPUT);
	if (err) {
		LOG_ERR("Failed to start stream (err %d)", err);
		return -1;
	}

	err = capture_one_frame(video, &model->inputs[0]);
	if (err) {
		LOG_ERR("Failed to capture frame (err %d)", err);
		(void)video_stream_stop(video, VIDEO_BUF_TYPE_OUTPUT);
		return -1;
	}

	err = video_stream_stop(video, VIDEO_BUF_TYPE_OUTPUT);
	if (err) {
		LOG_ERR("Failed to stop stream (err %d)", err);
		return -1;
	}

#if IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
	return run_classification(model, &res);
#else
	return run_detection(model, boxes, MAX_BOXES_LOG, NULL);
#endif
}

#endif

int main(void)
{
	int err;

	nrf_axon_result_e result;
	const nrf_axon_nn_compiled_model_s *model = &AXON_MODEL;
	const nrf_axon_nn_compiled_model_input_s *model_inputs = &model->inputs[0];

	__ASSERT(model_inputs->dimensions.width == MODEL_WIDTH &&
		 model_inputs->dimensions.height == MODEL_HEIGHT &&
		 model_inputs->dimensions.channel_cnt == 3,
		 "Model input does not match MODEL_WIDTH x MODEL_HEIGHT x 3");

#if !IS_ENABLED(CONFIG_PERSON_DET_STATIC_INPUT)
	const struct device *video = DEVICE_DT_GET(DT_NODELABEL(arducam_mega));
	struct video_buffer *vbufs[2];
	struct video_format fmt = {.type = VIDEO_BUF_TYPE_INPUT,
				   .pixelformat = VIDEO_PIX_FMT_RGB565,
				   .width = CAM_WIDTH,
				   .height = CAM_HEIGHT,
				   .pitch = CAM_WIDTH * 2};
#endif

	if (led_init(&led_capture) != 0) {
		return -1;
	}
	if (led_init(&led_detection) != 0) {
		return -1;
	}

#if !IS_ENABLED(CONFIG_PERSON_DET_STATIC_INPUT)
	if (!device_is_ready(video)) {
		LOG_ERR("Video device not ready");
		return -1;
	}

	err = video_set_format(video, &fmt);
	if (err) {
		LOG_ERR("Setting video format failed (err %d)", err);
		return -1;
	}

	for (size_t i = 0; i < ARRAY_SIZE(vbufs); i++) {
		vbufs[i] = video_buffer_alloc(1024, K_NO_WAIT);
		if (vbufs[i] == NULL) {
			LOG_ERR("Allocation failed for video buffer %u", i);
			return -1;
		}
		vbufs[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		video_enqueue(video, vbufs[i]);
	}
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_STREAM)
	err = host_stream_init();
	if (err != 0) {
		LOG_ERR("Host stream init failed (err %d)", err);
		return -1;
	}
	k_msleep(CONFIG_PERSON_DET_HOST_STREAM_STARTUP_DELAY_MS);
#endif

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

	prefill_input_buf(model_inputs);
#if !IS_ENABLED(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
#if HAS_RGB565_INPUT
	prefill_luts(model_inputs);
#endif
	decode_init(model);
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_INPUT)
	err = host_input_init();
	if (err != 0) {
		LOG_ERR("Host input init failed (err %d)", err);
		return -1;
	}

	LOG_INF("Model %s waiting for frames from host", model->model_name);

	while (true) {
		err = host_input_and_infer(model, model_inputs);
		if (err) {
			return -1;
		}
	}
#else

#if IS_ENABLED(CONFIG_PERSON_DET_STATIC_INPUT)
#if IS_ENABLED(CONFIG_PERSON_DET_HOST_STREAM)
	LOG_INF("Model %s static stream (embed image + UART binary)", model->model_name);
#else
	LOG_INF("Model %s start (static input, no camera)", model->model_name);
#endif
#else
	LOG_INF("Model %s start", model->model_name);
#endif

#if IS_ENABLED(CONFIG_PERSON_DET_HOST_STREAM)
	k_timer_start(&capture_timer, K_NO_WAIT,
		      K_MSEC(CONFIG_PERSON_DET_HOST_STREAM_PERIOD_MS));
#else
	k_timer_start(&capture_timer, K_NO_WAIT, K_MSEC(500));
#endif

	while (true) {
		err = k_sem_take(&capture_sem, K_FOREVER);
		if (err) {
			continue;
		}

#if IS_ENABLED(CONFIG_PERSON_DET_STATIC_INPUT)
		err = static_input_and_detect(model, model_inputs);
#else
		err = capture_and_detect(video, model);
#endif
		if (err) {
			return -1;
		}
	}
#endif /* CONFIG_PERSON_DET_HOST_INPUT */

	return 0;
}
