# Project: ESP32S3-Weather

Live weather dashboard with slippy-map base tiles (Carto dark / OpenTopoMap / OSM), RainViewer radar overlay, optional OpenWeatherMap cloud/rain overlays, and Open-Meteo 16-day forecast. Touch navigates 8 graph pages and cycles map zoom/layer/style. WebUI at port 80 provides live TFT mirroring and remote controls. Version 1.5.2.

**Target:** Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3, 16 MB Flash, 8 MB OPI PSRAM, ST7262 800×480 RGB panel, GT911 I2C capacitive touch).

## Non-Obvious Pin & Hardware Config

All 16 RGB data pins (`cfg::kPinD0`–`cfg::kPinD15`) plus DE/VSYNC/HSYNC/PCLK are in `include/config.h` namespace `cfg::`, passed to `Bus_RGB` at runtime in `waveshare_display.h`.

CH422G expander (I2C control) logical pins: `CH422G_TP_RST` = 1 (GT911 reset), `CH422G_LCD_BL` = 2 (backlight), `CH422G_LCD_RST` = 3 (panel reset).

## Hardware Quirks & Workarounds

- **Backlight is digital on/off by default.** Uncomment `#define CFG_BACKLIGHT_PWM_ENABLED` in `config.h` to enable software PWM (100 Hz FreeRTOS task on Core 0). When active, `setBacklightDim(0–255)` provides proportional control used by sleep schedule. See `docs/backlight_dimming_research.md`.
- **PSRAM required.** Four 800×415 sprites live in PSRAM (`renderScratch`, `cacheRadar`, `cacheClouds`, `cacheRain`); `BOARD_HAS_PSRAM` must be set.
- **`--no-stub` required for upload.** pioarduino platform requires this flag; removing it causes upload failure.
- **HTTPS without cert verification.** `fetchPngToBuffer()` and `getWeatherData()` use `WiFiClientSecure` with `.setInsecure()`. Works but skips validation — do not change without testing all tile sources.
- **C++17 in use.** Build flag `-std=gnu++17`. Structured bindings and `std::string_view` available.
- **Heap guard on tile fetches.** Tiles skipped if internal heap < 80 KB (TLS handshakes need contiguous RAM). 50 ms `delay()` between tiles allows FreeRTOS coalescing.
- **GT911 touch requires power cycle.** After first flash, if unresponsive, power-cycle the board; software reset alone is insufficient.

## Libraries

| Library | Version | Purpose |
|:--------|:--------|:--------|
| `lovyan03/LovyanGFX` | `^1.1.16` | RGB bus display driver |
| `esp-arduino-libs/esp-lib-utils` | `#v0.1.2` | ESP utility helpers |
| `tzapu/WiFiManager` | `^2.0.16-rc.2` | Captive portal provisioning |
| `bblanchon/ArduinoJson` | `^6.21.5` | JSON parsing |
| `bitbank2/PNGdec` | `^1.1.0` | PNG tile decoding |

**No ezTime.** Uses Arduino `configTzTime()` / `getLocalTime()` with POSIX tz string `cfg::kNtpTimezone`.

## Render Architecture (Core 1 Background + Layer Cache)

**Rule:** `lcd.` calls only from Core 0. Core 1 writes PSRAM sprite only.  
**Rule:** All HTTP (tiles + weather) on Core 1. Core 0 never blocks on network I/O.

Per-layer retained PSRAM sprite cache:

| Symbol | Role |
|:-------|:-----|
| `renderScratch` | Core 1 renders into this |
| `cacheRadar` / `cacheClouds` / `cacheRain` | Full-map PSRAM sprite cache per layer |
| `renderState` | `IDLE` → `BUSY` (Core 1) → `READY` → `IDLE` (Core 0 flip) |
| `renderPending` | Re-triggers render if set during BUSY |
| `weatherRefreshPending` | Core 0 sets; Core 1 calls `getWeatherData()` before rendering |
| `owmAuthFailed` | Set on OWM 401; CLOUDS/RAIN auto-skipped for session |
| `screenFrameVersion` | Incremented per Core 0 frame push; WebUI polls for changes |
| `renderTaskFn` | FreeRTOS Core 1 task (32 KB stack, priority 1): waits `xTaskNotifyGive()` → fetches weather if pending → renders scratch → copies to layer cache → sets READY |

Layer auto-cycle `cfg::kLayerCycleSecs` (default 30 s); touch resets timer. Realtime refresh `cfg::kRealtimeRefreshSecs` (default 1800 s); tiles only fetched on cache miss, zoom/style change, or refresh interval expiry.

**Stall watchdog:** `pollRenderWatchdog()` in `loop()`. If no progress for `cfg::kRenderStallWarnMs` (default 10 s), logs phase name, URL (redacted), and tile coordinates; repeats every `cfg::kRenderStallRepeatMs`.

## Night Sleep Schedule (v1.4.0+)

Phases: `SLEEP_AWAKE` → `SLEEP_PENDING` (2 s countdown) → `SLEEP_DARK` (backlight off) → `SLEEP_TOUCH_WOKEN` (display on, re-enters PENDING after `kSleepWakeDurationSecs`).

**Window logic:** if `onMins >= offMins` (crosses midnight), window = `now >= on OR now < off`; else same-day.

**NVS:** Preferences namespace `"sleepsch"`. Keys: `enabled`, `onHour`, `onMin`, `offHour`, `offMin`, `wakeSecs`, `dim`. Overlay alphas in separate namespace `"overlays"`, keys `radar`, `clouds`, `rain` (0–100, default 50).

**Key globals:** `sleepPhase`, `sleepPhaseMs`, `sleepScheduleEnabled`, `sleepOn/OffHour/Minute`.

**Render guard:** when `SLEEP_DARK` or `SLEEP_PENDING`, completed renders update layer cache but do not push to LCD; display restores from cache on wake.

## WebUI Endpoints (Core 0)

| Endpoint | Method | Notes |
|:---------|:-------|:------|
| `/` | GET | Embedded HTML: live TFT mirror, layer status, render progress |
| `/api/status` | GET | JSON: frame version, render state/progress, cache ages, heap/PSRAM/RSSI |
| `/screen.bmp` | GET | Current TFT as 24-bit BMP (800×480, RGB888) |
| `/api/touch` | POST | Inject touch — JSON `{"x":N,"y":N}` or query params |
| `/api/config` | POST | Update config keys: `zoom`, `radarAlpha`, `cloudAlpha`, `rainAlpha`, `sleepEnabled`, `sleepOnTime`, `sleepOffTime` ("HH:MM"). Changes saved to NVS immediately. |
| `/api/reboot` | POST | `ESP.restart()` |
| `/api/reset-wifi` | POST | WiFiManager.resetSettings() + disconnect + restart |

## Touch & UI

**GT911:** 20 ms minimum poll interval. Poll-based (no interrupt). `transformTouch()` constrained to screen bounds; swap/invert flags disabled.

**UI debounce:** 600 ms minimum between dispatched touch events.

**Map zones (`appState == 0`):**

| Zone | Area | Action |
|:-----|:-----|:--------|
| Layer badge |