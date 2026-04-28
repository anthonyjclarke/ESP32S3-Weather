#pragma once

#include <Arduino.h>

#define FW_VERSION "1.0.0"

namespace cfg {

constexpr int kScreenWidth  = 800;
constexpr int kScreenHeight = 480;

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
constexpr int kDefaultMapStyle = 1;          // 0 = dark, 1 = topo, 2 = OSM
constexpr int kBaseMapContrastPercent = 125; // 100 = unchanged
constexpr int kBaseMapBrightness = 18;       // -255 to 255, applied after contrast

constexpr int kPinD0  = 14;
constexpr int kPinD1  = 38;
constexpr int kPinD2  = 18;
constexpr int kPinD3  = 17;
constexpr int kPinD4  = 10;
constexpr int kPinD5  = 39;
constexpr int kPinD6  = 0;
constexpr int kPinD7  = 45;
constexpr int kPinD8  = 48;
constexpr int kPinD9  = 47;
constexpr int kPinD10 = 21;
constexpr int kPinD11 = 1;
constexpr int kPinD12 = 2;
constexpr int kPinD13 = 42;
constexpr int kPinD14 = 41;
constexpr int kPinD15 = 40;

constexpr int kPinDe    = 5;
constexpr int kPinVsync = 3;
constexpr int kPinHsync = 46;
constexpr int kPinPclk  = 7;

constexpr uint32_t kRgbClockHz = 14000000;

}  // namespace cfg

constexpr int I2C_SDA_PIN = 8;
constexpr int I2C_SCL_PIN = 9;

constexpr uint8_t TOUCH_I2C_ADDR = 0x5D;
constexpr int GT911_INT_PIN = 4;

constexpr uint8_t CH422G_TP_RST  = 1;
constexpr uint8_t CH422G_LCD_BL  = 2;
constexpr uint8_t CH422G_LCD_RST = 3;

constexpr bool TOUCH_SWAP_XY  = false;
constexpr bool TOUCH_INVERT_X = false;
constexpr bool TOUCH_INVERT_Y = false;

#define WIFI_AP_NAME "ESP32S3-Weather"
#define NTP_TIMEZONE "AEST-10AEDT,M10.1.0,M4.1.0/3"
