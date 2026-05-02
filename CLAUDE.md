# CLAUDE.md — ESP32S3-Weather

**Firmware version: 1.4.0**

## Target Hardware

**Waveshare ESP32-S3-Touch-LCD-7**

| Feature | Detail |
|:--------|:-------|
| MCU | ESP32-S3 dual-core 240 MHz |
| Flash | 16 MB |
| PSRAM | 8 MB OPI (`board_build.arduino.memory_type = qio_opi`) |
| Display | ST7262 7-inch 800×480 RGB parallel panel |
| Touch | GT911 capacitive — I2C at 0x5D, SDA=8, SCL=9, INT=4 |
| IO expander | CH422G — controls LCD backlight, LCD reset, touch reset |
| Driver library | **LovyanGFX only** — no TFT_eSPI in this project |

## What This Project Does

Live weather dashboard ported from Mirko Pavleski's (`mircemk`) Hackster project to PlatformIO. Displays slippy-map base tiles (Carto dark / OpenTopoMap / OSM) with PSRAM-backed 800×415 sprites, RainViewer radar overlay (no API key), optional OpenWeatherMap cloud/rain overlays, and Open-Meteo 16-day forecast. Touch navigates 8 graph pages (temperature, pressure, rain, cloud, humidity, wind, UV, solar) and cycles map zoom/layer/style. A local LAN WebUI at port 80 provides live TFT mirroring, render telemetry, and remote controls.

## Non-Obvious Pin Assignments

All 16 RGB data pins (`cfg::kPinD0`–`cfg::kPinD15`) plus DE/VSYNC/HSYNC/PCLK are defined in `include/config.h` inside the `cfg::` namespace. They are passed to the `WaveshareDisplay` class in `include/waveshare_display.h`, which constructs the LovyanGFX `Bus_RGB` at runtime.

CH422G expander logical pins:

| Constant | Value | Function |
|:---------|:------|:---------|
| `CH422G_TP_RST` | 1 | GT911 touch reset |
| `CH422G_LCD_BL` | 2 | Backlight enable (digital on/off) |
| `CH422G_LCD_RST` | 3 | LCD panel reset |

## Known Hardware Quirks

- **Backlight is digital on/off only.** `setBacklightBrightness()` in `src/display_hw.cpp` drives the CH422G GPIO as 0/1 — there is no PWM path. The on-screen brightness slider stores a value but has no hardware effect beyond panel on/off. PWM dimming would require hardware modification or a different expander pin routing.
- **PSRAM is required.** Four 800×415 sprites (`renderScratch`, `cacheRadar`, `cacheClouds`, `cacheRain`) live in PSRAM. `BOARD_HAS_PSRAM` must be set (it is, via build flags).
- **`--no-stub` is required for upload.** The pioarduino platform used here requires this flag; removing it will cause upload failure.
- **OTA auth is a placeholder.** `[env:esp32s3-7inch-ota]` has `--auth=change-me` — update before deploying OTA.
- **HTTPS without certificate verification.** `fetchPngToBuffer()` and `getWeatherData()` use `WiFiClientSecure` with `client.setInsecure()`. This works but skips certificate validation. Do not change without testing all tile sources.
- **C++17 in use.** Build flag is `-std=gnu++17` (deviates from global C++11 default). Structured bindings and `std::string_view` are available.
- **Heap guard on tile fetches.** Tiles are skipped if internal heap drops below 80 KB — TLS handshakes require contiguous internal RAM. A 50 ms `delay()` between tiles allows FreeRTOS to coalesce freed blocks.

## Library Stack

| Library | Version | Purpose |
|:--------|:--------|:--------|
| `lovyan03/LovyanGFX` | `^1.1.16` | RGB bus display driver |
| `esp-arduino-libs/esp-lib-utils` | `#v0.1.2` | ESP utility helpers |
| `tzapu/WiFiManager` | `^2.0.16-rc.2` | Captive portal provisioning |
| `bblanchon/ArduinoJson` | `^6.21.5` | JSON parsing |
| `bitbank2/PNGdec` | `^1.1.0` | PNG tile decoding into sprite |

**No ezTime.** This project uses Arduino `configTzTime()` / `getLocalTime()` with the POSIX tz string `cfg::kNtpTimezone`. Do not add ezTime unless doing a full time-handling refactor.

## Render Architecture (v1.3+)

Core 1 background render with retained per-layer caches:

