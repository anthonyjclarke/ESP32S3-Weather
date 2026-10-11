# Phase 2 — weather application review

Reviewed 12 October 2026; fixes prepared as **1.5.7**, following 1.5.6.
The user is testing Phase 1 hardware while Phase 2 is built separately.

## Scope and assessment

Reviewed maintained `src/main.cpp`, `src/display_hw.cpp`, `src/touch.cpp`, all
public headers/configuration templates, PlatformIO environments, partition
layout, README, CHANGELOG, CLAUDE, ENHANCEMENTS and functional/backlight docs.
The original reference C file is historical material, not compiled firmware.
Secrets were used only for the existing local build, not reproduced here.
The schematic was not electrically revalidated; PWM brightness/flicker and
power-cycle behavior require hardware observations.

The application has a useful retained-layer design and a working recovery pin
for its RGB driver. The largest maintenance risks were ownership of shared
render/forecast state, misleading freshness after failures, and unchecked
provider input. Build success alone previously missed the LCD regression.
Avoid changing framework, panel timing or driver pin alongside these fixes.

## Findings addressed in 1.5.7

| Priority | Finding and consequence | Change / relevant code |
|---|---|---|
| P1 | PNG callbacks use 256-pixel stack buffers without checking downloaded dimensions; oversized PNGs could overwrite memory. | Validate 256x256 before every decode; cap tile responses at 100 KB. `renderRadarMap()`, `validTileDimensions()`. |
| P1 | Missing NTP left `tm` values uninitialized before weekday indexing and graph dates. | Initialize timestamps, use nonblocking checks and display unavailable states. `drawBottomDashboard()`, `drawGraphPage()`. |
| P2 | Worker writes forecasts while main draws arrays, permitting mixed snapshots. Null values can silently become zero. | Parse into `ForecastData`, require finite numeric fields, publish through a length-one FreeRTOS queue; only main updates displayed arrays. `getWeatherData()`, `applyWeatherUpdate()`. |
| P2 | Attempt time was also treated as successful-data time; failed JSON could discard radar metadata. | Preserve last good forecast and metadata; expose successful age, attempt age, availability and retry state. Failed weather/metadata fetches retry after 300 s while awake. |
| P2 | Graph-page guard prevented hourly updates indefinitely. | Schedule weather on all awake pages. Main redraws an open graph after successful forecast publication. Sleep still pauses new scheduled refreshes. |
| P2 | Worker copied into displayed caches, and a new READY request could overwrite scratch before consumption. Live opacity could change during PNG decoding. | Atomic state handoff; main owns cache publication, READY queues new requests, opacity is captured per render. Cache-hit READY is distinguished from worker completion. |
| P2 | Tile errors still marked incomplete maps fresh for an hour. Completed renders also replaced graphs. | Preserve a complete matching map on failure; mark degraded and throttle automatic retries to five minutes. A first incomplete view can show partial coverage. Only the awake map page receives map pushes. |
| P2 | TLS waits were implicit and slow streams could extend tile reads indefinitely. | Explicit connect/read/handshake timeouts; 30 s tile body deadline. This is a per-request bound, not a whole-render deadline. |
| P2 | RainViewer radar requests followed the application's zoom range through 12. | Provider-specific maximum 7; higher views display the basemap and an unsupported badge. |
| P2 | CH422G touch reset could interleave with the running backlight task's Wire transactions. | Hold the existing Wire mutex for the reset writes/pulse. No panel parameters or task pinning changed. |
| P2 | Arduino integer `map()` truncated fractional graph ranges; flat temperature ranges could divide by zero. | Floating-point `graphCoordinate()` with a zero-range guard; normalize calendar-day offsets using `mktime()`. |
| P2 | WebUI hardware metrics inserted raw SSID text into HTML; malformed sleep time text was accepted as zero. | Escape metric labels/values and require exact numeric HH:MM. |
| P2 | Sprite/queue allocation failures could start an unusable worker. | Check allocations and stop worker startup with a diagnostic. |
| P3 | Docs asserted UI Core 0, all HTTP Core 1 and no main-thread network waits. | Document task ownership: Arduino main and render worker both Core 1 in current defaults, PWM Core 0; startup fetch and WebUI streaming are synchronous. Historical dimming/reference notes corrected. |

