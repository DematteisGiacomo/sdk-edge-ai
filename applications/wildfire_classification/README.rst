.. _app_wildfire_classification:

Wildfire classification
#######################

.. contents::
   :local:
   :depth: 2

This application demonstrates wildfire detection on the Axon NPU, classifying images streamed from a host PC over UART as fire or no fire.

The following animation shows the application on the nRF9251 DK, with the device console on the left and the feeder script on the right.
The feeder window shows the original image and the 96×96 frame sent to the device:

.. figure:: images/wildfire_classification_demo.gif
   :alt: Wildfire classification on the nRF9251 DK with images fed over UART

Application overview
********************

The application receives RGB565 frames from the host on a dedicated input UART, downscales them on the fly to the 96×96 model input, runs inference on the Axon NPU, and logs the result on the console.
No camera is required.
Nothing is sent back on the input UART, so a host script can show each image while the device console shows the classification result.

The model is a MobileNetV1 0.25 (the MLPerf Tiny Visual Wake Words backbone) fine-tuned from ImageNet weights on `The Wildfire Dataset <https://www.kaggle.com/datasets/elmadafri/the-wildfire-dataset>`_ (CC BY 4.0), a set of forest images with and without flames or smoke.
It outputs two classes, ``NO_FIRE`` and ``FIRE``, and classifies 92.4% of the 410 held-out test images correctly after int8 quantization.
The training, quantization, and Axon compilation pipeline is available in the `wildfire_axon <https://github.com/DematteisGiacomo/wildfire_axon>`_ repository.

The frames received from the host are not stored in full.
Pixels are box-filtered into the model input buffer while they arrive, so the host can send square frames from 96×96 up to 1536×1536.
Frames with a CRC error, an unsupported size, or a broken header are dropped with a warning.

Frame format
============

Each frame is sent as a single message with the following little-endian layout:

.. code-block:: none

   [4B magic 0x12345AA5] [1B version = 1] [1B type = 0x01] [2B width] [2B height]
   [4B payload length] [4B frame ID] [payload: RGB565, big-endian] [4B CRC32 (IEEE) of payload]

Requirements
************

The application supports the following development kit:

.. table-from-sample-yaml::

The host PC requires Python 3 with ``pyserial``, ``numpy``, ``pillow``, and ``opencv-python`` to run the :file:`scripts/feed_images_uart.py` script.

On the nRF9251 DK, the UARTs are used as follows:

* ``uart133`` (VCOM0) - Console logs, including the classification results.
* ``uart132`` (VCOM1) - Frame input from the host, at 115200 baud.

User interface
**************

LED0 (frame LED):
   Toggles each time a valid frame is received.

LED1 (fire LED):
   Turns on when the ``FIRE`` probability reaches :kconfig:option:`CONFIG_WILDFIRE_SCORE_THRESHOLD` (default 50%).

Configuration
*************

|config|

Configuration options
=====================

.. _CONFIG_WILDFIRE_SCORE_THRESHOLD:

CONFIG_WILDFIRE_SCORE_THRESHOLD
   ``FIRE`` probability, in per mille, at which LED1 turns on.

.. _CONFIG_WILDFIRE_RX_BUF_SIZE:

CONFIG_WILDFIRE_RX_BUF_SIZE
   Size of the input UART receive ring buffer.

Building and running
********************

.. |application path| replace:: :file:`applications/wildfire_classification`

.. include:: /includes/application_build_and_run.txt

To build and flash the application, run:

.. code-block:: console

   west build -p -b nrf9251dk/nrf9251/cpuapp applications/wildfire_classification
   west flash

Testing
=======

|test_application|

#. |connect_kit|
#. Open a terminal on VCOM0 (115200 baud) and reset the kit.
   The application logs ``Model wildfire (96x96) waiting for frames from host``.
#. Run the feeder script on VCOM1 with forest pictures, with and without fire or smoke:

   .. code-block:: console

      python3 applications/wildfire_classification/scripts/feed_images_uart.py \
         --port /dev/serial/by-id/usb-SEGGER_J-Link_<serial>-if02 --loop path/to/forest/images/

   Use ``--size`` to send larger frames, for example ``--size 192``.
   Larger frames take longer to transfer at 115200 baud.
   Use ``--interval`` to change the time between images and ``--shuffle`` to send them in random order.

#. Observe the image shown by the script and the classification result on the console.

Application output
==================

The following is an example output:

.. code-block:: console

   <inf> main: Host input on uart@9b5000 (RX only)
   <inf> main: Model wildfire (96x96) waiting for frames from host
   <inf> main: Frame 1238 (96x96) received
   <inf> main: Class 1: FIRE (99.5%)
   <inf> main: Frame 1240 (96x96) received
   <inf> main: Class 0: NO_FIRE (99.7%)

Memory footprint
================

On the nRF9251 DK application core, the application uses 335956 B (74%) of flash and 142816 B (40%) of RAM.

Dependencies
************

This application uses the following |EAI| library:

* :ref:`lib_axon`

This application uses the following Zephyr libraries:

* :ref:`zephyr:logging_api`
* :ref:`zephyr:uart_api`
* :ref:`zephyr:ring_buffers_api`
