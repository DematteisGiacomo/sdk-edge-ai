# Person detection on nRF9251 DK — hardware setup

Wiring guide for **ArduCam Mega** + **SparkFun Level Shifter (TXS0108E)** on the **nRF9251 DK**.

| Item | Value |
|------|--------|
| Build target | `nrf9251dk/nrf9251/cpuapp` |
| Devicetree overlay | `boards/nrf9251dk_nrf9251_cpuapp.overlay` |

P1 GPIO is **1.8 V**. The camera SPI is **3.3 V**. Use the level shifter between DK and camera.

---

## Level shifter pins (TXS0108E)

| Label on board | Connect to |
|----------------|------------|
| **VA** | **1.8 V** — nRF9251 DK **Port 1 `1V8IO`** |
| **VB** | **3.3 V** — from the **nRF9251 DK** (see next section) |
| **OE** | **VA** (same net as **`1V8IO`**) |
| **GND** | DK **GND** |
| **A1 … A8** | nRF9251 **P1** SPI (low-voltage side) |
| **B1 … B8** | ArduCam SPI (high-voltage side) |

Use matching pairs (A1↔B1, A2↔B2, …). One pair per SPI signal.

---

## 3.3 V — use the nRF9251 DK?

**Yes.** Power **VB** and ArduCam **VCC** from the **3.3 V output on the nRF9251 DK** (USB powered). You do **not** need a separate bench supply for a normal lab setup.

**Use this DK pin**

| DK label | Connect to |
|----------|------------|
| **`3V3IO`** (or **`3V3`**) on the **power** header — often near **RFGPIO** / **MIPI** | Shifter **VB** + ArduCam **VCC** |
| **`GND`** (same connector area) | Shifter **GND** + ArduCam **GND** |

Before wiring the camera, check with a multimeter: **`3V3IO` to `GND` ≈ 3.3 V** (about 3.2–3.4 V is fine).

**Do not use these for 3.3 V**

| DK label | Why |
|----------|-----|
| **`1V8IO`** (Port 1) | **1.8 V only** — use for **VA** and **OE**, never for camera or **VB** |
| **`MIPI VIO`** / **`VIO`** | **1.8 V** MIPI reference — not 3.3 V |
| **RFGPIO** pin names (e.g. RFGPIO0…) | **Signals**, not power |

**You cannot** make Port 1 SPI 3.3 V by changing software or Board Configurator. P1 stays **1.8 V** on nRF9251; the level shifter is still required for SPI.

**If the camera does not start** and **`3V3IO` drops** when you connect **VCC**, the DK rail may be current-limited. Then use an external **3.3 V** supply (still **common GND** with the DK).

