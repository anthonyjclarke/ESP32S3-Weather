# Changelog

## Todo
- Stop screen Glitching
- Implement WebUI with configuration and replica of TFT
- Indicator on TFT on date of last update on Layer
- Visual Indicator if downloading in background updated layers
- Simplify the logs when Fetching Data
- Evaluate base-map cache separation in `ENHANCEMENTS.md` to improve tile render
  speed.

## [1.3.1] 2026-04-29

### Fixed

- Touch unresponsive during weather data refresh. `getWeatherData()` made two
  blocking HTTPS calls (Open-Meteo + RainViewer) directly in `loop()` on
  Core 0, freezing the touchscreen for up to 40 s every
  `kRealtimeRefreshSecs` interval.
- All network I/O now runs on Core 1. `loop()` sets `weatherRefreshPending`
  instead of calling `getWeatherData()` directly. `renderTaskFn` checks the
  flag on wake, fetches weather data before rendering, then clears it.
  Core 0 stays fully responsive throughout.

### Added (OTA)

- ArduinoOTA support using the `esp32s3-7inch-ota` PlatformIO environment.

---

## [1.3.0] 2026-04-28

### Added

- Per-layer PSRAM render caches for RADAR, CLOUDS, and RAIN. Layer switching and
  auto-cycle now reuse cached full-screen sprites instead of refetching tiles
  every time.
- Configurable realtime refresh interval via `cfg::kRealtimeRefreshSecs`
  (default 1800 seconds / 30 minutes). Weather, RainViewer metadata, and map
  tile refreshes are now tied to this interval.
- Cache metadata for each layer: validity, zoom, map style, and last refresh
  time.

### Changed

- Render task now renders into a scratch sprite, copies the completed frame into
  the target layer cache, and signals the UI to push the cached layer.
- Layer auto-cycle no longer floods tile APIs. It renders only on cache miss,
  map/zoom change, or realtime refresh expiry.
- Progress timer now follows `cfg::kRealtimeRefreshSecs` instead of a fixed
  10-minute interval.

---

## [1.2.2] 2026-04-28

### Added

- Render/fetch diagnostics at default `DEBUG_LEVEL=3`: every tile now logs base
  fetch, overlay fetch, HTTP status/timing, payload size, decode timing, free
  heap, largest internal heap block, PSRAM, and render-task stack high-water.
- Loop-side render watchdog while the startup screen is waiting. If tile
  progress stalls for more than `cfg::kRenderStallWarnMs`, the serial monitor
  logs the active phase, tile index, tile coordinate, memory state, stack
  high-water, and redacted URL.
- Configurable render-task stack size, render-task priority, and stall logging
  intervals in `include/config.h`.

### Changed

- Render task stack increased to 32 KB via `cfg::kRenderTaskStackBytes`.
- Startup progress loop now calls `delay(10)` so the same-core render task gets
  scheduler time while the first map render is running.

---

## [1.2.1] 2026-04-28

### Fixed

- Tile fetch hang: replaced `http.begin(url)` with explicit `WiFiClientSecure` + `setInsecure()` + `setTimeout(15)` so TLS handshakes time out after 15 s instead of blocking indefinitely when heap is fragmented.
- Added `http.setConnectTimeout(15000)` for the TCP connect phase.
- Added per-tile heap guard in `renderRadarMap()` — tiles are skipped (with `DBG_WARN`) when free heap is below 80 KB to prevent attempting a TLS handshake that cannot succeed.
- Inter-tile `delay()` increased from 10 ms → 50 ms to allow FreeRTOS deferred heap cleanup between connections.
- Added `DBG_VERBOSE` heap log at the start of every HTTP fetch and at completion of each tile.

---

## [1.2.0] 2026-04-28

### Added

