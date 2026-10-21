/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "classification.h"

#include <errno.h>

int classification_decode(const nrf_axon_nn_compiled_model_s *model, const int8_t *packed_output,
			  struct classification_result *res)
{
	const nrf_axon_nn_model_layer_dimensions_s *dim = &model->outputs[0].dimensions;
	const size_t class_cnt = (size_t)dim->height * dim->width * dim->channel_cnt;
	const int32_t *scores = (const int32_t *)packed_output;
	int32_t top_score;
	int64_t sum = 0;

	if (model->output_cnt != 1 || dim->byte_width != sizeof(int32_t) ||
	    class_cnt > CLASSIFICATION_MAX_CLASSES) {
		return -EINVAL;
	}

	res->class_idx = nrf_axon_nn_get_classification(model, packed_output, &res->label,
							&top_score);
	if (res->class_idx < 0 || (size_t)res->class_idx >= class_cnt) {
		return -EINVAL;
	}
	res->class_cnt = (uint8_t)class_cnt;

	/* Axon softmax emits fixed-point probabilities; normalize by their sum so the
	 * result does not depend on the output Q format.
	 */
	for (size_t i = 0; i < class_cnt; i++) {
		if (scores[i] > 0) {
			sum += scores[i];
		}
	}

	for (size_t i = 0; i < class_cnt; i++) {
		res->probs[i] = (sum > 0 && scores[i] > 0) ? (float)scores[i] / (float)sum : 0.0f;
	}

	return 0;
}