| Symbol | Role |
|:-------|:-----|
| `renderScratch` | PSRAM scratch sprite that Core 1 renders into |
| `cacheRadar` / `cacheClouds` / `cacheRain` | Retained full-map PSRAM sprite cache for each layer |
| `layerCacheSprites[]` | Layer index to cache sprite mapping |
| `layerCaches[]` | Per-layer metadata: valid flag, zoom, map style, last update time |
| `mapFront` | Pointer to the cached sprite currently shown on screen |
| `renderTarget` | What PNG draw callbacks write to during a render (`renderScratch`) |
| `renderState` | `RENDER_IDLE` → `RENDER_BUSY` (Core 1) → `RENDER_READY` → `RENDER_IDLE` (Core 0 flip) |
| `renderPending` | Set when a new render is requested while one is already BUSY; re-triggers after flip |
| `renderTilesDone` / `renderTilesTotal` | Tile progress counters, read by Core 0 for the status overlay |
| `weatherRefreshPending` | Set by Core 0 when a realtime refresh is due; Core 1 calls `getWeatherData()` before rendering then clears it |
| `owmAuthFailed` | Set on first OWM 401 response; CLOUDS and RAIN layers auto-skipped for the session |
| `screenFrameVersion` | Incremented each time Core 0 pushes a new frame; WebUI mirror polls this to detect changes |
| `triggerRenderForLayer()` | Core 0 entry point — serves fresh cache immediately or schedules a network render |
| `renderTaskFn` | FreeRTOS task on Core 1 (32 KB stack, priority 1): waits via `xTaskNotifyGive()` → fetches weather if `weatherRefreshPending` → renders scratch → copies to layer cache → sets READY |

**Rule:** `lcd.` (display bus) calls only from Core 0. Core 1 writes to PSRAM sprite only.

**Rule:** All HTTP fetches (weather data and map tiles) happen on Core 1. Core 0 never blocks on network I/O after startup, keeping the touchscreen responsive throughout.

Layer auto-cycle interval is `cfg::kLayerCycleSecs` (default 30 s). Touch on the layer badge resets the timer. OWM layers auto-skipped if key absent or 401 received.

Realtime API refresh interval is `cfg::kRealtimeRefreshSecs` (default 1800 s / 30 min). Network tile fetches happen only when a layer cache is missing, map style or zoom changes, or the realtime refresh interval expires.

### Render Diagnostics & Watchdog

`pollRenderWatchdog()` runs in `loop()` every frame. If a render stalls for more than `cfg::kRenderStallWarnMs` (default 10 s) without progress, it logs:

| Variable | Content |
|:---------|:--------|
| `renderDiagPhase` | Current phase name (`idle` / `http_begin` / `http_get` / `http_read` / `base_decode` / `overlay_fetch` / `overlay_decode` / `cache_copy` / `marker`) |
| `renderDiagUrl` | Tile URL being fetched (API key redacted via `redactUrlForLog()`) |
| `renderDiagTileIndex` / `renderDiagTileX` / `renderDiagTileY` | Tile coordinates in progress |

Stall warnings repeat every `cfg::kRenderStallRepeatMs` (default 10 s) until the render completes.

## Night Sleep Schedule (v1.4.0+)

When enabled, the display turns off at a configurable time each night and wakes on physical touch or WebUI touch.

**Sleep phases (`SleepPhase` enum):**

| Phase | State |
|:------|:------|
| `SLEEP_AWAKE` | Normal operation |
| `SLEEP_PENDING` | Sleep message shown on LCD, backlight turns off after 2 s |
| `SLEEP_DARK` | Backlight off; touch or WebUI touch triggers wake |
| `SLEEP_TOUCH_WOKEN` | Woken by touch, shows full dashboard; re-enters PENDING after `kSleepWakeDurationSecs` |

**Window logic:** if `onMins >= offMins` (crosses midnight), window = `now >= on OR now < off`. Otherwise a same-day window.

**NVS persistence:** stored in Preferences namespace `"sleepsch"`. Keys: `enabled`, `onHour`, `onMin`, `offHour`, `offMin`, `wakeSecs`.

**Key globals:** `sleepPhase`, `sleepPhaseMs`, `sleepScheduleEnabled`, `sleepOnHour/Minute`, `sleepOffHour/Minute`.

**Key functions:** `pollSleepSchedule()` (called in `loop()` once/second), `drawSleepScreen()`, `exitSleepRestoreDashboard()`, `loadSleepSettings()`, `saveSleepSettings()`.

