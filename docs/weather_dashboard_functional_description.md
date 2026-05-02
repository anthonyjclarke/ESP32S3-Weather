# Weather Dashboard Function Overview

This project turns an ESP32-based colour display into a self-contained, Wi-Fi connected weather dashboard. It combines live weather data, tiled map imagery, weather overlay layers, forecast summaries, and touch-driven navigation into a single screen interface suitable for a desktop, wall-mounted, or bench-top display.

The original reference project was published by Mirko Pavleski on Hackster.io and describes a weather dashboard using an ESP32-S3 display, Open-Meteo forecast data, OpenWeatherMap map layers, RainViewer radar tiles, NTP time synchronisation, and PNG tile decoding. This fork is implemented differently, but preserves the same overall functional intent: a compact interactive weather station with live data, map views, and multi-day forecast visualisation.

---

## What It Does

The dashboard connects to Wi-Fi, synchronises the current time, downloads weather data for a configured latitude and longitude, and renders a graphical weather interface on the display.

The main screen shows:

- A map centred on the configured location.
- A visible location marker drawn over the map.
- Selectable weather overlay layers such as radar, cloud cover, and rain or precipitation intensity.
- Current temperature and current weather condition.
- Current date and time.
- Wi-Fi signal status.
- A short forecast for the current day.
- A multi-day forecast summary.
- Touch areas for switching map styles, zoom levels, overlay layers, brightness, and forecast graph pages.

---

## Data Sources

| Data                  | Purpose                                  | Notes                                      |
|:----------------------|:-----------------------------------------|:-------------------------------------------|
| Open-Meteo            | Current conditions and 16-day forecast   | No account required for basic forecast use |
| OpenWeatherMap        | Map overlay layers                       | API key required                           |
| RainViewer            | Radar tile imagery                       | Used for live radar-style map overlays     |
| NTP                   | Time and date synchronisation            | Keeps the dashboard clock accurate         |
| Map tile providers    | Background maps                          | Dark, topographic, and street-map styles   |

---

## Main Display Behaviour

On startup, the device:

1. Connects to the configured Wi-Fi network.
2. Synchronises time using NTP.
3. Downloads weather forecast data for the configured location.
4. Downloads the map and weather overlay tiles.
5. Decodes the image tiles and draws them into a complete map view.
6. Draws the dashboard interface, marker, weather data, clock, and touch controls.

The map view is built from multiple smaller tiles, which are fetched and assembled into one continuous image. Weather layers are then drawn over the base map, followed by the location marker and dashboard UI elements.

---

## Interactive Map Functions

The dashboard supports touch-based map interaction.

| Control area        | Function                                      |
|:--------------------|:----------------------------------------------|
| Centre map press    | Cycles through available zoom levels          |
| Clock / map style   | Switches base map style                       |
| Top layer selector  | Switches weather overlay layer                |
| Side brightness bar | Adjusts screen backlight                      |
| Forecast buttons    | Opens detailed 16-day forecast graph screens  |
| Back button         | Returns from graph view to the main dashboard |

Supported base-map styles include a dark map, a topographic map, and an OpenStreetMap-style map. The dark map is especially useful because weather overlays are easier to read against a low-contrast background.

---

## Weather Layers

The weather map can display different meteorological layers over the selected base map.

| Layer       | Description                                  |
|:------------|:---------------------------------------------|
| Radar       | Shows radar-style precipitation activity     |
| Clouds      | Shows cloud-cover conditions over the map    |
| Rain        | Shows rain or precipitation intensity layer  |

The active weather layer is shown on the main screen and can be changed from the touch interface.

---

## Forecast Display

The dashboard provides both immediate and extended forecast information.

The main screen includes:

- Current temperature.
- Weather condition icon.
- Morning, noon, and evening forecast values for the current day.
- Short multi-day min/max forecast summary.

A set of forecast buttons opens detailed graph pages for longer-range weather trends. The reference design uses eight graph views, each showing a different 16-day forecast variable.

Typical graph views include:

| Graph | Forecast variable                  |
|:------|:-----------------------------------|
| T     | Min/max temperature and condition  |
| R     | Rain or precipitation              |
| P     | Pressure                           |
| C     | Cloud cover                        |
| H     | Humidity                           |
| W     | Wind speed                         |
| U     | UV index                           |
| S     | Solar radiation                    |

Each graph page shows day labels, dates, scaled values, weather icons where relevant, and a back control to return to the main dashboard.

---

## Weather Icons

The interface uses simple graphical weather icons to make conditions readable at a glance.

Supported conditions include:

- Sunny.
- Partly cloudy.
- Mostly cloudy.
- Cloudy.
- Rain.
- Heavy rain.
- Snow.

The exact icon artwork may vary in this fork, but the functional purpose is the same: translating weather condition codes into compact visual indicators.

---

## Update Cycle

Weather data is refreshed periodically. In the reference design, weather information updates approximately every 10 minutes. A progress indicator on the side of the display shows the time elapsed since the last update.

This helps make the dashboard feel live without constantly reloading data or making unnecessary API requests.

---

## Functional Summary

This project is best described as an interactive ESP32 weather station with:

- Wi-Fi based weather and map data retrieval.
- Configurable location by latitude and longitude.
- Live clock and date display.
- Map-centred visual weather display.
- Selectable map styles.
- Selectable weather overlay layers.
- Current conditions display.
- Today forecast summary.
- Multi-day forecast summary.
- Detailed 16-day forecast graph pages.
- Touchscreen navigation.
- Brightness control.
- Periodic data refresh.

Although this fork may use a different code structure, hardware target, graphics library, screen size, rendering pipeline, or configuration method, it aims to provide the same user-facing behaviour: a polished, interactive weather dashboard running locally on an ESP32 display.

---

## Credits

Functional concept based on the Hackster.io project **ESP32 Weather Dashboard with Satellite Maps and 16-day Forec** by **Mirko Pavleski**, published April 2026.

This fork is not a direct copy of the original implementation. It re-creates the functional concept using a different build approach while preserving the same overall dashboard behaviour.
