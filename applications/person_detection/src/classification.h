/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __CLASSIFICATION_H__
#define __CLASSIFICATION_H__

#include <stddef.h>
#include <stdint.h>

#include <drivers/axon/nrf_axon_nn_infer.h>

#define CLASSIFICATION_MAX_CLASSES 16

/**
 * @brief Result of a single-output classification model.
 */
struct classification_result {
	/** Index of the highest scoring class. */
	int16_t class_idx;
	/** Label of the highest scoring class, or "N/A". */
	const char *label;
	/** Number of valid entries in @ref probs. */
	uint8_t class_cnt;
	/** Per-class probability, normalized to sum to 1. */
	float probs[CLASSIFICATION_MAX_CLASSES];
};

/**
 * @brief Decode the softmax output of a classification model.
 *
 * @param model Axon model with a single 1-D int32 softmax output.
 * @param packed_output Output from the model in packed format.
 * @param[out] res Place to store the result.
 *
 * @return 0 on success, -EINVAL if the model output is not supported.
 */
int classification_decode(const nrf_axon_nn_compiled_model_s *model, const int8_t *packed_output,
			  struct classification_result *res);

#endif /* __CLASSIFICATION_H__ */
