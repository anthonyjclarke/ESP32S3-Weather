# Changelog

## [1.5.9] 12-10-2026

### Changed

- Rebrand the local WebUI using the Health App paper/green palette, system
  typography, white cards and form styling. Dashboard, Settings and System
  navigation groups the mirror/controls, device/night settings and diagnostics.
- Keep the running firmware version visible across all sections, preserve data
  age, cache states, mirror touch and all existing control/API behavior. Add
  mobile layouts, keyboard focus indicators and a scrollable cache table.
- Web installer integration remains paused at the user's request.

### Validation

- Browser preview and isolated firmware build results are recorded in the phased
  plan. Device display, settings persistence and LAN controls still need board
  acceptance after upload. No display driver or refresh timing changes.

## [1.5.8] 12-10-2026

### Added

- Phase 3 public environment excludes personal secrets even when a local header
  exists; ordinary builds accept an absent secrets file. Public defaults use
  Sydney and blank keys/passwords.
- WebUI Device setup stores location, coordinates, POSIX timezone, OWM key and
  OTA password in NVS, validates a candidate, then restarts to apply it. Password
  fields retain existing values when blank; explicit actions clear them. API
  reads return presence flags only. OTA is disabled without a configured password.
- Always-on Improv provisioning over native USB Serial/JTAG using the shared
  installer's patched library, ESP32-S3 identity and a Core 0 listener task.
  Add malformed-frame bounds checks and restore credentials after failed joins.
- Public post-build artifacts, identity/hash guard, four-part ESP32-S3 manifest
  packaging, partition/NVS checks, checksums and web-install runbook.

### Changed

- A literal `FIRMWARE_VERSION` now feeds startup, serial, WebUI, Improv and
  installer metadata. WebUI shows **Running firmware v1.5.8** in its initial
  HTML before polling. Runtime location settings drive maps and forecast requests.
- Keep WebUI mDNS discovery available when OTA is disabled.
- Replace the hardcoded OHRID map label with LOCATION. Preserve panel timing,
  partition layout, the pinned LCD interrupt fix and hourly refresh policy.
- Installer preparation is local only; USB onboarding, update/settings retention
  and app1 recovery remain pending hardware tests. No firmware published.

## [1.5.7] 12-10-2026

### Fixed

- Phase 2: validate a complete forecast snapshot before publishing it through a
  FreeRTOS queue. Failed or malformed forecast/radar replies retain last good
  data; successful forecast age is separate from attempt age in `/api/status`
  and WebUI. Retry failed weather requests after five minutes while awake.
- Hourly weather refreshes continue on graph pages; completed map renders do
  not replace a graph or a sleeping display. Main publishes layer caches after
  atomic render completion, preserving complete matching maps on tile failure.
  Capture opacity per render and discard results for changed views.
- Reject oversized PNG dimensions before decoding into 256-pixel callback
  buffers; cap tile payloads and body-read duration, and set explicit TLS waits.
- Handle missing NTP/forecast values safely, normalize calendar-day offsets,
  and use floating-point graph scaling with a flat-range guard.
- Serialize touch reset with backlight PWM I2C transactions; check sprite/queue
  allocations before starting the worker.
- Escape WebUI hardware metric text, strictly validate sleep times, and respect
  RainViewer's documented zoom-7 limit. Higher zooms retain the base map.

### Documentation

- Full maintained-source review in `docs/PHASE_2_REVIEW.md`, including deferred
  findings, installer prerequisites and hardware acceptance checks. Correct
  task/core ownership and distinguish historical reference behavior.
- Preserve the pinned LovyanGFX LCD interrupt fix and existing panel timing.
  Build/native/JavaScript checks do not establish hardware compatibility.

## [1.5.6] 12-10-2026

### Changed