- Layer auto-cycle: rotates Radar → Clouds → Rain every `cfg::kLayerCycleSecs` seconds (default 30s). Touching the layer badge resets the timer and advances immediately. OWM layers silently skipped if key is absent or auth has failed.
- Double-buffer map sprite: two PSRAM sprites (`spriteA`/`spriteB`) — Core 1 renders the next frame into the back buffer while Core 0 continues running the UI. Eliminates display freeze during tile fetching.
- FreeRTOS render task (`renderTaskFn`) pinned to Core 1. All HTTP fetches and PNG decodes happen off the main loop.
- Startup screen displayed while the first render loads: firmware version, location, WiFi SSID + IP, NTP timezone, PSRAM/heap, OWM key status, cycle interval.
- Serial startup banner logged at end of `setup()`.
- Startup loading progress bar: cyan fill bar (640×18 px at y=420) and tile counter text drawn on the startup screen while the first render runs on Core 1. Updates on every tile completion without redrawing unchanged frames.
- Serial log at render start: `Map render start | layer=X zoom=N | N tiles` printed before any HTTP fetches begin.
- Startup screen transitions from "Loading map..." to "Fetching map tiles..." after `getWeatherData()` returns, so the user can distinguish the weather fetch phase from the tile fetch phase.

### Changed

- `renderRadarMap()` is now purely a sprite-write function — no direct `lcd.` calls, no `pushSprite`. Push and UI overlay happen in `loop()` when `RENDER_READY` is signalled.
- `drawMapBadges()` extracted from duplicated inline code across touch handlers and `renderRadarMap`.
- Touch handlers for layer/map/zoom no longer block with `delay()` — they update badges and call `triggerRender()` for a non-blocking background render.
- Weather fetch single-line summary now includes elapsed time.
- `getWeatherData()` 10-minute timer now calls `triggerRender()` instead of the blocking `renderRadarMap()` + UI redraw.

### Fixed

- Render task stack increased from 8192 → 16384 bytes. The 8192-byte stack was too small for concurrent HTTPClient + PNGdec on Core 1, causing the task to stall silently on first load.

---

## [1.1.0] 2026-04-28

### Changed

- Replaced all raw `Serial.printf()`/`Serial.println()` application diagnostics with leveled `DBG_*` macros throughout `main.cpp`.
- Added `DBG_VERBOSE` (level 4) to `include/debug.h` — hex/text PNG dump now guarded by `#if DEBUG_LEVEL >= 4`.
- Converted `#define FW_VERSION` → `constexpr const char* kFwVersion` in `config.h`.
- Moved `WIFI_AP_NAME` and `NTP_TIMEZONE` from `#define` to `cfg::kWifiApName` and `cfg::kNtpTimezone` (`constexpr const char*`) in the `cfg` namespace.
- Renamed `changelog.md` → `CHANGELOG.md`.

### Added

- `DBG_INFO` heap log at end of `setup()` (always on at default `DEBUG_LEVEL=3`).

---

## [1.0.0] 2026-04-28

Baseline release of this PlatformIO adaptation, based on Mirko Pavleski's
(`mircemk`) original Hackster weather dashboard code.

### Based On

- Original project: [ESP32 Weather Dashboard with Satellite Maps and 16-day Forec](https://www.hackster.io/mircemk/esp32-weather-dashboard-with-satellite-maps-and-16-day-forec-7ee044)
- Original author: Mirko Pavleski (`mircemk`)
- Original publication date: April 11, 2026

### Included In This Baseline

- Waveshare ESP32-S3-Touch-LCD-7 PlatformIO project structure.
- Configurable location settings in `include/config.h`.
- Open-Meteo forecast integration for current and 16-day forecast data.
- RainViewer radar overlay support.
- Optional OpenWeatherMap cloud/rain overlays via `SECRET_OWM_API_KEY`.
- GT911 touch handling and CH422G display reset/backlight support.
- PSRAM-backed 800x415 map canvas.
- Centered map viewport and red location marker.
- Base map visibility tuning for LCD contrast/brightness.
- Serial diagnostics for forecast parsing, tile fetching, and OpenWeatherMap
  authorization failures.
