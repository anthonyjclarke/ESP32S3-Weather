# ESP32S3-Weather web installer preparation

Phase 3 prepared 12 October 2026, firmware **1.5.8**. This is local preparation;
there is no published installer for this project yet. Phase 4 integrates the
release workflow and hub, and Phase 5 records hardware install/update tests.

## Project contract

| Item | Value |
|---|---|
| Stable project name | `ESP32S3-Weather` — keep unchanged for Update recognition |
| Version source | `FIRMWARE_VERSION` in `include/config.h`; `kFwVersion` aliases it |
| Running-version displays | Startup, serial banner, WebUI initial page and `/api/status`; Improv device info uses the same version |
| Installer environment | `esp32s3-7inch-public` only |
| Hardware | Waveshare ESP32-S3-Touch-LCD-7, 800x480 RGB, 16 MB flash / 8 MB PSRAM; other variants untested |
| Chip family | `ESP32-S3`, not `ESP32` |
| Framework / RGB driver | Existing Arduino 3.3.7 platform and LovyanGFX `d504ab7` recovery pin |
| Partitions | Existing `partitions_custom.csv`; unchanged in this phase |
| Device configuration | NVS namespace `weatherdev`, schema-1 `settings` blob |
| Provisioning | Improv over native USB Serial/JTAG; captive portal remains available |
| Improv listener | Core 0, 4 KB stack, priority 1, every 20 ms, started before startup waits |
| Setup hotspot | `ESP32S3-Weather` |
| Filesystem image | None; WebUI is embedded, and settings are in NVS |

The Waveshare board has native USB and a separate USB-to-UART connection.
Use the **native USB port** for this project's Improv listener. UART logging and
its existing configuration are preserved. Browser access and cold-boot USB
reconnection still need physical tests.
[Waveshare hardware documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7).