- Phase 1: increase `kRealtimeRefreshSecs` from 1800 to 3600 seconds. Scheduled
  weather/RainViewer fetches and active-layer refreshes now run hourly while
  the map page is active. Layer-cache freshness also becomes one hour; radar
  images may consequently be older. Startup fetching, 30-second layer cycling,
  forced renders, panel timing, and the pinned LCD interrupt fix are unchanged.
- Document cache misses and forced-render traffic separately from scheduled
  refreshes, and note that forecast graph pages defer periodic refreshes.

### Added

- README display example from `images/TFT-Display.jpg` and a WebUI screenshot
  placeholder describing the browser controls and running-version display.

## [1.5.5] 12-10-2026

### Added

- WebUI shows the running firmware version beside the TFT mirror metadata.
  `/api/status` exposes `firmwareVersion` from `kFwVersion`, the same source
  used by the startup screen and serial banner.

### Fixed

- Firmware identity now reports `1.5.5`. It previously stayed at `1.5.2`
  even though the changelog recorded the subsequent 1.5.3 and 1.5.4 fixes.
- Blank RGB display after rebuilding with LovyanGFX 1.2.21 on Arduino 3.3.7 /
  ESP-IDF 5.5. Boot logged `No free interrupt inputs for LCD_CAM interrupt
  (flags 0x80E)` while touch, WiFi, weather fetches, and map rendering continued.
  The previous `LovyanGFX@^1.1.16` constraint permitted the affected version;
  a successful compilation did not verify display operation.
- Pin LovyanGFX to upstream commit
  `d504ab71264b8197ede527746fa1a21b5d20e075`, restoring `ESP_INTR_FLAG_SHARED`
  for the ESP32-S3 RGB LCD_CAM interrupt. The user confirmed display recovery
  after flashing the corrected build on 12 October 2026. Panel timings,
  pin assignments, and weather refresh interval were unchanged.
  Keep the exact dependency pin when rebuilding; the plan document alone
  cannot change the running firmware. Upstream fix:
  https://github.com/lovyan03/LovyanGFX/commit/d504ab71264b8197ede527746fa1a21b5d20e075

## Todo
- Refresh the WebUI format using the Health App project UI as the design template;
  see `ENHANCEMENTS.md` and Phase 6 in `CODE_REVIEW_AND_PHASED_PLAN.md`.
- Add TZ selection in WebUI
- Tidy up Social status line in WebUI
- Evaluate free-to-use API alternatives based on current usage in the README
  `## API` section. Keep Open-Meteo as the forecast baseline if
  non-commercial limits fit the project; compare OpenFreeMap or self-hosted
  OSM/OpenMapTiles for base maps instead of relying on `tile.openstreetmap.org`;
  verify RainViewer free-user radar limits before raising zoom or request rate;
  find a no-key or free-tier replacement for OpenWeatherMap CLOUDS/RAIN overlays
  and document provider terms, attribution, rate limits, max zoom, tile format,
  ESP32 TLS/PNG compatibility, and cache strategy before implementation.
- Evaluate base-map cache separation in `ENHANCEMENTS.md` to improve tile render speed
- Based on Zoom if data not available then skip display with a status message to advise
- Evaluate https://github.com/rainviewer/rainviewer-api-example for Zoom details


## [1.5.4] 09-10-2026

### Fixed

- `initWiFi()` never detected saved WiFiManager credentials: it tested
  `WiFi.SSID()`, which is only the network currently joined and is always
  empty before `autoConnect()`. It now starts the STA driver with
  `WiFi.mode(WIFI_STA)` and asks `wm.getWiFiIsSaved()`, which reads the stored
  STA config. Credentials saved through the portal are now used instead of
  being overwritten by the `secrets.h` pre-seed on every boot, and a board
  with saved credentials but no `secrets.h` SSID now closes the portal after
  15 s instead of waiting forever. Port of CYD_PongClock commit `2862967`.

## [1.5.3] 09-05-2026

### Fixed

