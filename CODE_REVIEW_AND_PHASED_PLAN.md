# Code review and phased improvement plan

Review date: 12 October 2026 (Australia/Sydney).

> Target confirmed: **ESP32S3-Weather**. Phase 0 completed on 12 October 2026.
> The original review below was copied from the display scaffold; its demo
> findings apply to that scaffold. See the Phase 0 record at the end for the
> confirmed weather project baseline and next action.

## Scope and project mismatch

The active checkout is `ESP32-S3-Touch-LCD-7`, on `main` at `67bf028`.
All maintained firmware sources, headers, build settings, partition table,
README, CHANGELOG, and current CLAUDE.md were reviewed. The vendor reference
directory contains only Finder metadata, not reference source. The local `dev`
branch also contains the display/touch scaffold.

**This project has no weather service, Wi-Fi provisioning, HTTP client, or
weather refresh timer.** Its display changes scenes every eight seconds and
polls touch every 50 ms. Neither setting controls weather downloads.

A nearby project, `../ESP32S3-Weather`, contains the matching weather dashboard
(version 1.5.2). Its configuration, README, architecture notes, weather fetch,
and refresh scheduling were inspected to identify a likely next step. This was
a targeted inspection, not a full review of its 3,491 source lines. Confirm the
intended project before applying changes there.

Existing edits to CLAUDE.md and the untracked CLAUDE.md.bak were preserved.
No firmware changes, upload, commit, push, or publication were performed.

## Findings in the active checkout

### 1. P2 — Failed expander initialization bypasses the advertised I2C fallback

Evidence: `src/main.cpp:67–77`, `src/main.cpp:111–134`.

`initIoExpander()` assigns the global pointer before calling `init()` and
`begin()`. Either can fail while leaving a non-null pointer. `initTouch()`
uses `if (!io_expander)` to decide whether to initialize the bus itself, so
that fallback is never reached after those failures. `resetGt911()` also
calls the expander based on pointer existence rather than readiness.

The installed expander implementation confirms that `init()` installs the
legacy I2C driver; allocation alone does not establish bus readiness.

Recommendation: track bus readiness and expander readiness separately. Check
the bus before probing touch, and only pulse expander pins after successful
initialization. A failed expander can still prevent the physical touch reset,
so report degraded operation rather than promising recovery.

### 2. P2 — Critical reset/backlight writes are ignored, allowing false success

Evidence: `src/main.cpp:79–90`, `src/main.cpp:98–108`.

Failure to enable outputs produces only a warning; LCD reset, backlight,
and touch reset writes have unchecked return values. Boot then reports the
expander ready even if the panel stayed in reset or the backlight stayed off.

Recommendation: propagate failure of required output configuration and pin
writes. Log the failed stage and avoid reporting successful hardware startup.
Validate with a disconnected/unresponsive expander as well as a normal boot.

### 3. P2 — LCD initialization relies on unchecked results and an assert side effect

Evidence: `src/lcd_port.cpp:527–547`.

`init()` and `reset()` results are ignored, and `begin()` is executed inside
`assert(s_lcd->begin())`. With assertions disabled, that entire call is removed.
The current build succeeds; this is a failure-path problem and a future build
configuration hazard, not evidence that the current firmware fails to boot.

Recommendation: execute each initialization call explicitly and check its
result. Return an LCD readiness result to setup and leave rendering disabled
after failure. Keep assertions for invariants without operational side effects.

### 4. P2 — Documentation no longer describes the firmware users build

Evidence: `README.md:15–18`, `README.md:51–60`, `CHANGELOG.md`, CLAUDE.md introduction;
compare `src/lcd_port.cpp:361–573`.

Documentation still describes a built-in color-bar test and a log message that
the code no longer emits. Firmware instead cycles four rendered scenes:
gradients, shapes, throughput pattern, and fonts. Each scene is drawn once
on entry; despite tick-based drawing helpers, it is not continuously animated.
The April graphics commit has no corresponding changelog entry or version bump.

Recommendation: describe actual behavior and expected logs, explain panel FPS
versus scene redraws, record the graphics addition, and adopt a consistent
version policy. Document cold boot, serial upload, and touch checks.

