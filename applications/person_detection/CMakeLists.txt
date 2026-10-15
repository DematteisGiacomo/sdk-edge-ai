# Copyright (c) 2026 Nordic Semiconductor
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(person_detection)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dts)

target_sources(app PRIVATE
  src/main.c
  src/host_stream.c
)

if(CONFIG_PERSON_DET_HOST_INPUT)
  target_sources(app PRIVATE src/host_input.c)
endif()

if(CONFIG_PERSON_DET_MODEL_CIFAR10_IC)
  target_sources(app PRIVATE src/classification.c)
else()
  target_sources(app PRIVATE src/postprocessing.c)
endif()

if(NOT CONFIG_PERSON_DET_STATIC_INPUT)
  target_sources(app PRIVATE src/drivers/arducam_mega.c)
endif()