Pin locations: [nRF9251 DK hardware user guide](https://docs.nordicsemi.com/bundle/ug_nrf9251_dk/page/ug/nrf9251_dk/hw_description/hw_description.html).

---

## DK power summary

| DK pin | Connect to |
|--------|------------|
| Port 1 **`1V8IO`** | Shifter **VA** and **OE** |
| **`3V3IO`** (3.3 V supply) | Shifter **VB** and ArduCam **VCC** |
| **`GND`** | Shifter **GND** and ArduCam **GND** |

## Ground — tie everything together

**Yes.** Connect **DK GND**, **level shifter GND**, and **ArduCam GND** on the **same net** (star or daisy-chain from one DK **GND** pin is fine). SPI and the shifter only work when every part shares that reference.

If you add an **external 3.3 V** supply, connect its **negative / GND** to **DK GND** as well. Do **not** run the camera on a separate “floating” supply with no GND link to the DK.

One **GND** wire to the shifter and one **GND** to the camera is enough if those two grounds are also tied on the breadboard or module.

---

## Wiring overview

```
nRF9251 DK (Port 1)                 TXS0108E                       ArduCam Mega

P1.0  SCK  ----------------------->  A1  <->  B1  ----------------->  SCK
P1.8  MOSI ----------------------->  A2  <->  B2  ----------------->  MOSI
P1.4  MISO <-----------------------  A3  <->  B3  <-----------------  MISO
P1.7  CS   ----------------------->  A4  <->  B4  ----------------->  CS

1V8IO  --->  VA, OE
3V3IO  --->  VB
GND    --->  GND

3V3IO  ----------------------------------------------->  VCC

P1.5, P1.6  —  UART console (do not connect to shifter or camera)
```

---

## Connection table

Wire with the DK **unpowered**.

### Shifter power

| From (DK) | To (shifter) |
|-----------|----------------|
| Port 1 **`1V8IO`** | **VA** |
| Port 1 **`1V8IO`** | **OE** |
| **`3V3IO`** (DK 3.3 V) | **VB** |
| **GND** | **GND** |

### SPI (example channels)

| DK (Port 1) | Shifter A | Shifter B | ArduCam |
|-------------|-----------|-----------|---------|
| **P1.0** SCK | A1 | B1 | SCK |
| **P1.8** MOSI | A2 | B2 | MOSI |
| **P1.4** MISO | A3 | B3 | MISO |
| **P1.7** CS | A4 | B4 | CS |

Other A/B pairs work if all four lines use matching pairs.

### Camera power

| From (DK) | To (ArduCam) |
|-----------|----------------|
| **`3V3IO`** (DK 3.3 V) | **VCC** |
| **GND** | **GND** |

### Do not connect

- Camera SPI directly to P1 (bypass shifter)
- **3.3 V** to **`1V8IO`**
- **P1.5** or **P1.6** to shifter or camera

---

## Power-up

1. Finish wiring with USB unplugged.
2. Power the DK from USB.
3. Verify **`1V8IO` ≈ 1.8 V**, **`3V3IO` ≈ 3.3 V**, **OE** tied to **VA**.
4. Flash and run the application.

```bash
west build -b nrf9251dk/nrf9251/cpuapp applications/person_detection
west flash
```

Expect `Person detection start` on the serial console; ArduCam init messages if the camera is detected.

---

## Static input (no camera)

Use this to verify **Axon NPU + person_det model** on nRF9251 (or any supported DK) **without** ArduCam, SPI, or a level shifter. The app runs inference on a **prefilled neutral-gray** tensor every 500 ms.

Paths below are **relative to the application** (`applications/person_detection`), which is what Zephyr expects for `-DEXTRA_CONF_FILE` and `-DDTC_OVERLAY_FILE`.

```bash
west build -b nrf9251dk/nrf9251/cpuapp applications/person_detection -- \
  -DEXTRA_CONF_FILE=conf/static_input.conf \
  -DDTC_OVERLAY_FILE=boards/nrf9251dk_nrf9251_cpuapp_static_input.overlay
west flash
```

On the console you should see:

- `Person detection start (static input, no camera)`
- `No detections` (gray input has no person)

If inference fails, the log shows `Inference failed` or an earlier Axon init/validate error.

For **nRF54LM20 DK**, use `conf/static_input.conf` and either disable SPI in an overlay or use a dedicated static overlay (enable `&axon` only; do not include the ArduCam SPI nodes).

### Static image + desktop viewer (nRF9251)

Embed a **128×128** photo (RGB565), run **person_det**, and stream frame + boxes over the **J-Link virtual COM** port (same binary protocol as the LM20 USB demo).

**1. Embed your image** (from `edge-ai`, requires `pip install pillow`):

```bash
python3 applications/person_detection/scripts/embed_static_image.py /path/to/photo.jpg
```

This writes `applications/person_detection/src/generated/static_image_rgb565.h` (rebuild after changing the photo).

**2. Build and flash** (paths relative to the application directory).

*Viewer only (binary on VCOM0, no text logs):*

```bash
west build -b nrf9251dk/nrf9251/cpuapp applications/person_detection -- \
  -DEXTRA_CONF_FILE=conf/static_input_host.conf \
  -DDTC_OVERLAY_FILE="boards/nrf9251dk_nrf9251_cpuapp_static_input.overlay;boards/nrf9251dk_nrf9251_cpuapp_static_host.overlay"
west flash
```

*Logs on VCOM0 + viewer on VCOM1 (recommended):*

```bash
west build -b nrf9251dk/nrf9251/cpuapp applications/person_detection -- \
  -DEXTRA_CONF_FILE=conf/static_input_host_dual.conf \
  -DDTC_OVERLAY_FILE="boards/nrf9251dk_nrf9251_cpuapp_static_input.overlay;boards/nrf9251dk_nrf9251_cpuapp_static_host_dual.overlay"
west flash
```

| Host port | nRF9251 UART | DK pins | Use |
|-----------|--------------|---------|-----|
| **VCOM0** / `ttyACM0` | uart133 | P1.5 RX, P1.6 TX | Console logs @ **115200** |
| **VCOM1** / `ttyACM1` | uart132 | P2.4 RX, P2.5 TX | Binary viewer @ **115200** |

**3. On the PC**, open the viewer **before** reset (firmware waits ~3 s, then streams every ~4.5 s):

```bash
pip install pyserial opencv-python numpy
# Dual-UART build:
python3 applications/person_detection/scripts/view_person_stream.py --port /dev/ttyACM1 --baud 115200
# Single-UART build (logs off):
python3 applications/person_detection/scripts/sniff_person_stream.py --port /dev/ttyACM1 --baud 115200
python3 applications/person_detection/scripts/view_person_stream.py --port /dev/ttyACM0 --baud 115200
```

Watch logs on **ACM0** at **115200** (dual build only). Press **q** in the viewer to quit.

### CIFAR-10 image classification instead of person detection

`conf/model_cifar10_ic.conf` swaps MCUnet person detection for the MLPerf Tiny ResNet-8 CIFAR-10 classifier (`tiny/benchmark/training/image_classification`, compiled for Axon as `src/generated/nrf_axon_model_tinyml_ic_.h`). The 128×128 frame is box-averaged to 32×32. Instead of boxes, the firmware logs and streams the probability of each of the ten classes (airplane, automobile, bird, cat, deer, dog, frog, horse, ship, truck). CIFAR-10 has **no person class**.

| | Person detection | CIFAR-10 classification |
|---|---|---|
| Model input | 160×128×3 | 32×32×3 |
| Axon interlayer buffer | 229,376 B | 52,224 B |
| App RAM used (static dual build) | ~339 KiB (97%) | ~102 KiB (29%) |

To check the full pipeline with a known answer, embed one of the compiled model's CIFAR-10 test images. Vector 1 is an automobile:

```bash
python3 applications/person_detection/scripts/embed_static_image.py \
  --axon-test-vector tests/axon/compiled_models/nrf_axon_model_tinyml_ic_test_vectors_.h:1
west build -b nrf9251dk/nrf9251/cpuapp applications/person_detection -- \
  -DEXTRA_CONF_FILE="conf/static_input_host_dual.conf;conf/model_cifar10_ic.conf" \
  -DDTC_OVERLAY_FILE="boards/nrf9251dk_nrf9251_cpuapp_static_input.overlay;boards/nrf9251dk_nrf9251_cpuapp_static_host_dual.overlay"
west flash
```

The log on VCOM0 should show `Class 1: AUTOMOBILE (...)` with close to 100% (the reference output for this vector). The viewer prints each result and draws the top-3 classes over the frame.

### Feed images from the PC over UART (demo)

With `conf/host_input_dual.conf`, the firmware receives frames on **VCOM1** (RX only) and writes only to the log on **VCOM0**. Frames use the same RGB565 frame message as the viewer stream. With the CIFAR-10 model, any square multiple of 32×32 works (32×32 is about 2 KB, so about 0.2 s at 115200). Person detection needs 128×128.

```bash
west build -b nrf9251dk/nrf9251/cpuapp applications/person_detection -- \
  -DEXTRA_CONF_FILE="conf/host_input_dual.conf;conf/model_cifar10_ic.conf" \
  -DDTC_OVERLAY_FILE="boards/nrf9251dk_nrf9251_cpuapp_static_input.overlay;boards/nrf9251dk_nrf9251_cpuapp_static_host_dual.overlay"
west flash
```

Watch the log on VCOM0, then feed images on VCOM1. The script shows each image as it is sent:

```bash
# The model's CIFAR-10 test vectors:
python3 applications/person_detection/scripts/feed_images_uart.py --port /dev/ttyACM1 \
  --axon-test-vectors tests/axon/compiled_models/nrf_axon_model_tinyml_ic_test_vectors_.h --loop
# Your own photos (center-cropped and resized to --size):
python3 applications/person_detection/scripts/feed_images_uart.py --port /dev/ttyACM1 photos/
# Random CIFAR-10 test images, with the true label shown in the window:
python3 applications/person_detection/scripts/feed_images_uart.py --port /dev/ttyACM1 \
  --cifar-test-batch cifar-10-batches-py/test_batch --count 20
```

Each frame is logged as `Frame N (32x32) received` followed by `Class K: LABEL (xx.x%)`. Frames that fail the CRC check or have an unsupported size are dropped with a warning.

---

## Troubleshooting

### No output on `/dev/ttyACM0` or `/dev/ttyACM1`

1. **Match the firmware to the tool** — Text logs (`Person detection start`, …) need a normal serial terminal at **115200**. The Python viewer only works with **`conf/static_input_host.conf`** (UART logging off; binary stream). The default ArduCam build on **nRF54LM20 DK** does not use that stream (use USB viewer in `app-axon-person-detection` for LM20 live demo).
2. **Pick the correct VCOM** — On **nRF54LM20 DK**, Zephyr console is **uart20** (P1.16–P1.19), wired to **VCOM1** (often **`/dev/ttyACM1`**, not ACM0). On **nRF9251 DK**, console is **uart133** (P1.5/P1.6); use the J-Link VCOM that carries the app (try both ports after reset).
3. **Open the port, then reset** — J-Link tri-states UART until a terminal opens the VCOM. Use **hardware flow control (RTS/CTS)** if the port stays empty (`minicom -c on`, or `view_person_stream.py` which enables `--rtscts` by default).
4. **Board Configurator** — In nRF Connect Board Configurator, ensure **VCOM0/VCOM1** (and HWFC if used) are **connected**, not disconnected.
5. **Host-stream timing** — Open the viewer, press reset, wait **~3 s** for the first frame; frames repeat every **~4.5 s**. With logging disabled you will not see printable text on the UART.
6. **Bytes but no frames** — Run **`sniff_person_stream.py`** on **`ttyACM1` @ 115200**. **`magic_hits: 0`** → wrong port (stream is not on that VCOM). **`magic_hits` > 0, `frames_ok: 0`, CRC errors** → baud mismatch: reflash static overlays (**115200** on stream UART) or try **`--baud 1000000`** if you still run old 1M firmware.
7. **Logs on VCOM0 but nothing on VCOM1** — Dual build needs **both** overlays. Boot log should show **`Host stream on uart132 … (dedicated UART)`**.
8. **Viewer still open after reset** — Press **`r`** in the viewer, or restart the script, then reset the DK. **`--baud`** must match firmware (**115200** with current static overlays).

### `arducam mega not detected, 0x0`

The firmware read register `0x40` (sensor ID) and got **0x00**. SPI is running, but the MCU is **not seeing data from the camera on MISO**. A working Mega usually returns **`0x81`–`0x84`**.

Check in this order:

1. **Shifter enabled** — **OE** wired to **VA** (`1V8IO`), not left floating.
2. **A vs B** — **A1–A4** → DK **P1**; **B1–B4** → camera. If A and B are swapped, detection fails.
3. **MISO direction** — Camera **MISO / SDO** → **B3** → **A3** → **P1.4** (not MOSI).
4. **CS** — **P1.7** → **A4** → **B4** → camera **CS** (driver uses **CS active high**).
5. **SCK / MOSI** — **P1.0** → A1 → B1 → SCK; **P1.8** → A2 → B2 → MOSI/SDI.
6. **Power** — With USB on, **3V3IO** and camera **VCC** ≈ 3.3 V; **GND** common everywhere.
7. **Module** — Must be **ArduCam Mega** (SPI), not a different ArduCam family.

**nRF9251 DK note:** **P1.08** and **P1.09** are tied to the **NFCT antenna** on the DK. **MOSI on P1.8** can still work, but if everything else looks correct, try a **shorter MOSI lead** or confirm SCK/MOSI toggle with a scope.

**Quick wiring sanity check** (power off): continuity from **P1.4** to shifter **A3**, and from **A3** to **B3**; camera MISO on **B3** only.

If ID stays **`0x0`**, lower SPI speed for testing: in `boards/nrf9251dk_nrf9251_cpuapp.overlay`, set `spi-max-frequency = <1000000>;`, rebuild, and flash.

### `Video device not ready`

This follows a failed camera init. Fix detection first; then reboot after wiring changes.

---

## Bring-up: prove shifter + camera on nRF54LM20 DK

Use the **nRF54LM20 DK** to split “camera + shifter wiring” from “nRF9251 pin routing”. Same ArduCam and TXS0108E; only the DK and devicetree target change.

### What a pass/fail means

| Result | Likely cause |
|--------|----------------|
| LM20 **direct** (no shifter) works | Camera and firmware OK |
| LM20 **via shifter @ 1.8 V** works, nRF9251 still `0x0` | nRF9251 side: overlay pins, **P1.8 / NFCT**, header labels, or **SPI130** routing |
| LM20 **via shifter** also fails | Shifter (**OE**, A/B), camera power, MISO/CS, or module — not nRF9251-specific |

### Step A — baseline (no level shifter)

Official **3.3 V** wiring on LM20 (see `README.rst`): camera **directly** on **P1.4** SCK, **P1.5** MISO, **P1.6** MOSI, **P1.7** CS; **VCC** from **`VDD:IO`** (3.3 V).

```bash
west build -b nrf54lm20dk/nrf54lm20b/cpuapp applications/person_detection
west flash
```

Expect sensor ID **`0x81`–`0x84`** in the log (not `0x0`). If this fails, fix camera/power before testing the shifter.

### Step B — same stack as nRF9251 (level shifter)

Goal: **A-side = 1.8 V** (like nRF9251 **P1**), **B-side = 3.3 V** (camera).

1. In **Board Configurator** for nRF54LM20 DK, set **VDDIO** for the camera port to **1.8 V** (inverse of the normal 3.3 V camera note in `README.rst`). Flash/save board config if your flow requires it.
2. **VA** and **OE** → DK **1.8 V** (same rail as configured **VDDIO** — do **not** use 3.3 V for **VA**).
3. **VB** and camera **VCC** → DK **3.3 V** (`VDD:IO` at 3.3 V, or a separate 3.3 V supply with **common GND**).
4. **A-side** (low voltage) — matches `boards/nrf54lm20dk_nrf54lm20b_cpuapp.overlay`:

   | LM20 pin | Shifter A | Shifter B | Camera |
   |----------|-----------|-----------|--------|
   | **P1.4** SCK | A1 | B1 | SCK |
   | **P1.6** MOSI | A2 | B2 | MOSI |
   | **P1.5** MISO | A3 | B3 | MISO |
   | **P1.7** CS | A4 | B4 | CS |

5. Rebuild/flash the **LM20** target (same command as Step A).

**Important:** With **VA = 1.8 V**, the **A** pins must be driven at **1.8 V** only. Do **not** leave VDDIO at 3.3 V while tying SPI to the **A** side — that over-stresses the shifter low-voltage side.

If Step B works and nRF9251 does not, compare nRF9251 mapping (**P1.0 / P1.8 / P1.4 / P1.7**) and re-check **P1.8** vs NFCT on the 9251 DK.

### Step B failed (A OK) — fix the shifter path

You already proved **camera + firmware + LM20 pins** (Step A). Treat Step B as **wiring + power on the shifter**, not nRF9251.

**1. Confirm LM20 A-side pinout (not nRF9251)**

Still building **`nrf54lm20dk/nrf54lm20b/cpuapp`**? The **A** wires must go to **P1.4 / P1.6 / P1.5 / P1.7**, not P1.0 / P1.8 / P1.4 / P1.7.

**2. Camera must stay on 3.3 V in Step B**

After Board Configurator sets **VDDIO to 1.8 V**, the **`VDD:IO`** header may read **~1.8 V**. **Do not** use that for ArduCam **VCC** or **VB**.

Use a **fixed 3.3 V** DK pin (measure **≈ 3.2–3.4 V** to GND) for **VB** and camera **VCC**. Tie **VA** / **OE** to the **1.8 V** port supply (same as **P1** IO voltage). **GND** common.

| Net | Voltage |
|-----|---------|
| **VA**, **OE**, LM20 **P1** SPI | **1.8 V** |
| **VB**, camera **VCC** | **3.3 V** (separate measured pin) |

If **VCC** was **~1.8 V** in Step B, the camera will not answer on SPI (often **`0x0`**).

**3. Step B½ — shifter only at 3.3 V (optional, recommended)**

Before fighting 1.8 V domain settings, prove the **TXS0108E** wiring:

- Leave **VDDIO at 3.3 V** (Step A config).
- **VA** = **VB** = **OE** = same **3.3 V** from **`VDD:IO`**.
- **A-side** ← LM20 **P1.4 / P1.6 / P1.5 / P1.7**; **B-side** → camera (same B1–B4 as Step A signals).

Rebuild/flash LM20. If this **passes**, the shifter channels and **OE** are fine; then retry full Step B with **P1 @ 1.8 V** and split rails as in the table above. If **B½ fails**, fix **OE → VA**, **A↔B swap**, and **MISO** (**P1.5** ↔ **A3** ↔ **B3** ↔ camera MISO/SDO).

**4. Quick checks**

- **OE** wired to **VA**, not floating.
- **A** = DK side, **B** = camera side.
- **MISO** is camera **out** → **B3** (not MOSI).
- Log still **`0x0`**? Almost always **MISO/CS/OE** or **camera not on 3.3 V**.

### Step B½ failed (A OK) — isolate the shifter

Same **`0x0`** / no camera means the MCU is not receiving valid MISO through the breakout. Work in this order.

**1. Enable (most common on SparkFun board)**

The breakout pulls **OE low** with **~110 kΩ** until you override it. **OE must be wired to VA** (same 3.3 V as **VA** in B½). Measure **OE to GND** — it must be **~3.3 V**, not ~0 V.

**2. Short wires**

SparkFun reports SPI problems with **~15 cm (6")** jumpers on the TXS0108E. Use the **shortest** leads that reach; keep the shifter **close** to the DK and camera.

**3. Paired channels only**

Each signal uses one **An ↔ Bn** pair (e.g. SCK **A1–B1** only). Do not split SCK across A1 and B2.

**4. MISO bypass (find a dead channel)**

Leave **SCK, MOSI, CS** through the shifter. Run **MISO only** direct:

**P1.5** (LM20) **↔** camera **MISO / SDO** (3.3 V, no shifter).

Flash/run Step A build. If the camera **detects**, the fault is **A3–B3** (or that hop). Move MISO to **A5–B5** and rewire **P1.5 → A5**, MISO → **B5** to test another channel.

**5. CS bypass**

If MISO bypass is not enough, also wire **P1.7 ↔ CS** direct (keep SCK/MOSI on the shifter).

**6. Slower SPI**

In `boards/nrf54lm20dk_nrf54lm20b_cpuapp.overlay`, set `spi-max-frequency = <1000000>;`, rebuild, retry B½.

**7. Side swap at equal voltage (B½ only)**

With **VA = VB = 3.3 V**, try **MCU on B**, **camera on A** (still **A1–B1** pairs). At 1.8 V / 3.3 V on nRF9251, revert to **MCU → A**, **camera → B**, **VA = 1.8 V**, **VB = 3.3 V**.

**8. If bypass never helps**

Check **GND** (DK + shifter + camera), resolder shifter headers, or try another TXS0108E board — the IC is marginal for breadboard SPI even when wiring is correct.

### All SPI bypassed works; any line through TXS0108E fails

If **P1 ↔ camera direct** on SCK/MOSI/MISO/CS works, but **one or more** of those lines through the SparkFun **TXS0108E** fails (even with the other three bypassed), the camera and DK pins are fine. The blocker is the **translator chip + how it is wired**, not nRF9251 devicetree.

**VA must be powered whenever SPI uses the shifter.** Camera **VCC** on **VB** alone is not enough. With USB on, measure **VA**, **VB**, and **OE** on the breakout — all should match the intended low/high rails (B½: all **~3.3 V**; nRF9251: **VA ~1.8 V**, **VB ~3.3 V**). Unpowered **VA** looks like a dead shifter on every channel.

**Why one line through the shifter can break the whole bus**

The TXS0108E is a **fast auto-direction** translator. SparkFun documents **SPI failures with normal jumper length** (ringing / false edges). A distorted **SCK** alone can make every register read look like **`0x0`**, even if MISO is bypassed.

**Practical options**

| Approach | Notes |
|----------|--------|
| **LM20 without shifter** | **3.3 V** direct wiring (Step A) for app bring-up and model testing. |
| **Short, soldered adapter** | Solder DK + camera + shifter on perfboard / protoboard so leads are **well under ~5 cm**; retry B½. |
| **Different translator** | For **1.8 V ↔ 3.3 V SPI**, use **direction-controlled** parts (e.g. **SN74LVC1T45** ×4, or an **8-bit 245**-style bus transceiver wired per TI SPI level-shift app notes), not a long-wire **TXS0108E** breadboard. |
| **Second TXS0108E** | Rare, but worth ruling out a damaged breakout. |

Until **at least one** SPI line reliably passes through a translator on the LM20 (B½), fixing nRF9251 overlay pins will not help — the same shifter sits between **P1 (1.8 V)** and the camera on the 9251 DK.

---

## References

- [SparkFun TXS0108E hookup guide](https://learn.sparkfun.com/tutorials/level-shifter---8-channel-txs0108e-hookup-guide)
- Application documentation: `README.rst`
