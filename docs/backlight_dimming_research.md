# Backlight Dimming — Research & Implementation Options

**Date:** 2026-05-03  
**Firmware version at research:** 1.4.0  
**Status:** Not yet implemented — options documented for execution

---

## Background

The v1.4.0 night sleep schedule turns the backlight fully off (`setBacklightBrightness(0)`) at the scheduled time. The goal is to add true **dimming** — a configurable low brightness during the sleep window — rather than a hard on/off.

The CLAUDE.md already notes the constraint:

> Backlight is digital on/off only. `setBacklightBrightness()` in `src/display_hw.cpp` drives the CH422G GPIO as 0/1 — there is no PWM path. PWM dimming would require hardware modification or a different expander pin routing.

---

## Hardware Constraint

The backlight is controlled via pin `CH422G_LCD_BL` (value `2`) on the **CH422G I2C GPIO expander** (I2C address `0x38`). The CH422G is a simple push-pull digital expander with no PWM hardware. Its output is either 0 V or ~3.3 V — nothing in between.

The CH422G output drives the gate of an N-channel MOSFET on the backlight circuit (standard topology for these Waveshare boards). The MOSFET source is GND; the drain drives the LED cathode side. PWM on the MOSFET gate produces proportional brightness.

Current firmware relevant files:

| File | Role |
|:-----|:-----|
| `src/display_hw.cpp` | `setBacklightBrightness(uint8_t)` — sets CH422G pin 0 or 1 |
| `include/display_hw.h` | Declaration of `setBacklightBrightness()` |
| `src/main.cpp` | Sleep state machine, all call sites of `setBacklightBrightness()` |
| `include/config.h` | Sleep schedule constants in `cfg::` namespace |

Current `setBacklightBrightness()` signature already accepts `uint8_t brightness` (0–255), so any replacement implementation is a drop-in at the call sites — the API does not need to change.

---

## Current Call Sites (`src/main.cpp`)

All calls should continue to work unchanged after any of the options below; only the implementation in `display_hw.cpp` changes (plus any new init code).

Search term: `setBacklightBrightness` — confirmed call sites at lines: 1150, 1190, 1522, 2855, 2880, 2888, 2889, 3047.

---

## Option 1 — FreeRTOS Software PWM via CH422G I2C

### Principle

Rapidly toggle `CH422G_LCD_BL` via I2C at ~200 Hz with a configurable duty cycle. The eye integrates the on/off pulses and perceives intermediate brightness.

### Feasibility

At 400 kHz I2C (fast mode), each I2C write transaction (start + address byte + data byte + stop) takes roughly 100–150 µs including Arduino Wire overhead. At 200 Hz PWM, a full cycle is 5 ms. Two I2C writes per cycle (HIGH then LOW) = 400 writes/second = ~60 ms/s of I2C time ≈ 0.6% of a core. Trivial CPU load.

200 Hz is above the flicker fusion threshold for most viewers under typical conditions. At very low duty cycles (< 10%) some peripheral flicker may be visible; 15–20% duty is the practical night-dim floor.

### Wire Bus Conflict

Both the PWM task and GT911 touch polling (`touch.cpp`, `pollTouch()`) use the same `Wire` instance. A mutex is required.

### Implementation Sketch

**`include/display_hw.h`** — add:
```cpp
void startBacklightPwm();          // call after initBoardHardware()
void setBacklightDuty(uint8_t pct); // 0–100 %; 100 = full on
```

**`src/display_hw.cpp`** — add:
```cpp
static SemaphoreHandle_t sWireMutex = nullptr;
static uint8_t           sPwmDuty   = 100;   // %
static TaskHandle_t      sPwmTask   = nullptr;

static void backlightPwmTask(void*) {
  const uint32_t periodMs = 5; // 200 Hz
  while (true) {
    uint8_t duty = sPwmDuty;
    if (duty == 0) {
      if (xSemaphoreTake(sWireMutex, portMAX_DELAY)) {
        ch422gSetPin(CH422G_LCD_BL, 0);
        xSemaphoreGive(sWireMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (duty >= 100) {
      if (xSemaphoreTake(sWireMutex, portMAX_DELAY)) {
        ch422gSetPin(CH422G_LCD_BL, 1);
        xSemaphoreGive(sWireMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    uint32_t onMs  = (periodMs * duty) / 100;
    uint32_t offMs = periodMs - onMs;
    if (onMs < 1) onMs = 1;
    if (offMs < 1) offMs = 1;
    if (xSemaphoreTake(sWireMutex, portMAX_DELAY)) {
      ch422gSetPin(CH422G_LCD_BL, 1);
      xSemaphoreGive(sWireMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(onMs));
    if (xSemaphoreTake(sWireMutex, portMAX_DELAY)) {
      ch422gSetPin(CH422G_LCD_BL, 0);
      xSemaphoreGive(sWireMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(offMs));
  }
}

void startBacklightPwm() {
  sWireMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(backlightPwmTask, "bl_pwm", 2048, nullptr, 2, &sPwmTask, 0);
}

void setBacklightDuty(uint8_t pct) {
  sPwmDuty = pct > 100 ? 100 : pct;
}

// Replace existing setBacklightBrightness():
void setBacklightBrightness(uint8_t brightness) {
  setBacklightDuty(brightness == 0 ? 0 : 100); // preserve on/off behaviour
  // OR: setBacklightDuty(map(brightness, 0, 255, 0, 100)); for proportional
}
```