### 5. P3 — Refresh diagnostics and configuration need clearer ownership

Evidence: `include/lcd_port.h:64–75`, `src/lcd_port.cpp:160–179`,
`include/config.h:5–16`, `src/main.cpp:16–22`.

`EXAMPLE_LCD_PRINT_FPS_PERIOD_MS` and the draw-finish feature switch are unused.
FPS logs every 50 panel refreshes, independently of DEBUG_LEVEL, from the
interrupt callback. I2C pins and expander assignments are duplicated; scene
timing lives in the implementation despite documentation claiming all tunable
constants live in config.h.

Recommendation: collect application settings in one place, remove unused
switches or implement them, and have the callback count frames while the main
task prints on a configured interval. The current use of IRAM/DRAM and ROM
printing is deliberate; callback logging duration remains a hardware concern,
not a confirmed crash.

### 6. P3 — Tight idle loop and slow touch failure path deserve attention

Evidence: `src/main.cpp:168–207`, `src/main.cpp:233–250`.

The loop has no explicit delay between scheduled work. Normal touch polls are
small, but a five-point read with I2C timeouts can block for hundreds of
milliseconds and repeatedly emit warnings. There is no runtime recovery or
rate-limiting after persistent touch failures.

Recommendation: add a small task delay, bound/rate-limit failure diagnostics,
and consider reading all touch records in one transaction. Add recovery only
if testing shows it is necessary. Preserve the existing wrap-safe unsigned
interval comparisons and ready-flag clearing for zero/invalid point counts.

## Build and hardware assessment

`pio run` with the configured environment passed on 12 October 2026:

- Application flash: 382,241 bytes of the 5 MB slot (7.3%).
- Static RAM reported by PlatformIO: 23,100 bytes (7.0%).
- Third-party IO expander C sources emitted const-qualification warnings.
- Dynamic PSRAM framebuffer consumption is not included in those percentages.

The partition entries are aligned, do not overlap, and end at 16 MB. Two
5 MB OTA slots already exist, but there is no OTA implementation in this demo.
The bounce buffer is configured before LCD initialization, as required by the
documented display workaround. Dependency versions and platform are pinned.

No hardware was flashed or observed. Touch reliability, panel stability,
tearing during direct framebuffer writes, and failure recovery remain
unverified. Avoid changing panel timing or migrating display libraries as part
of a refresh-rate adjustment.

## Weather refresh: likely intended project

`../ESP32S3-Weather/include/config.h` currently defines:

```cpp
constexpr int kLayerCycleSecs = 30;
constexpr int kRealtimeRefreshSecs = 1800;
```

Thirty-second layer switching reuses fresh cached images; it does not imply
weather downloads every thirty seconds. The 1,800-second value controls
Open-Meteo/RainViewer refresh scheduling and layer-cache freshness. Cache
misses, zoom/style changes, and forced renders can cause additional tile traffic.

The minimum proposed change, after confirming this is the intended project,
is **1800 → 3600 seconds**: hourly routine refreshes rather than half-hourly.
This halves scheduled refresh cycles (48 → 24 per day while continuously on
the map page), not necessarily total requests. It also makes map overlays
eligible for reuse for an hour, which may be too old for useful radar viewing.

The scheduler at `src/main.cpp:3214` only refreshes while `appState == 0`.
Forecast pages can therefore remain stale while selected. It records the
scheduled attempt time before the worker fetch succeeds. Later work should
separate attempt time from successful data age and schedule independently of
the currently displayed page.

For a better long-term design, give forecast data, radar metadata, overlay
tiles, and base tiles separate freshness policies. Set those policies against
provider update frequency and current usage rules before choosing defaults.
Keep cached data visible on failure, show its age, and use bounded retries.
This policy split is a recommendation, not an implemented or tested change.

## Web installer integration with the existing process

Reviewed remote hub and shared tooling on 12 October 2026:

