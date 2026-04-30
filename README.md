# ESP32S3 Weather

Weather dashboard for the Waveshare ESP32-S3-Touch-LCD-7 with live map tiles,
RainViewer radar, optional OpenWeatherMap cloud/rain overlays, and Open-Meteo
forecast data.

This project is based on Mirko Pavleski's (`mircemk`) Hackster project,
[ESP32 Weather Dashboard with Satellite Maps and 16-day Forecast](https://www.hackster.io/mircemk/esp32-weather-dashboard-with-satellite-maps-and-16-day-forec-7ee044) and https://www.youtube.com/watch?v=k2Vu7qecPro.
The original code and concept are by Mirko Pavleski and were adapted here for a
PlatformIO project targeting the Waveshare ESP32-S3-Touch-LCD-7 hardware stack.

## Hardware

This build uses the Waveshare 7-inch ESP32-S3 RGB display - https://docs.waveshare.com/ESP32-S3-Touch-LCD-7

- 800x480 RGB panel
- GT911 capacitive touch
- CH422G reset/backlight expander
- PSRAM-backed map sprite
- PlatformIO / Arduino ESP32 build

## Setup

1. Configure location and map defaults in `include/config.h`.
2. Configure optional credentials in `include/secrets.h`.
3. Build with:

```sh
pio run
```

4. Upload with:

```sh
pio run -t upload
```

5. If no WiFi credentials are stored, connect to the `ESP32S3-Weather` captive
   portal and choose your network.

After the first serial upload and WiFi connection, OTA firmware uploads can use:

```sh
pio run -e esp32s3-7inch-ota -t upload
```

The OTA hostname is configured by `cfg::kOtaHostname` and defaults to
`ESP32S3-Weather.local`.

The local WebUI is always enabled after WiFi connects:

```text
http://ESP32S3-Weather.local/
```

It serves a BMP mirror of the TFT that refreshes only when the firmware updates
the display, plus layer cache status, zoom and brightness controls, hardware
memory/network status, reboot and WiFi reset actions, and footer links. Clicking
the mirror sends the matching TFT touch coordinate back to the device.

## Location Settings

All user-editable location settings are grouped near the top of
`include/config.h`:

```cpp
// Location settings:
// 1. Set kLocationName to the label you want to use for this dashboard.
// 2. Set latitude/longitude in decimal degrees. South is negative, east is positive.
// 3. kMapZoom controls the startup zoom. kMapZoomMin/kMapZoomMax set the touch-cycle
//    range. Radar and OWM overlay tiles are only available up to zoom 12; above that
//    the base map still works but overlays will not load.
// 4. kDefaultMapStyle selects startup map base: 0 = dark, 1 = topo, 2 = OSM.
// 5. The base-map contrast/brightness values tune tile visibility on the LCD.
constexpr const char* kLocationName = "Putney, NSW, Australia";
constexpr double kLocationLatitude  = -33.8261;
constexpr double kLocationLongitude = 151.1063;
constexpr int kMapZoom    = 7;
constexpr int kMapZoomMin = 5;
constexpr int kMapZoomMax = 12;
constexpr int kDefaultMapStyle = 1;          // 0 = dark, 1 = topo, 2 = OSM
constexpr int kBaseMapContrastPercent = 125;
constexpr int kBaseMapBrightness = 18;
```

`kLocationLatitude` and `kLocationLongitude` drive both the Open-Meteo forecast
request and the map/radar center. The red location marker is drawn at the center
of the map view.

Use decimal-degree coordinates:

- Sydney / east longitudes are positive.
- Southern hemisphere latitudes are negative.
- Example: Putney, NSW is approximately `-33.8261`, `151.1063`.

After changing location settings, rebuild and upload the firmware.

## Secrets

`include/secrets.h` is gitignored. Use `include/secrets.example.h` as the
template:

```cpp
#define SECRET_WIFI_SSID ""
#define SECRET_WIFI_PASS ""
#define SECRET_OWM_API_KEY ""
#define SECRET_OTA_PASSWORD "change-me"
```

WiFi credentials are optional because WiFiManager can provision the device via
the captive portal.

`SECRET_OTA_PASSWORD` must match the `--auth` value in the
`esp32s3-7inch-ota` PlatformIO environment. Change both values before using OTA
on an untrusted network.

## OpenWeatherMap Overlays

The RADAR layer uses RainViewer and does not need an OpenWeatherMap key.

The CLOUDS and RAIN overlay buttons use OpenWeatherMap Weather Maps tiles. Set a
valid key in `include/secrets.h`:

```cpp
#define SECRET_OWM_API_KEY "your-openweathermap-key"
```

If the serial monitor shows `OpenWeatherMap overlay unauthorized (401)`, the key
is missing, not active yet, incorrect, or does not have Weather Maps access.

## API

Periodic weather refreshes and tile rendering run on the render task on Core 1
so touch handling on Core 0 stays responsive during downloads. Startup weather
data is fetched once during `setup()` before the first map render. PNG tile
requests are fetched through `fetchPngToBuffer()` in `src/main.cpp`, which uses
`WiFiClientSecure` and `HTTPClient`, requests uncompressed PNG bytes with
`Accept-Encoding: identity`, applies 15 second connect/read timeouts, and
redacts API keys before logging URLs.

| API or service | What it is used for | Authentication | Where in code |
| --- | --- | --- | --- |
| Open-Meteo Forecast API | Current temperature/weather code, same-day morning/noon/evening forecast points, and 16-day daily forecast values for temperature, weather code, precipitation, humidity, wind, UV, solar radiation, pressure, and cloud cover. | None. The request uses `cfg::kLocationLatitude` and `cfg::kLocationLongitude`. | `getWeatherData()` in `src/main.cpp` builds `https://api.open-meteo.com/v1/forecast?...`, downloads JSON, and fills `currentTemp`, `morningTemp`, `dMax`, `dMin`, `dRain`, `dPress`, `dCloud`, `dHum`, `dWind`, `dUV`, `dSolar`, and `dCode`. |
| RainViewer Weather Maps API | Latest radar metadata, including the tile host and newest past radar frame path. | None. | `getWeatherData()` requests `https://api.rainviewer.com/public/weather-maps.json`, then stores `radarHost`, `radarPath`, and `radarTS`. |
| RainViewer radar tiles | RADAR overlay PNG tiles shown over the selected base map. | None. | `renderRadarMap()` builds tile URLs as `radarHost + radarPath + "/256/{z}/{x}/{y}/1/1_1.png"` when `layerStyle == 0`, then decodes them with PNGdec via `pngDrawOverlayCanvas()`. |
| OpenWeatherMap Weather Maps tiles | CLOUDS and RAIN overlay PNG tiles. CLOUDS uses `clouds_new`; RAIN uses `precipitation_new`. | `SECRET_OWM_API_KEY` in `include/secrets.h`. The template is `include/secrets.example.h`. | `owmApiKey` is initialized near the top of `src/main.cpp`. `renderRadarMap()` skips OWM layers when the key is blank, builds `https://tile.openweathermap.org/map/{layer}/{z}/{x}/{y}.png?appid={key}`, and disables OWM overlays for the rest of the boot after a 401. |
| CARTO basemap tiles | Dark base map style. | None. | `mapUrls[0]` in `src/main.cpp` is `https://basemaps.cartocdn.com/dark_all/`; `renderRadarMap()` appends `{z}/{x}/{y}.png` for every visible base tile. |
| OpenTopoMap tiles | Topographic base map style. | None. | `mapUrls[1]` in `src/main.cpp` is `https://tile.opentopomap.org/`; selected by `cfg::kDefaultMapStyle = 1` or by the map-style touch control. |
| OpenStreetMap tiles | Street-map base map style. | None. | `mapUrls[2]` in `src/main.cpp` is `https://tile.openstreetmap.org/`; selected by map style index `2`. |
| WiFiManager captive portal | WiFi provisioning when no saved credentials are available. | Optional `SECRET_WIFI_SSID` and `SECRET_WIFI_PASS`; otherwise the user provisions through the `ESP32S3-Weather` access point. | `initWiFi()` in `src/main.cpp` uses `WiFiManager::autoConnect(cfg::kWifiApName)` after optionally starting `WiFi.begin()` with secrets. |
| NTP | Local clock and date display. | None. | `setup()` calls `configTzTime(cfg::kNtpTimezone, "pool.ntp.org")` after WiFi connects. `loop()` uses `getLocalTime()` for minute updates. |
| ArduinoOTA | Network firmware upload after initial serial flashing. | `SECRET_OTA_PASSWORD`, which must match the `--auth` value in the `esp32s3-7inch-ota` PlatformIO environment. | `setupOta()` in `src/main.cpp` configures hostname, password, progress/error handlers, and `ArduinoOTA.begin()`. `loop()` calls `ArduinoOTA.handle()` while WiFi is connected. |
| Local WebUI | Browser-based TFT mirror, layer cache status, immediate zoom/brightness/overlay opacity controls, hardware environment info, reboot/WiFi reset actions, and mirrored touch input on the LAN. | None; local LAN only. | `setupWebUi()` in `src/main.cpp` starts `WebServer` on port 80. `/screen.bmp` streams the LCD framebuffer as BMP, `/api/status` returns cache/render/hardware state, `/api/config` applies zoom/brightness and RADAR/CLOUDS/RAIN opacity changes, `/api/touch` maps browser clicks back to TFT coordinates, and `/api/reboot` plus `/api/reset-wifi` handle device actions. |

`cfg::kRealtimeRefreshSecs` controls how often the firmware refreshes
Open-Meteo data, RainViewer metadata, and stale rendered layer caches. Layer,
map-style, and zoom changes can also trigger tile API traffic when no valid
cache exists for the requested view.

## Realtime Refresh And Layer Cache

RADAR, CLOUDS, and RAIN are rendered into separate PSRAM layer caches. Layer
cycling uses those cached sprites instead of fetching fresh tiles on every
change.

The render path has two sprite roles:

- `renderScratch` is the background render target. Network tile fetches and PNG
  decoding happen there while the current map remains on screen.
- `cacheRadar`, `cacheClouds`, and `cacheRain` store the completed layer images.
  Once a render completes, the scratch sprite is copied into the relevant cache
  and that cached sprite becomes the front buffer for display.

The realtime refresh interval is configured in `include/config.h`:

```cpp
constexpr int kRealtimeRefreshSecs = 1800;
```

At the default value, weather data, RainViewer metadata, and map tiles are
refreshed every 30 minutes. Cache misses, map-style changes, and zoom changes
still trigger a render because the stored tile image no longer matches the
requested view.

A serial log like this is expected during normal layer cycling:

```text
[INFO]  Layer cache hit | layer=CLOUDS age=401s | zoom=6 map=TOPO
```

It means the CLOUDS layer was already rendered for the current zoom level and
base map style, and the cached image is still inside the refresh window. In this
example the cached image is 401 seconds old, so the firmware reuses it instead
of downloading and rendering the OpenWeatherMap cloud tiles again.

A fresh cache hit avoids network/API traffic and keeps layer changes responsive.
A new render is scheduled only when the cache is missing, stale, explicitly
forced, or no longer matches the active map style or zoom.

## Notes

The Waveshare board backlight is controlled through the CH422G expander as a
digital enable. The on-screen brightness slider therefore behaves as an on/off
backlight control on this hardware.

## Reference Docs

- [Original source code](docs/weather_dashboard_original_code.c) — Mirko
  Pavleski's reference implementation, preserved for comparison.
- [Functional description](docs/weather_dashboard_functional_description.md) —
  overview of dashboard behaviour, data sources, and UI interactions.