**Render guard:** when `sleepPhase == SLEEP_DARK` or `SLEEP_PENDING`, completed renders still update the layer cache but do not push to the LCD. Display restores correctly from cache on wake.

## WebUI (v1.3.2+)

A `WebServer` on port 80 starts automatically after WiFi connects. All endpoints serve from Core 0 via `handleWebUiClient()` in `loop()`.

| Endpoint | Method | Description |
|:---------|:-------|:------------|
| `/` | GET | Embedded HTML dashboard: live TFT mirror, layer status, render progress |
| `/api/status` | GET | JSON: frame version, render state/progress, layer cache ages, IP/RSSI/heap/PSRAM |
| `/screen.bmp` | GET | Current TFT as 24-bit BMP (800×480, RGB888, bottom-up) |
| `/api/touch` | POST | Inject touch event — JSON `{"x":N,"y":N}` or query params `?x=N&y=N` |
| `/api/config` | POST | Update config — JSON keys: `zoom`, `brightness`, `radarAlpha`, `cloudAlpha`, `rainAlpha` |
| `/api/reboot` | POST | `ESP.restart()` |
| `/api/reset-wifi` | POST | WiFiManager.resetSettings() + disconnect + restart |

The `/api/config` endpoint also accepts sleep schedule keys: `sleepEnabled` (bool), `sleepOnTime` / `sleepOffTime` ("HH:MM" strings). Changes are saved to NVS immediately.

The HTML root page polls `/api/status` for `frameVersion` changes, then fetches `/screen.bmp` to refresh the mirror image. The `sleep` object in the status JSON drives the Night Schedule card.

## Touch Architecture

- **GT911 polling:** 20 ms minimum between polls (enforced in `pollTouch()`). No interrupt mode — fully poll-based.
- **Coordinate transform:** `transformTouch()` in `src/touch.cpp` supports swap/invert flags; both are disabled for this hardware. Constrained to screen bounds before storing in `touch_last_x` / `touch_last_y`.
- **UI debounce:** `handleUiTouch()` enforces 600 ms minimum between dispatched touch events.

Touch zones on the map screen (`appState == 0`):

| Zone | Area | Action |
|:-----|:-----|:-------|
| Left edge | tx ≤ 32 | Brightness — map Y to 255 (top) … 20 (bottom) |
| Layer badge | ty < 40, tx 300–500 | Cycle layer (RADAR → CLOUDS → RAIN) |
| Map style badge | ty > 385, tx 310–490 | Cycle map style (DARK → TOPO → OSM) |
| Right buttons | tx > 760 | Graph pages 1–4 (T / P / R / C) |
| Left buttons | tx 32–66 | Graph pages 5–8 (H / W / U / S) |
| Map center | tx 250–550, ty 100–380 | Cycle zoom (wraps to `kMapZoomMin` at max) |

## Graph Pages

Eight graph pages, selected by touch buttons or WebUI. Each covers 16 days of forecast data from Open-Meteo:

| Page | `appState` | Data source | Chart style |
|:-----|:-----------|:------------|:------------|
| Temperature | 1 | `dMax[]` / `dMin[]` | Dual line, color coded; mini weather icons |
| Pressure | 2 | `dPress[]` | Yellow line |
| Rain | 3 | `dRain[]` | Blue bar chart |
| Cloud | 4 | `dCloud[]` | Shaded area |
| Humidity | 5 | `dHum[]` | Blue line |
| Wind | 6 | `dWind[]` | Orange line |
| UV Index | 7 | `dUV[]` | Magenta line |
| Solar radiation | 8 | `dSolar[]` | Yellow bar chart |

All pages: auto-scaled Y axis, weekend background shading (Saturday blue / Sunday red), day-of-week + date labels, BACK button bottom-left.

## File Map

| File | Purpose |
|:-----|:--------|
| `src/main.cpp` | All UI rendering, HTTP fetch, touch dispatch, weather/radar logic (~3100 lines) |
| `include/waveshare_display.h` | LovyanGFX device class — RGB bus and panel config |
| `include/display_hw.h` / `src/display_hw.cpp` | CH422G init, backlight on/off, touch reset sequence |
| `include/touch.h` / `src/touch.cpp` | GT911 I2C driver — poll-based, no interrupt |
| `include/config.h` | All user-tuneable constants and pin definitions (`cfg::` namespace) |
| `include/debug.h` | Leveled `DBG_ERROR` / `DBG_WARN` / `DBG_INFO` / `DBG_VERBOSE` macros |
| `include/secrets.h` | Gitignored — WiFi credentials + OWM API key |
| `include/secrets.example.h` | Template for `secrets.h` |
| `ENHANCEMENTS.md` | Proposed base-map cache separation optimization (tracked, not yet implemented) |
| `docs/weather_dashboard_functional_description.md` | Functional description of the dashboard |