- [Hub guide](https://github.com/anthonyjclarke/anthonyjclarke.github.io/blob/main/README.md)
  and [hub page](https://github.com/anthonyjclarke/anthonyjclarke.github.io/blob/main/index.html).
- [Manifest generator](https://github.com/anthonyjclarke/cyd-web-installer/blob/main/tools/make_manifests.py),
  [post-build script](https://github.com/anthonyjclarke/cyd-web-installer/blob/main/tools/merge_bin.py),
  and [release workflow](https://github.com/anthonyjclarke/cyd-web-installer/blob/main/.github/workflows/cyd-release.yml).

The hub reads each project site's `index.json`; firmware remains in the
project's own Pages deployment. The display project has no Git remote configured,
installer tooling, CI workflow, or PROJECT_NAME identity setting.

The shared generator currently emits `chipFamily: "ESP32"` for every build.
It must derive the family per environment and emit **ESP32-S3** for this board,
while retaining ESP32 behavior for existing CYDs. The hub's README explicitly
excludes S3 projects and its page describes CYDs only, so update that scope and
board copy when adding an S3 entry. ESP Web Tools supports ESP32-S3 and requires
HTTPS: [official integration guide](https://esphome.github.io/esp-web-tools/).

Use the existing four-part manifests and derive offsets from actual build
metadata. Do not copy classic ESP32 bootloader offsets into the S3 configuration.
Keep merged images for explicitly documented clean-install downloads only;
they fill gaps covering NVS and are unsuitable for preserving settings on Update.
Including boot_app0 resets OTA selection so a device running app1 boots the
newly flashed app0. Test this behavior on the actual board.

The scaffold already has FIRMWARE_VERSION; the likely weather project uses
`kFwVersion` instead. The generator expects a literal FIRMWARE_VERSION and
PROJECT_NAME, so align identities with one authoritative version and use the
same name for Improv reports. Public weather builds must work without secrets.h
and need runtime provisioning for any optional weather keys. Blank Wi-Fi secrets
alone are insufficient if another public-build dependency still requires that file.

Improv is optional for basic USB installation, but is required to match the
existing process's firmware detection, Configure WiFi, and settings-preserving
Update experience. Reuse its patched vendored library and integrate servicing
early enough to answer during provisioning and startup downloads. Validate
compatibility with this project's Arduino 3.x platform; do not blindly replace
its pinned platform with a CYD configuration.

## Phased plan

| Phase | Work | Completion evidence |
|---|---|---|
| 0 — Confirm target | Confirm whether ESP32S3-Weather is the intended application; capture its branch, working changes, deployed version, and present API cadence. | Correct source identified; no cross-project assumptions. |
| 1 — Reduce weather traffic | If the weather candidate is confirmed, change refresh to 3600 s and update its docs/version. Keep panel timing and layer cycling stable. | Build passes; observed startup fetch and next scheduled fetch match policy; cache hits do not fetch tiles. |
| 2 — Reliability and documentation | Complete the weather application's full review; address stale data/failed refresh handling. Apply the demo findings only if retaining it as a reusable baseline. | Failure tests preserve last good data; logs/docs match behavior; touch and display remain responsive. |
| 3 — Prepare public installs | Add stable project identity, optional-secret fallbacks, runtime configuration, Improv, installer metadata and post-build artifacts. Generalize shared chip-family handling. | Clean build without personal secrets; valid S3 manifests; existing CYD manifests unchanged. |
| 4 — Integrate release workflow | Add project workflow, version/tag checks, Pages artifacts, checksums, and installer runbook; broaden hub scope and add the confirmed repo to projects.json. | Branch CI produces a usable preview; no unrequested tag or publication. |
| 5 — Validate and release | Test erase/install, provisioning, same-firmware detection, changed-version Update, settings retention, app1 Update, wrong-chip selection, and power-cycle touch behavior. | Dated hardware results in WEB_INSTALLER.md; deliberate tagged release; live hub card and assets verified. |
| 6 — WebUI refresh and maintenance | Refresh the WebUI using the Health App project UI as the template (layout, typography, colors, cards and controls); update the README WebUI screenshot. Split weather/radar/base-map policies and extract network/cache/render modules incrementally. | Desktop/mobile comparison with Health App; mirror touch, settings, version/data-age/status and all controls verified; page size/heap and polling responsiveness measured; long-run request counts support cache changes. |

Start with the confirmed weather timer and a small release. Avoid coupling that
change to a display-driver rewrite or broad framework upgrade.

## Phase 0 execution record — completed 12 October 2026

The user confirmed ESP32S3-Weather as the intended target and requested this
plan be copied here. The original scaffold review is retained as historical
context, not presented as a full review of the weather firmware.

### Source baseline

- Repository: `git@github.com:anthonyjclarke/ESP32S3-Weather.git`.
- Active branch: `dev`; HEAD: `964ca6b` — saved Wi-Fi credential detection fix.
- Local main branch does not exist; cached `origin/main` is `fc3a7e8`.
- Against cached `origin/dev`, local dev is one commit ahead. Remote refs were
  not fetched, so this does not establish the current server state.
- Existing changes: `.vscode/extensions.json` modified; `.claude/` untracked.
  These were preserved. Only this plan document was added by this phase.
- Firmware identity: `kFwVersion = "1.5.2"`; latest CHANGELOG entry is
  `1.5.4` (9 October 2026). Resolve this discrepancy when cutting the next
  firmware version; do not assume the changelog version is what's deployed.
- Running-device version and observed request traffic are not verified:
  no device was contacted or flashed. Source cadence is recorded below.

### Confirmed source behavior

- `include/config.h:37–38`: layers cycle every 30 s; weather/cache interval
  is 1800 s (30 minutes).
- `src/main.cpp:3063–3064`: startup performs one weather fetch in setup.
- `src/main.cpp:3095–3097`: periodic timing starts after setup and queues the
  first map render.
- `src/main.cpp:3214–3223`: periodic refresh is gated by `appState == 0`,
  advances the attempt timestamp, sets `weatherRefreshPending`, and requests
  a forced layer render. The render worker fetches weather before rendering.
- `src/main.cpp:509–512`: the same interval defines composite-cache freshness.
- `src/main.cpp:2475` onward: fresh cache hits reuse sprites; missing/stale
  caches and forced renders fetch tiles. Zoom/style/opacity controls can also
  force rendering, so total traffic cannot be calculated solely from 1800 s.
- `ENHANCEMENTS.md` already proposes separating base-map and overlay caches.
  Reuse that design discussion in later phases rather than creating a parallel
  proposal.

### Validation

Baseline command: `pio run -e esp32s3-7inch` — **PASS**.

- Build duration: 32.665 s.
- Static RAM: 104,720 / 327,680 bytes (32.0%).
- Application flash: 1,394,691 / 5,242,880 bytes (26.6%).
- Resolved libraries: LovyanGFX 1.2.21, WiFiManager 2.0.17, ArduinoJson
  6.21.6, PNGdec 1.1.6, esp-lib-utils 0.1.2; Arduino framework 3.3.7.
- Full build log: `/private/tmp/esp32s3-weather-phase0-build.log`.

This was a local build with the existing secrets file. It is not a public
release artifact. `src/main.cpp:21` includes secrets.h unconditionally, while
that file is not tracked: public CI needs an optional include plus defaults
before installer builds can succeed without personal configuration.

### Phase 1 ready

Implement `kRealtimeRefreshSecs = 3600`, document hourly weather/cache
refreshes, and reconcile the version/changelog identity. Keep 30 s layer
cycling, panel timing, and rendering architecture stable for this small change.
Build again, then verify request timing and cache reuse on hardware before
calling runtime behavior validated. No Phase 1 firmware change has been made
in this Phase 0 execution.

## Display recovery — 12 October 2026

The user reported a blank display and LCD_CAM interrupt allocation failure
after the baseline build. The previous phase did not upload firmware, but its
build used LovyanGFX 1.2.21, whose installed RGB driver omits the shared
interrupt flag under ESP-IDF 5.5. This matches upstream issues #841/#844.
The build-only validation did not establish runtime display compatibility.

`platformio.ini` now pins the upstream fix at
`d504ab71264b8197ede527746fa1a21b5d20e075` instead of the floating registry
constraint. This restores `ESP_INTR_FLAG_SHARED` in Bus_RGB and makes the
display dependency reproducible. Panel timing, pins, settings, and the
30-minute weather interval remain unchanged.

Upstream fix: https://github.com/lovyan03/LovyanGFX/commit/d504ab71264b8197ede527746fa1a21b5d20e075

Rebuild and hardware validation are recorded separately below.

Recovery build: `pio run -e esp32s3-7inch` **PASS** (52.094 s).
Installed dependency HEAD verified as the pinned fix commit; Bus_RGB line 335
contains `ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED`.
Flash: 1,395,239 / 5,242,880 bytes; static RAM: 104,720 bytes.
Log: `/private/tmp/esp32s3-weather-display-fix-build.log`.
No upload or device erase performed. Recovery is pending flashing and checking
that the LCD_CAM error disappears and the startup display is visible.

### Recovery confirmed and version reporting updated

The user confirmed display recovery after flashing the pinned upstream fix.
This supersedes the earlier pending-hardware-verification notes.
The incident and confirmed recovery are recorded under CHANGELOG 1.5.5.
Firmware identity is now 1.5.5; `/api/status` and the WebUI use kFwVersion,
matching startup and serial output. These WebUI/version changes require a
new upload; no upload was performed during this edit.

## Phase 1 execution — 12 October 2026

Implemented in firmware 1.5.6: `kRealtimeRefreshSecs` is now 3600 s (60 min).
The shared cache lifetime follows that interval. Startup fetches, the 30 s
layer cycle, panel settings, and the pinned LovyanGFX fix are retained.
README now distinguishes scheduled weather fetches from extra tile traffic and
explains that the map-page guard still defers refreshes on graph pages.

Added the supplied `images/TFT-Display.jpg` to README and a WebUI screenshot
placeholder without introducing a broken image link.

Implementation is complete; build validation follows. Hardware cadence/cache
verification remains pending a new upload and observation. Expected boot log:
`Realtime refresh: 3600s`. While the map page remains active, expect the next
`Realtime refresh due | interval=3600s` after roughly one hour (it may wait for
an active render to finish). Normal 30 s layer switching should log cache hits
once each layer is populated, without repeated tile downloads until expiry.
Confirm the WebUI reports 1.5.6 and refreshSec=3600, then check that returning
from a graph page after the interval schedules the deferred refresh. Record
any forced zoom/style/opacity renders separately when measuring API traffic.

Phase 1 build validation: `pio run -e esp32s3-7inch` **PASS** (15.501 s).
Static RAM: 104,720 bytes; application flash: 1,395,535 / 5,242,880 bytes.
`git diff --check` passed; README display-image path resolves to the supplied
JPEG. Build log: `/private/tmp/esp32s3-weather-phase1-build.log`.
No firmware upload performed; runtime cadence and cache reuse remain pending
hardware verification. Phase 2 review is the next development phase.

## Phase 2 execution — 12 October 2026

Maintained-source review and the first reliability fixes are prepared in 1.5.7.
See [the full review](docs/PHASE_2_REVIEW.md) for fixed/deferred findings,
installer prerequisites, validation limits and the hardware acceptance list.

Forecast/radar failures retain last good data; forecast snapshots cross a queue
and expose successful-data age separately from attempt age. Hourly refresh
continues on graphs while awake, with five-minute retries after failed weather
requests. Main owns cache publication after atomic completion; incomplete maps
do not replace complete matching caches. Tile bounds, NTP/graph guards, strict
sleep-time parsing, escaped WebUI metrics and shared I2C reset locking are added.
RainViewer radar requests are limited to documented zoom 7.

Isolated build PASS (38.844 s), RAM 104,768 bytes, application flash 1,400,371 /
5,242,880 bytes. Native input/timing/graph tests and WebUI JavaScript checks PASS.
Log: `/private/tmp/esp32s3-weather-phase2-build-reused.log`. Existing dependencies
were reused after an isolated download stalled. The original project's .pio
upload artifacts were preserved for the user's concurrent 1.5.6 test.

This completes the review and initial reliability implementation, not hardware
acceptance. No device upload performed. Deferred P2 issues (timestamp-based
forecast labels, bounded verified JSON/TLS, PWM failure handling and provider
policy/attribution) remain recorded; address them before public distribution.
Phase 3 is public-build/runtime configuration/ESP32-S3 installer preparation.

## Phase 6 WebUI design addition — 12 October 2026

Refresh the WebUI format using the local Health App project's
`public/index.html`, `public/style.css`, and System / Diagnostics UI as the
design reference. Adapt its paper/green palette, typography, card layout,
spacing and form controls to the weather dashboard. Keep the TFT mirror and
weather status prominent, group settings clearly, and provide a dedicated
system/diagnostics section. Preserve mirror touch, existing controls, persisted
settings, version reporting and successful-data age.

The detailed scope and acceptance checks are in
[ENHANCEMENTS.md](ENHANCEMENTS.md#webui-format-refresh--health-app-template).
This is planned work; the current request updates the todo and plan only.

## Phase 3 execution — 12 October 2026

Prepared firmware 1.5.8 with stable identity, optional-secret fallbacks, a public
build that excludes personal secrets, WebUI runtime device configuration/NVS,
and native USB Improv using the existing installer library/process. Running
firmware is visible in initial WebUI HTML and all version paths share one value.

Public post-build metadata and four-part packaging derive flash offsets from
PlatformIO. Package validation checks ESP32-S3 identity, current public-build
checksum/version, app/OTA partition fit and absence of NVS writes; SHA256SUMS
is generated. Shared `cyd-web-installer` was independently updated during this work to 1.2.0
(commit `bd32c59`), deriving ESP32/S3 chip identity from the application header.
The newer shared checkout was preserved and used to regenerate the preview.
Existing ESP32 manifests remain unchanged; index metadata now includes chip
identity. Phase 4 should pin that compatible shared-tools revision.


Local and public isolated builds PASS; the public build ignores an intentionally
failing secrets header. Settings, Improv parser/sanitizer, manifest/partition
and JavaScript/version checks PASS. Detailed evidence and hardware acceptance
are in [WEB_INSTALLER.md](docs/WEB_INSTALLER.md). Existing project upload
artifacts were not replaced. No device flashing, push, tag or publication.

Implementation preparation is complete; hardware acceptance and deferred P2
release prerequisites remain open. Next: Phase 4 workflow/hub integration,
followed by Phase 5 physical install/update tests and deliberate release.
Phase 6 retains the Health App-inspired WebUI refresh request.

## Phase 6 WebUI execution — 12 October 2026

Firmware 1.5.9 implements the Health App-inspired paper/green WebUI with
Dashboard, Settings and System navigation, responsive cards/forms, visible
firmware version, keyboard focus and a scrollable cache table. Existing API
requests, mirror touch mapping, settings, polling and refresh policy are retained.
No panel driver, interrupt, backlight or weather scheduling code was changed.

Validation: isolated default-environment build PASS; RAM 105,536 bytes and
application flash 1,426,719 bytes (about 2 KB more than 1.5.8). JavaScript syntax,
unique/preserved control IDs and byte-for-byte unchanged backend checks PASS.
Browser checks PASS: desktop/390px phone layouts, all three sections, populated
forms/status/cache/hardware, mocked device save, night toggle, keyboard zoom and
mirror touch mapping (400,240). No phone page overflow; cache table scrolls.
Mock requests confirm existing endpoints/payloads. These checks use synthetic
status data and the display photo, not live board acceptance. Build log:
`/private/tmp/weather-rebrand/build.log`. Hardware acceptance:
check all three sections on desktop/phone, mirror touch, zoom/opacity, night
controls, device save/restart/persistence, version and diagnostics after upload.
README includes a clearly labelled design preview; replace with a real board
WebUI screenshot after acceptance. The remaining
Phase 6 maintenance tasks are still open.

Phases 4–5 resumed 12 October 2026 at the user's instruction. Done locally:
caller workflow (`envs: esp32s3-7inch-public`, shared tooling `@main`) and a
`7″` board label for the hub size filter. Remaining: push `dev` and check the
`site-preview`, one-time Pages/tag-rule setup, hardware acceptance, `v*`
release from `main`, then the hub `projects.json` entry. Ask before each push.
