# ESP32S3 Weather

Weather dashboard for the Waveshare ESP32-S3-Touch-LCD-7 with live map tiles,
RainViewer radar, optional OpenWeatherMap cloud/rain overlays, and Open-Meteo
forecast data.

This project is based on Mirko Pavleski's (`mircemk`) Hackster project,
[ESP32 Weather Dashboard with Satellite Maps and 16-day Forec](https://www.hackster.io/mircemk/esp32-weather-dashboard-with-satellite-maps-and-16-day-forec-7ee044).
The original code and concept are by Mirko Pavleski and were adapted here for a
PlatformIO project targeting the Waveshare ESP32-S3-Touch-LCD-7 hardware stack.

## Hardware

This build uses the Waveshare 7-inch ESP32-S3 RGB display

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

## Location Settings

All user-editable location settings are grouped near the top of
`include/config.h`:

```cpp
// Location settings:
// 1. Set kLocationName to the label you want to use for this dashboard.
// 2. Set latitude/longitude in decimal degrees. South is negative, east is positive.
// 3. kMapZoom controls the startup map zoom; 5-7 is the intended touch-cycle range.
// 4. kDefaultMapStyle selects startup map base: 0 = dark, 1 = topo, 2 = OSM.
// 5. The base-map contrast/brightness values tune tile visibility on the LCD.
constexpr const char* kLocationName = "Putney, NSW, Australia";
constexpr double kLocationLatitude  = -33.8261;
constexpr double kLocationLongitude = 151.1063;
constexpr int kMapZoom = 6;
constexpr int kDefaultMapStyle = 1;
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
```

WiFi credentials are optional because WiFiManager can provision the device via
the captive portal.

## OpenWeatherMap Overlays

The RADAR layer uses RainViewer and does not need an OpenWeatherMap key.

The CLOUDS and RAIN overlay buttons use OpenWeatherMap Weather Maps tiles. Set a
valid key in `include/secrets.h`:

```cpp
#define SECRET_OWM_API_KEY "your-openweathermap-key"
```

If the serial monitor shows `OpenWeatherMap overlay unauthorized (401)`, the key
is missing, not active yet, incorrect, or does not have Weather Maps access.

## Realtime Refresh And Layer Cache

RADAR, CLOUDS, and RAIN are rendered into separate PSRAM layer caches. Layer
cycling uses those cached sprites instead of fetching fresh tiles on every
change.

The realtime refresh interval is configured in `include/config.h`:

```cpp
constexpr int kRealtimeRefreshSecs = 1800;
```

At the default value, weather data, RainViewer metadata, and map tiles are
refreshed every 30 minutes. Cache misses, map-style changes, and zoom changes
still trigger a render because the stored tile image no longer matches the
requested view.

## Notes

The Waveshare board backlight is controlled through the CH422G expander as a
digital enable, matching the Pong Clock project. The on-screen brightness slider
therefore behaves as an on/off backlight control on this hardware.