RainViewer documents maximum zoom **7** and identifies frame timestamps as
frame generation time rather than observation time. The new radar metadata age
is retrieval age; it must not be interpreted as rainfall observation age.
[RainViewer Weather Maps API](https://www.rainviewer.com/api/weather-maps-api.html).

FreeRTOS queues and explicit publication are used to transfer ownership; merely
placing work on another task does not synchronize shared state.
[ESP-IDF FreeRTOS documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/freertos_idf.html).

## Deferred findings and recommendations

| Priority | Remaining issue | Recommended phase / acceptance |
|---|---|---|
| P2 | Forecast arrays are labeled relative to current local date rather than retained API timestamps. A failed refresh across midnight or location/timezone mismatch can show yesterday's values as today. | Follow-up reliability patch: store daily/hourly timestamps and select labels/indices from them; test midnight, DST and failed refresh. Current age display helps but does not correct date labels. |
| P2 | HTTPS clients use `setInsecure()`. Weather JSON still uses `getString()` with read timeouts but no hard size/total-body deadline. | Follow-up network patch: trusted roots or certificate bundle, bounded JSON streaming/allocation and end-to-end deadlines. Verify NTP/certificate bootstrap and provider redirects on device. |
| P2 | `/screen.bmp` streams about 1.15 MB synchronously, delaying touch/OTA; periodic browser polls can overlap on slow links. | Phase 6: one in-flight poll, backoff, and chunked/cooperative capture. Measure touch latency during screen polling and OTA. |
| P2 | PWM mutex/task creation is unchecked, and diagnostic strings/counters are shared without a complete snapshot protocol. | Follow-up hardware/task patch: fail-safe backlight on allocation failure, diagnostics snapshot synchronization; test allocation failure before changing startup ordering. |
| P2 | Public installer needs per-device location, timezone and OWM configuration; private build secrets must not enter published images. | Phase 3: optional secret include with blank defaults, runtime configuration and clean CI build. Use a separate release build, not this local binary. |
| P2 | WebUI is unauthenticated HTTP with reboot/reset/settings endpoints. | Before public distribution: document trusted-LAN deployment and evaluate authentication and request-origin protections. No Internet exposure assumed or introduced. |
| P2 | Third-party basemap attribution and allowed traffic must be checked before distributing firmware to many devices. | Phase 3/4: audit current provider terms, display attribution, choose an appropriate production tile provider. Hourly forecasts do not imply only 24 tile requests/day. |
| P3 | OTA/WebUI initialization depends on successful startup WiFi; later reconnection does not initialize services missed at boot. Captive portal can wait indefinitely. | Follow-up connectivity patch with explicit startup/reconnect states and portal timeout policy. Test absent AP, provisioning and AP return. |
| P3 | Touch state can persist after failed point reads; held touches can repeat, and only the configured GT911 address is probed. | Hardware-backed touch patch: clear failed states, validate point count, define press/repeat behavior. Test cold boot and held buttons. |
| P3 | Weather, radar metadata and all composed layer caches share the hourly interval. Basemaps re-download with forced overlay renders. | Phase 6: independent policies/base-map reuse with provider-aware cache keys and bounded memory. Keep one-hour baseline until measured traffic supports a change. |
| P3 | Configurable coordinates lack geographic validation; map label includes hardcoded OHRID; API routes and rendering remain in a large main file. | Runtime-config phase: validate Mercator-safe latitude/longitude, use configured location label, then extract modules incrementally. |
| P3 | Other library version ranges can float even though framework and LCD fix are pinned. | Record resolved versions in release artifacts; pin/lock tested dependencies and require hardware smoke tests for upgrades. |

Open-Meteo returns daily/hourly time axes and timezone metadata. A future date
fix should use those values rather than reconstructing dates from current NTP.
[Open-Meteo API documentation](https://open-meteo.com/en/docs).

## Installer preparation for the next phase

Follow `anthonyjclarke.github.io`'s existing shared installer process rather
than creating a parallel release system. The earlier hub review found the
manifest generator's chip family fixed to ESP32 and hub scope limited to CYD;
these need explicit ESP32-S3 support while preserving existing CYD manifests.
[Installer repository](https://github.com/anthonyjclarke/anthonyjclarke.github.io).

1. Add stable project/version identity compatible with the shared workflow.
2. Build without personal `secrets.h`; persist device configuration in NVS.
3. Integrate the hub's Improv provisioning approach and verify it on S3.
4. Derive flash offsets from this build's partition/upload metadata, emit all
   required bootloader/partition/boot_app0/application artifacts and checksums.
   Never copy ESP32 offsets or rely on an application-only binary for first install.
5. Verify OTA slot selection, settings retention and wrong-chip rejection on
   hardware before publishing; hub/release workflow changes belong to Phase 4.

## Validation and its limits

- Isolated `pio run -d /private/tmp/esp32s3-weather-phase2 -e esp32s3-7inch`:
  PASS, using the same verified local dependency versions and pinned LCD fix.
  Arduino 3.3.7, LovyanGFX 1.2.21+sha.d504ab7, ArduinoJson 6.21.6,
  PNGdec 1.1.6, WiFiManager 2.0.17.
- Native `tests/weather_validation_test.cpp`: valid data, null/string/nonfinite
  rejection, short arrays, zero values, tile bounds, fractional/flat graph scale,
  refresh boundary and millis wraparound PASS.
- Extracted WebUI JavaScript: `node --check` PASS. HTML metric escaping checked
  with an injected-tag input. These checks do not exercise browser integration.
- Native parsing tests cover candidate validation; queue/cache ownership and
  end-to-end network failure behavior are reviewed code, not hardware-tested.
- Original project build/upload artifacts were left intact while Phase 1 was
  being uploaded. No flash, erase, commit, push, tag or publication performed.

Hardware acceptance for 1.5.7 remains pending:

1. Confirm visible display, working touch, version 1.5.7 and hourly interval.
2. Stay on a graph across a due refresh; graph must stay visible and update.
3. Disconnect/reconnect networking around refresh; data age must keep increasing,
   retry must be reported, and a successful response must clear failure state.
4. Change opacity/zoom during a render; verify caches match the latest view.
5. Induce tile failures with a complete matching cache and then without one;
   verify retained/degraded versus partial behavior and five-minute cooldown.
6. Boot without NTP/forecast, exercise every graph, midnight/DST and night wake.
7. Compare touch responsiveness during WebUI mirror polling and run overnight.

Do not label Phase 2 runtime acceptance complete until these observations are
recorded. The deferred P2 items should be addressed before a public installer
release, with small separately tested patches.

## Phase 3 follow-up — 12 October 2026

The public-build/runtime configuration prerequisite is now implemented in
1.5.8: optional personal secrets, explicit public-secret exclusion, Device setup
in NVS, and shared version reporting. Coordinates are validated for runtime
configuration and the hardcoded OHRID label is removed. Installer preparation
and pending USB/update hardware checks are recorded in
[WEB_INSTALLER.md](WEB_INSTALLER.md). Other deferred findings remain open.