- Clock showing wrong time (epoch/00:00) on startup — `setup()` now waits up
  to 10 s for the first NTP sync to complete before proceeding. "Syncing
  time..." shown on the startup splash during the wait.
- `drawBottomDashboard()` and `drawTopDate()` now guard `getLocalTime()` return
  value; clock shows `--:--` and the date badge is hidden until NTP syncs,
  instead of displaying junk from an uninitialised `tm` struct.
- `getLocalTime()` in `loop()` changed to zero-timeout so it no longer stalls
  the loop for up to 5 s when NTP hasn't synced yet.
- WiFi reconnection after a drop no longer leaves the clock drifting — a
  `ARDUINO_EVENT_WIFI_STA_GOT_IP` event handler now re-calls `configTzTime()`
  whenever the device regains an IP address.
- `initWiFi()` now explicitly enables `WiFi.setAutoReconnect(true)` on
  successful connection.

---

## [1.5.2] 04-05-2026

### Added

- Overlay opacity settings (Radar, Clouds, Rain) now persisted to NVS under
  the `"overlays"` Preferences namespace, keys `radar`, `clouds`, `rain`.
  Values are loaded at boot and saved immediately on any WebUI change.
  Default changed to 50% for all three layers (was 55/25/25).

### Changed

- WebUI Night Schedule: **Sleep dim** control renamed to **Display Brightness**.
- Removed the WebUI Brightness slider and all associated code. The CH422G
  backlight is digital on/off at full brightness; the slider had no effect
  beyond toggling on/off and was misleading. Brightness during the sleep window
  is still controlled by the Display Brightness slider in Night Schedule.
- Removed left-edge touch brightness zone on the physical display (drag-to-dim
  gesture), and the yellow brightness-level marker from the progress-timer bar,
  for the same reason.

### Fixed

- Startup TFT version string now matches `kFwVersion` in `config.h`. Was
  displaying `1.4.0` (stale value) despite subsequent version bumps.

## [1.5.1] 2026-05-03

### Added

- TFT map-style badge now includes the active zoom level, e.g. `TOPO Zoom 7`,
  for every base map style.
- WebUI Night Schedule card: **Sleep dim** slider for `kSleepDimBrightness`.
  The value is persisted in NVS and applies immediately while sleep mode is dark.
- WebUI Night Schedule card: **Force sleep now** toggle. Activates sleep mode immediately regardless of the schedule window — useful for testing PWM dimming and for manual override outside normal sleep hours. Cleared automatically on toggle-off; not persisted to NVS.

### Fixed

- `display_hw.cpp`: full-on PWM could skip the initial CH422G pin-high write
  because the PWM task started with `lastB = 255`; steady 0/255 states now
  force their first write.
- `setBacklightBrightness()` stays steady on/off when `CFG_BACKLIGHT_PWM_ENABLED`
  is active. Software PWM is reserved for `setBacklightDim()` so the awake TFT
  backlight does not flicker.
- `display_hw.cpp`: `std::max()` type-deduction error (`unsigned int` vs `uint32_t`) on toolchain GCC 14 — both arguments now explicitly cast to `uint32_t`.
- `setup()`: init-order crash when `CFG_BACKLIGHT_PWM_ENABLED` is active — `touch_init()` was called before `startBacklightPwm()`, so `gWireMutex` was `nullptr` when `WIRE_TAKE()` fired inside `gt911ReadBytes()`. `startBacklightPwm()` (mutex creation + task start) now runs before `touch_init()`.
- `renderTaskFn`: stale render displayed when zoom or map style changed mid-render — the in-flight render at the old settings would re-validate the layer cache (overwriting the `invalidateLayerCaches()` that zoom-change triggers) and update `mapFront` to the wrong-zoom sprite. Render result is now discarded if `renderZoom != myZoom || renderMapStyle != mapStyle` at completion; Core 0 re-triggers via `renderPending`.

## [1.5.0] 2026-05-03

### Added