## User-Tuneable Settings (`include/config.h`)

All settings are in the `cfg::` namespace:

```cpp
cfg::kLocationName           // display label
cfg::kLocationLatitude       // decimal degrees, south = negative
cfg::kLocationLongitude      // decimal degrees, east = positive
cfg::kMapZoom                // startup zoom level (default 7)
cfg::kMapZoomMin             // minimum touch-cycle zoom (default 5)
cfg::kMapZoomMax             // maximum touch-cycle zoom (default 12 — overlay tiles top out here)
cfg::kDefaultMapStyle        // 0 = dark (CartoDB), 1 = topo (OpenTopoMap), 2 = OSM
cfg::kBaseMapContrastPercent // base map contrast; 100 = unchanged (default 125)
cfg::kBaseMapBrightness      // -255 to 255 (default +18)
cfg::kRadarOverlayAlphaPercent // radar overlay opacity 0–100 (default 55)
cfg::kCloudOverlayAlphaPercent // cloud overlay opacity 0–100 (default 25)
cfg::kRainOverlayAlphaPercent  // rain overlay opacity 0–100 (default 25)
cfg::kWifiApName             // captive portal SSID
cfg::kOtaHostname            // ArduinoOTA hostname (default "ESP32S3-Weather")
cfg::kNtpTimezone            // POSIX tz string (default AEST-10AEDT,M10.1.0,M4.1.0/3)
cfg::kLayerCycleSecs         // UI layer auto-cycle interval (default 30 s)
cfg::kRealtimeRefreshSecs    // weather/map API refresh interval (default 1800 s)
cfg::kRenderTaskStackBytes   // FreeRTOS render task stack size (default 32 KB)
cfg::kRenderTaskPriority     // FreeRTOS render task priority (default 1)
cfg::kRenderStallWarnMs      // ms before stall warning is logged (default 10 000)
cfg::kRenderStallRepeatMs    // ms between repeated stall warnings (default 10 000)
cfg::kSleepScheduleEnabled   // master enable for night schedule (default false)
cfg::kSleepOnHour            // sleep-start hour (default 22)
cfg::kSleepOnMinute          // sleep-start minute (default 0)
cfg::kSleepOffHour           // wake hour (default 7)
cfg::kSleepOffMinute         // wake minute (default 0)
cfg::kSleepWakeDurationSecs  // touch-wake stay-awake duration (default 300 s)
```

All sleep settings are persisted in NVS and overridden via the WebUI Night Schedule card. Config.h values serve as factory defaults only.

Overlay alpha values can also be updated at runtime via the WebUI `/api/config` endpoint — changes take effect on next render.

## Debug Levels

Set via `-DDEBUG_LEVEL=N` in `platformio.ini` build flags (default 3):

| Level | Macro | Use |
|:------|:------|:----|
| 1 | `DBG_ERROR` | Critical failures |
| 2 | `DBG_WARN` | Unexpected but recoverable |
| 3 | `DBG_INFO` | State changes, render/cache events, per-tile fetch timing |
| 4 | `DBG_VERBOSE` | Raw malformed PNG dumps and very detailed diagnostics |

Level 4 also enables a raw hex/text dump of malformed PNG overlay responses and per-tile detailed logging.

## Flashing Notes

- Upload port: `/dev/cu.usbmodem5AAF2820151` (check `platformio.ini` if upload fails — port changes between Macs)
- Upload speed: 230400 (`--no-stub` required by pioarduino)
- OTA env: `ESP32S3-Weather.local` — update `--auth` before use
- After first flash: GT911 touch needs a power cycle if unresponsive; software reset alone is insufficient

## Partition Table

16 MB flash — app slots 5 MB each, 6 MB LittleFS, 64 KB coredump:

```
nvs        0x9000   20 KB
otadata    0xe000    8 KB
app0      0x10000    5 MB
app1     0x510000    5 MB
spiffs   0xA10000    6 MB   (mounted as LittleFS)
coredump 0xFF0000   64 KB
```

`spiffs` subtype is correct even when mounted as LittleFS — this is expected Arduino ESP32 behaviour.
