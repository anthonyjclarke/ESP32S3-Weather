# CLAUDE.md — ESP32S3-Weather

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

Live weather dashboard ported from Mirko Pavleski's (`mircemk`) Hackster project to PlatformIO. Displays slippy-map base tiles (Carto dark / OpenTopoMap / OSM) with a PSRAM-backed 800×415 sprite, RainViewer radar overlay (no API key), optional OpenWeatherMap cloud/rain overlays, and Open-Meteo 16-day forecast. Touch navigates graph pages (temperature, pressure, rain, cloud, humidity, wind, UV, solar) and cycles map zoom/layer/style.

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
- **PSRAM is required.** The RGB frame buffer and the 800×415 map sprite (`mapCanvas.setPsram(true)`) both live in PSRAM. `BOARD_HAS_PSRAM` must be set (it is, via build flags).
- **`--no-stub` is required for upload.** The pioarduino platform used here requires this flag; removing it will cause upload failure.
- **OTA auth is a placeholder.** `[env:esp32s3-7inch-ota]` has `--auth=change-me` — update before deploying OTA.
- **HTTPS without certificate verification.** `fetchPngToBuffer()` and `getWeatherData()` call `http.begin(url)` directly for HTTPS URLs. This works but skips certificate validation. Use `WiFiClientSecure` with `client.setInsecure()` explicitly if refactoring; do not change without testing all tile sources.

## Library Stack

| Library | Version | Purpose |
|:--------|:--------|:--------|
| `lovyan03/LovyanGFX` | `^1.1.16` | RGB bus display driver |
| `tzapu/WiFiManager` | `^2.0.16-rc.2` | Captive portal provisioning |
| `bblanchon/ArduinoJson` | `^6.21.5` | JSON parsing |
| `bitbank2/PNGdec` | `^1.1.0` | PNG tile decoding into sprite |

**No ezTime.** This project uses Arduino `configTzTime()` / `getLocalTime()` with the POSIX tz string `cfg::kNtpTimezone`. Do not add ezTime unless doing a full time-handling refactor — it adds a library dependency for a single time-of-day call.

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
| `triggerRenderForLayer()` | Core 0 entry point — serves fresh cache immediately or schedules a network render |
| `renderTaskFn` | FreeRTOS task on Core 1: waits → renders scratch → copies to layer cache → sets READY |

**Rule:** `lcd.` (display bus) calls only from Core 0. Core 1 writes to PSRAM sprite only.

Layer auto-cycle interval is `cfg::kLayerCycleSecs` (default 30 s, set in `include/config.h`). Touch on the layer badge resets the timer. OWM layers auto-skipped if key absent or 401 received.

Realtime API refresh interval is `cfg::kRealtimeRefreshSecs` (default 1800 s / 30 min). RADAR, CLOUDS, and RAIN reuse their cached sprites between realtime refreshes. Network tile fetches happen only when a layer cache is missing, map style or zoom changes, or the realtime refresh interval expires.

## File Map

| File | Purpose |
|:-----|:--------|
| `src/main.cpp` | All UI rendering, HTTP fetch, touch dispatch, weather/radar logic |
| `include/waveshare_display.h` | LovyanGFX device class — RGB bus and panel config |
| `include/display_hw.h` / `src/display_hw.cpp` | CH422G init, backlight on/off, touch reset sequence |
| `include/touch.h` / `src/touch.cpp` | GT911 I2C driver — poll-based, no interrupt |
| `include/config.h` | All user-tuneable constants and pin definitions (`cfg::` namespace) |
| `include/debug.h` | Leveled `DBG_ERROR` / `DBG_WARN` / `DBG_INFO` / `DBG_VERBOSE` macros |
| `include/secrets.h` | Gitignored — WiFi credentials + OWM API key |
| `include/secrets.example.h` | Template for `secrets.h` |

## User-Tuneable Settings (`include/config.h`)

All location/map settings are in the `cfg::` namespace:

```cpp
cfg::kLocationName          // display label
cfg::kLocationLatitude      // decimal degrees, south = negative
cfg::kLocationLongitude     // decimal degrees, east = positive
cfg::kMapZoom               // startup zoom 5–7
cfg::kDefaultMapStyle       // 0 = dark, 1 = topo, 2 = OSM
cfg::kBaseMapContrastPercent // 100 = unchanged
cfg::kBaseMapBrightness     // -255 to 255
cfg::kWifiApName            // captive portal SSID
cfg::kNtpTimezone           // POSIX tz string
cfg::kLayerCycleSecs        // UI layer auto-cycle interval
cfg::kRealtimeRefreshSecs   // weather/map API refresh interval
cfg::kRenderTaskStackBytes  // FreeRTOS render task stack size
```

## Debug Levels

Set via `-DDEBUG_LEVEL=N` in `platformio.ini` build flags (default 3):

| Level | Macro | Use |
|:------|:------|:----|
| 1 | `DBG_ERROR` | Critical failures |
| 2 | `DBG_WARN` | Unexpected but recoverable |
| 3 | `DBG_INFO` | State changes, render/cache events, per-tile fetch timing |
| 4 | `DBG_VERBOSE` | Raw malformed PNG dumps and very detailed diagnostics |

Level 4 also enables a raw hex/text dump of malformed PNG overlay responses.

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