- Software PWM backlight dimming via CH422G I2C, controlled by feature flag
  `CFG_BACKLIGHT_PWM_ENABLED` in `config.h`. Disabled by default — the build
  retains original digital on/off behaviour until the flag is uncommented.
- When enabled, a FreeRTOS task (`bl_pwm`, Core 0, priority 2) drives the
  backlight at 100 Hz with proportional duty cycle. At 0 and 255 the pin is
  held steady with no toggling.
- `setBacklightDim(uint8_t)` — new function for proportional brightness
  (0=off, 255=full, 1–254 = ~10% duty steps). Distinct from
  `setBacklightBrightness()` which retains on/off semantics.
- `startBacklightPwm()` — call once from `setup()` after `touch_init()`. Wires
  up the mutex and spawns the task.
- `gWireMutex` — `SemaphoreHandle_t` protecting the shared I2C bus between the
  PWM task (Core 0) and GT911 touch polling (Core 1). Active only when flag is
  set; zero overhead otherwise.
- `kSleepDimBrightness` (default 30/255 ≈ 12%) — configurable sleep dim level.
- `kSleepDimTestMode` (default false) — when true, immediately dims to
  `kSleepDimBrightness` after boot so the level can be evaluated without waiting
  for the sleep window.
- Sleep state machine updated: when `CFG_BACKLIGHT_PWM_ENABLED` is set, the
  `SLEEP_PENDING → SLEEP_DARK` transition calls `setBacklightDim()` instead of
  turning off. All wake paths continue to use `setBacklightBrightness()` and
  restore full brightness unchanged.

## [1.4.0] 2026-05-02

### Added

- Night sleep schedule feature. When enabled, the display turns off at a
  configurable time each night and wakes on touch. A 2-second "In Sleep mode,
  touch to wake up" message is shown before the backlight turns off.
- Fixed HH:MM sleep-start and wake times, configurable from the WebUI.
  Window may cross midnight (e.g. 22:00–07:00).
- Touch-to-wake during sleep window: a physical touch or a WebUI click on the
  TFT mirror restores the full dashboard for `kSleepWakeDurationSecs`
  (default 5 min), then the display re-sleeps if still inside the window.
- Sleep schedule controls added to WebUI Night Schedule card: enable/disable
  toggle, sleep-at / wake-at time inputs, and a live status line showing
  current sleep state, window times, and remaining wake time.
- Sleep settings persisted in NVS via `Preferences` ("sleepsch" namespace).
  Config defaults documented in `config.h` (`kSleepScheduleEnabled`,
  `kSleepOnHour/Minute`, `kSleepOffHour/Minute`, `kSleepWakeDurationSecs`).
- `buildStatusJson()` extended with a `sleep` object exposing all schedule
  state for WebUI polling (state, in-window flag, wake countdown).
- Background renders that complete during sleep no longer update the LCD
  (cache update still happens; display restores correctly on wake).

## [1.3.2] 2026-04-29

### Added

- Local LAN WebUI with a BMP TFT mirror, layer cache status table, render
  progress/status JSON, immediate zoom and brightness controls, hardware
  environment status, reboot and WiFi reset actions, footer credits/links, and
  click-to-touch coordinate forwarding.
- Per-layer RADAR, CLOUDS, and RAIN opacity controls in the WebUI. Overlay alpha
  is applied over every base map style, and changing a value invalidates the
  affected rendered layer cache so the next frame reflects the new blend.
- Zoom range extended from 5–7 to 5–12. `kMapZoomMin` and `kMapZoomMax`
  constants added to `config.h` so the range is configurable without touching
  code. Radar and OWM overlay tiles are available up to zoom 12; base map works
  beyond that if `kMapZoomMax` is raised.
- Zoom level confirmation message (`ZOOM: N`) restored on the display when the
  map area is tapped. Shown immediately and cleared by the render flip when the
  new map is ready — no blocking delay.

---

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
