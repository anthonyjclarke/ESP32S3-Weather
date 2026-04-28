# Changelog

## v1.0.0 - 2026-04-28

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
