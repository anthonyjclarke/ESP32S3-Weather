#pragma once

#include <Arduino.h>

constexpr const char* kFwVersion = "1.5.2";

namespace cfg {

constexpr int kScreenWidth  = 800;
constexpr int kScreenHeight = 480;

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
constexpr int kMapZoomMin = 5;   // minimum touch-cycle zoom
constexpr int kMapZoomMax = 12;  // maximum touch-cycle zoom (overlay tiles top out here)
constexpr int kDefaultMapStyle = 1;          // 0 = dark, 1 = topo, 2 = OSM
constexpr int kBaseMapContrastPercent = 125; // 100 = unchanged
constexpr int kBaseMapBrightness = 18;       // -255 to 255, applied after contrast
constexpr int kRadarOverlayAlphaPercent = 50; // 0 = invisible, 100 = opaque
constexpr int kCloudOverlayAlphaPercent = 50; // 0 = invisible, 100 = opaque
constexpr int kRainOverlayAlphaPercent  = 50; // 0 = invisible, 100 = opaque

constexpr const char* kWifiApName  = "ESP32S3-Weather";
constexpr const char* kOtaHostname = "ESP32S3-Weather";
constexpr const char* kNtpTimezone = "AEST-10AEDT,M10.1.0,M4.1.0/3";
constexpr int         kLayerCycleSecs = 30;
constexpr int         kRealtimeRefreshSecs = 1800; // weather/map API refresh interval
constexpr uint32_t    kRenderTaskStackBytes = 32768;
constexpr UBaseType_t kRenderTaskPriority = 1;
constexpr uint32_t    kRenderStallWarnMs = 10000;
constexpr uint32_t    kRenderStallRepeatMs = 10000;

// Night sleep schedule:
// kSleepScheduleEnabled: master on/off (default off — enable via WebUI or set true here).
// kSleepOnHour / kSleepOnMinute: sleep-start time (display turns off). Window may cross
//   midnight (e.g. 22:00–07:00).
// kSleepOffHour / kSleepOffMinute: wake time (display turns back on).
// kSleepWakeDurationSecs: how long to stay awake after a touch during the sleep window.
// All values are overridden by NVS once saved from the WebUI.

constexpr bool kSleepScheduleEnabled  = false;
constexpr int  kSleepOnHour           = 22;
constexpr int  kSleepOnMinute         = 0;
constexpr int  kSleepOffHour          = 7;
constexpr int  kSleepOffMinute        = 0;
constexpr int  kSleepWakeDurationSecs = 300;

// --- Backlight software PWM dimming (Option 1 — FreeRTOS task via CH422G I2C) ---
//
// Uncomment the #define below to enable. When disabled the build retains the original
// digital on/off behaviour with zero overhead — no task, no mutex, no code change.
//
// When enabled:
//   - setBacklightBrightness() preserves steady on/off behaviour (0=off, >0=full on).
//   - setBacklightDim() drives proportional PWM at 100 Hz (0=off, 255=full, 1–254=dim).
//   - The sleep state machine calls setBacklightDim(kSleepDimBrightness) instead of off.
//   - A Wire mutex guards I2C access between the PWM task (Core 0) and touch (Core 1).
//
// To remove entirely: comment out the #define. All other code paths revert automatically.
//
#define CFG_BACKLIGHT_PWM_ENABLED

// Brightness during sleep window (0–255). Only used when CFG_BACKLIGHT_PWM_ENABLED is set.
// 30 ≈ 12% duty — visible but not intrusive in a dark room. Adjust to taste.

constexpr uint8_t kSleepDimBrightness = 70;

// When true, setBacklightDim(kSleepDimBrightness) is called immediately after
// startBacklightPwm() in setup() so the dim level is visible without waiting for the
// sleep window. Set false for normal operation. Only active when CFG_BACKLIGHT_PWM_ENABLED.

constexpr bool kSleepDimTestMode = false;

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