**`touch.cpp`** — wrap every `Wire.beginTransmission()` with `xSemaphoreTake / xSemaphoreGive`. Declare `extern SemaphoreHandle_t gWireMutex` and expose it from `display_hw.cpp`.

**`main.cpp`** — call `startBacklightPwm()` at end of `setup()`. For the night dim state, call `setBacklightDuty(15)` instead of `setBacklightBrightness(0)`.

### Pros / Cons

| | |
|:--|:--|
| **Pro** | No hardware modification |
| **Pro** | Configurable brightness level (WebUI can expose a night-dim slider) |
| **Pro** | Drop-in with existing sleep state machine |
| **Con** | Wire mutex required across `display_hw.cpp` and `touch.cpp` |
| **Con** | ~200 Hz minimum; may show mild flicker at ≤ 10% duty |
| **Con** | ~80 lines of new code |

---

## Option 2 — Hardware Modification: Tap MOSFET Gate (Recommended)

### Principle

The CH422G EXIO2 output drives the backlight MOSFET gate through a series resistor (~10 kΩ typical). Soldering a wire from a free ESP32-S3 GPIO to that gate (with a series resistor to avoid conflict) allows the ESP32 to drive it with hardware LEDC PWM — completely flicker-free at any frequency.

CH422G_LCD_BL is then left permanently HIGH (gate enable asserted), and all brightness control passes through the LEDC channel.

### Free ESP32-S3 GPIOs

All RGB data, sync, I2C, and touch INT pins are accounted for. The following GPIOs are unassigned in `config.h` and suitable for LEDC output:

| GPIO | Notes |
|:-----|:------|
| **11** | Recommended — general purpose, no special function |
| **12** | Recommended — general purpose |
| **13** | General purpose |
| **15** | General purpose |
| **16** | General purpose |

Avoid GPIO 19/20 (USB D−/D+) and GPIO 43/44 (UART0 TX/RX).

### Hardware Steps

1. Power off the board completely.
2. With a multimeter in continuity mode, locate the trace from CH422G EXIO2 (pin 18 of CH422G) that leads to the MOSFET gate. On these Waveshare boards there is typically a 10 kΩ series resistor between the expander and the gate — the via or resistor pad on the gate side is the tap point.
3. Solder a short wire from that tap point to the chosen free GPIO (e.g. GPIO 11) through a 10 kΩ series resistor.
4. Do **not** cut the original trace — the CH422G pin held HIGH acts as a weak pull-up; the ESP32 GPIO driving the gate with LEDC overrides it easily. If the MOSFET gate is driven HIGH by CH422G and PWM LOW by the ESP32 simultaneously, the resistor limits contention. Alternatively, reconfigure CH422G_LCD_BL = LOW in firmware to cede full control to the ESP32 (preferred).

### Firmware Changes

Add to `config.h`:
```cpp
constexpr int kPinBacklightPwm = 11;  // or whichever GPIO was tapped
```

Replace `setBacklightBrightness()` in `display_hw.cpp`:
```cpp
void initBoardHardware(uint8_t brightness) {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  // ... existing CH422G init ...
  ch422gSetPin(CH422G_LCD_BL, 0); // cede gate control to ESP32 LEDC
  ch422gSetPin(CH422G_LCD_RST, 0);
  delay(10);
  ch422gSetPin(CH422G_LCD_RST, 1);
  delay(100);

  ledcSetup(0, 1000, 8);                   // channel 0, 1 kHz, 8-bit
  ledcAttachPin(cfg::kPinBacklightPwm, 0);
  setBacklightBrightness(brightness);
}

void setBacklightBrightness(uint8_t brightness) {
  ledcWrite(0, brightness);                 // 0 = off, 255 = full
}
```