The process reuses [cyd-web-installer](https://github.com/anthonyjclarke/cyd-web-installer)
and its generic page, matching the existing
[installer hub](https://github.com/anthonyjclarke/anthonyjclarke.github.io).
The shared tooling was independently updated during this phase to version
1.2.0, commit `bd32c59`: it derives chip family from the built application
header and includes the detected chip in index metadata. That existing update
was preserved; this task made no changes to the shared checkout. Phase 4 should
pin the current compatible tooling revision in the project workflow.


## Personal versus public builds

`pio run -e esp32s3-7inch` optionally includes local `include/secrets.h`. If the
file is absent, WiFi/key/password defaults are blank. Existing personal builds
still use their local credentials as defaults.

`pio run -e esp32s3-7inch-public` defines `WEATHER_PUBLIC_BUILD` and ignores that
file even if present. Public location defaults are Sydney; no OWM key or OTA
password is embedded. WiFi provisioning and Device setup complete onboarding.
Do not distribute personal/default-env build artifacts.

NVS values override build defaults on subsequent boots. A saved blank key stays
blank even if a local build later embeds a key. Overlay/night/WiFi settings keep
their existing namespaces; this phase does not migrate or erase them.

## Device setup and version display

1. Provision WiFi through Improv (supported browser over USB) or the captive portal.
2. Open the device URL, then use **Device setup** in the WebUI.
3. Set location label, latitude/longitude and POSIX timezone; optionally set OWM
   key and OTA password. Example timezone: `AEST-10AEDT,M10.1.0,M4.1.0/3`.
4. Save and restart; configuration is applied together at boot. Active renders
   continue using their existing configuration until restart.

Blank password fields retain saved values. Explicit clear checkboxes remove
keys/passwords; clearing the OTA password disables OTA. On a public first
install OTA is disabled until a password is configured; WebUI mDNS discovery remains available. When using PlatformIO
OTA, its `--auth` value must match the device password.

`GET /api/device` returns location/timezone, project/version and key/password
presence booleans, never credential values. `POST /api/device` accepts the form
fields, validates finite Mercator-safe coordinates and bounded text, saves one
NVS blob, and schedules restart after the response. Timezone syntax is a user
supplied POSIX string; semantic timezone validation/selection remains future work.

The **Running firmware** label appears immediately from the compiled version,
then updates from `/api/status`. It reports firmware actually running on the
board, so 1.5.8 appears only after uploading that firmware.

Use Device setup on a trusted LAN. Existing WebUI transport is unauthenticated
HTTP and NVS credentials are stored without encryption; this phase does not
add Internet access or a new authentication scheme.

## Build and package locally

From this project's root:

```sh
pio run -e esp32s3-7inch-public
python3 tools/package_installer.py --tools-root ../cyd-web-installer
```

The second command requires shared tooling 1.2.0 or later with chip detection. It
checks public-build identity/version/application checksum, invokes the existing
generic installer generator, verifies chip/partition/NVS safety, and writes
`_site/` plus `SHA256SUMS`. The directory is ignored by Git.

`flash_parts.json`, `flash_chip.json` and `installer_build.json` are written by
the public post-build script. Offsets are taken from the PlatformIO upload
configuration. The verified build produced:

| Image | Offset |
|---|---|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

Four separate parts leave the NVS interval `0x9000–0xdfff` unwritten when
updating without erase. `boot_app0.bin` resets OTA selection so the written app0
can boot after an earlier OTA used app1. The package checks application fit,
OTA reset size, non-overlap and absence of NVS writes against `partitions.bin`.
Preservation/slot-selection behavior still requires hardware acceptance.

`firmware-merged.bin` is a clean-install diagnostic artifact, **excluded from
web manifests and packaged site**. It writes through the NVS gap and can erase
settings. ArduinoOTA takes only `firmware.bin`, not a merged image.

Preview `_site` through a local HTTP server; published use requires HTTPS and a
Web Serial capable browser. The generic installer provides board selection,
Install/Update and Improv onboarding. Do not treat a local preview as a released
hub entry. [ESP Web Tools documentation](https://esphome.github.io/esp-web-tools/).

## Vendored provisioning library

`lib/ImprovWiFi` comes from the shared installer's patched Improv WiFi Library
0.0.2, retaining MIT licensing and adjacent-frame/newline parser fixes. Weather
adds receive-buffer, short-RPC and credential-field bounds checks. Provenance:
shared tooling checkout `748276006bf57ccb5e7445f7bf6822973cbc6b31`.
`src/improv_setup.cpp` adapts that process to native USB and ESP32-S3 identity,
restores prior STA credentials after a failed join, and restarts after successful
provisioning. Logs never include passwords.

## Validation recorded

- Final combined build without `secrets.h`: PASS, local 32.997 s / public
  30.176 s. RAM 105,536 bytes; local app flash 1,424,575 bytes, public app flash
  1,424,559 / 5,242,880 bytes. Log:
  `/private/tmp/esp32s3-weather-phase3-verified.log`.
- Earlier public build with an intentionally failing `secrets.h`: PASS, proving
  the personal header is excluded; final image also lacks the personal location
  label. This check was performed before removing the temporary test header.
- Public manifest generation, four flash offsets, partition/NVS checks and
  SHA256SUMS: PASS. No merged image packaged.
- Shared generator checks: firmware-header chip IDs produce ESP32/S3, invalid
  headers/chips are rejected, and legacy ESP32 manifests are byte-identical to
  1.1.0. Current index metadata intentionally adds the detected `chip` field.
- Native settings tests: bad coordinates/types/text rejected; blank credentials
  retained, explicit clearing supported, active candidate unchanged on failure.
- Native Improv parser tests with address/undefined sanitizers: adjacent packets,
  S3 identity/version, short/oversized RPCs and valid credentials PASS. They mock
  the transport/WiFi; they do not verify actual USB timing or association.
- WebUI JavaScript syntax check and built initial version-label check: PASS.
- Phase 2 native weather validation/timing/graph tests retained.

## Hardware acceptance still pending

1. Confirm visible LCD, working touch, WebUI version 1.5.8 and hourly cadence.
2. Erased first install: Improv responds during the captive portal and startup
   downloads, configures WiFi, restarts and offers the device URL.
3. Connect again during startup/render/sleep: correct S3 name/version; matching
   project offers Update. Test incorrect WiFi credentials and recovery.
4. Save location/timezone/key/password, reboot and verify map/forecast agreement,
   persistence, blank retention, explicit clear and OTA password behavior.
5. Update without erase: WiFi, device setup, overlay and night settings survive.
6. OTA to app1, then browser Update: app0 boots the new version.
7. Wrong chip and wrong Waveshare variant: confirm appropriate rejection/selection;
   chip identity alone cannot distinguish different S3 panels.
8. USB reconnect/power-cycle, slow network, missing NTP and WebUI mirror latency.

Resolve the deferred Phase 2 provider-policy/attribution, verified TLS/JSON and
allocation-failure findings before public distribution. Phase 4 adds the project
workflow, checksums/release checks, documentation and hub entry; Phase 5 records
physical acceptance and an intentional release. No erase/upload/tag/push or
publication was performed in Phase 3.
