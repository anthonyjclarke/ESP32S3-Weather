#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>

struct ForecastData {
  float currentTemp, morningTemp, noonTemp, eveningTemp;
  int morningCode, noonCode, eveningCode;
  float dMax[16], dMin[16], dRain[16], dPress[16], dCloud[16];
  float dHum[16], dWind[16], dUV[16], dSolar[16];
  int dCode[16];
};

inline bool readForecastFloat(JsonVariantConst value, float& out) {
  if (!value.is<float>()) return false;
  out = value.as<float>();
  return std::isfinite(out);
}

// Parse into an unpublished snapshot. A missing/null/non-numeric field must
// never turn into a plausible zero in the displayed forecast.
inline bool readForecast(JsonVariantConst doc, ForecastData& out) {
  if (!readForecastFloat(doc["current"]["temperature_2m"], out.currentTemp)) return false;
  const int hours[] = {9, 14, 21};
  float* temps[] = {&out.morningTemp, &out.noonTemp, &out.eveningTemp};
  int* codes[] = {&out.morningCode, &out.noonCode, &out.eveningCode};
  for (int i = 0; i < 3; ++i) {
    if (!readForecastFloat(doc["hourly"]["temperature_2m"][hours[i]], *temps[i]) ||
        !doc["hourly"]["weather_code"][hours[i]].is<int>()) return false;
    *codes[i] = doc["hourly"]["weather_code"][hours[i]].as<int>();
  }
  const char* dailyNames[] = {
    "temperature_2m_max", "temperature_2m_min", "precipitation_sum",
    "relative_humidity_2m_mean", "wind_speed_10m_max", "uv_index_max",
    "shortwave_radiation_sum"
  };
  float* values[] = {out.dMax, out.dMin, out.dRain, out.dHum, out.dWind, out.dUV, out.dSolar};
  for (int day = 0; day < 16; ++day) {
    for (int field = 0; field < 7; ++field) {
      if (!readForecastFloat(doc["daily"][dailyNames[field]][day], values[field][day])) return false;
    }
    if (!doc["daily"]["weather_code"][day].is<int>() ||
        !readForecastFloat(doc["hourly"]["pressure_msl"][day * 24 + 12], out.dPress[day]) ||
        !readForecastFloat(doc["hourly"]["cloud_cover"][day * 24 + 12], out.dCloud[day])) return false;
    out.dCode[day] = doc["daily"]["weather_code"][day].as<int>();
  }
  return true;
}

inline bool validTileDimensions(int width, int height) {
  return width == 256 && height == 256;
}

inline bool refreshDue(uint32_t now, uint32_t attemptedAt, uint32_t intervalMs) {
  return uint32_t(now - attemptedAt) >= intervalMs;
}

inline int graphCoordinate(float value, float minimum, float maximum, int start, int end) {
  if (!(maximum > minimum)) return start;
  return start + static_cast<int>((value - minimum) / (maximum - minimum) * (end - start));
}