No other changes needed — all existing call sites remain valid.

### Night Dim Level

Add to `config.h` and NVS persistence:
```cpp
constexpr uint8_t kSleepDimBrightness = 30; // 0–255; ~12% — adjust to taste
```

In the sleep state machine (`pollSleepSchedule()`), replace `setBacklightBrightness(0)` with `setBacklightBrightness(cfg::kSleepDimBrightness)` for the `SLEEP_DARK` state, or introduce a new `SLEEP_DIM` state if a two-level night schedule is wanted.

### Pros / Cons

| | |
|:--|:--|
| **Pro** | True hardware PWM — flicker-free at 1 kHz |
| **Pro** | Zero changes to Wire, touch, or sleep state machine |
| **Pro** | Minimal firmware delta (~10 lines) |
| **Pro** | Existing `setBacklightBrightness(uint8_t)` API is a perfect fit |
| **Con** | Requires soldering (fine-tip iron, ~0402 pad or via) |
| **Con** | Identifying the correct pad requires tracing the PCB |

---

## Option 3 — Framebuffer Pixel Scaling (Software Only)

### Principle

Scale all pixel RGB values in `renderScratch` before `pushSprite()`. The backlight stays at full brightness; only the image is darkened.

```cpp
// In renderTaskFn(), after render completes, before cache copy:
if (nightBrightnessScale < 100) {
  uint16_t* buf = (uint16_t*)renderScratch.getBuffer();
  int count = cfg::kScreenWidth * cfg::kScreenHeight;
  for (int i = 0; i < count; i++) {
    uint16_t px = buf[i];
    uint8_t r = ((px >> 11) & 0x1F) * nightBrightnessScale / 100;
    uint8_t g = ((px >> 5)  & 0x3F) * nightBrightnessScale / 100;
    uint8_t b = ( px        & 0x1F) * nightBrightnessScale / 100;
    buf[i] = (r << 11) | (g << 5) | b;
  }
}
```

800 × 480 = 384 000 pixels; at ~3 ns/op on ESP32-S3 this adds roughly 5–8 ms to each render on Core 1.

### Verdict

**Do not use as the sole dimming mechanism for night mode.** The backlight LEDs remain at full power, so:
- Light bleeds into a dark room just as much as at full brightness
- Power consumption is unchanged
- The visual effect is a dim image behind a bright backlight glow

Could be combined with Option 1 or 2 for extra brightness reduction, but adds render overhead for limited gain. Not recommended as a standalone night-dim solution.

---

## Recommended Approach

**Option 2 (hardware mod) if comfortable with a soldering iron.** It fits the existing API exactly and requires the fewest firmware changes. GPIO 11 is the suggested tap target.

**Option 1 (FreeRTOS software PWM) if no hardware modification is acceptable.** The Wire mutex is the only meaningful complexity; the rest is straightforward.

Option 3 is not worth implementing as a standalone solution.

---

## Sleep State Machine Changes (either Option 1 or 2)

The current `SleepPhase` enum and state machine in `main.cpp` supports a clean extension:

```
SLEEP_AWAKE → SLEEP_PENDING → SLEEP_DIM → SLEEP_DARK
                                  ↑               |
                              touch/WebUI     (optional deeper off)
```

Or more simply: replace `SLEEP_DARK` behaviour from "backlight off" to "backlight dim" — no new states needed.

The `kSleepDimBrightness` config constant and its NVS key (`"dimBright"`) should be added to the `"sleepsch"` Preferences namespace alongside the existing schedule settings. The WebUI Night Schedule card can expose a "Dim level" slider (0–100%) that posts to `/api/config` as `sleepDimBrightness`.

---

## Related Files to Update

| File | Change |
|:-----|:-------|
| `src/display_hw.cpp` | Replace `setBacklightBrightness()` per chosen option |
| `include/display_hw.h` | Add any new function declarations |
| `include/config.h` | Add `kSleepDimBrightness` (and `kPinBacklightPwm` for Option 2) |
| `src/main.cpp` | Update sleep state machine; add NVS load/save for dim level |
| `include/CLAUDE.md` | Update backlight note once implemented |
| `CHANGELOG.md` | Add entry under new version |

---

*Research by Claude Code — 2026-05-03*
