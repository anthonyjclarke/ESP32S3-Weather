#include "weather_validation.h"
#include <cassert>
#include <limits>
#include <iostream>

int main() {
  DynamicJsonDocument doc(65536);
  doc["current"]["temperature_2m"] = 16.1;
  for (int hour = 0; hour < 384; ++hour) {
    doc["hourly"]["temperature_2m"][hour] = 20.0;
    doc["hourly"]["weather_code"][hour] = 3;
    doc["hourly"]["pressure_msl"][hour] = 1013.5;
    doc["hourly"]["cloud_cover"][hour] = 50.0;
  }
  const char* names[] = {"temperature_2m_max", "temperature_2m_min", "precipitation_sum",
    "relative_humidity_2m_mean", "wind_speed_10m_max", "uv_index_max", "shortwave_radiation_sum"};
  for (int day = 0; day < 16; ++day) {
    for (const char* name : names) doc["daily"][name][day] = 5.0;
    doc["daily"]["weather_code"][day] = 61;
  }
  assert(!doc.overflowed());
  ForecastData data{};
  assert(readForecast(doc.as<JsonVariantConst>(), data));
  assert(data.dPress[15] == 1013.5f && data.dCode[15] == 61);
  doc["daily"]["precipitation_sum"][15] = nullptr;
  assert(!readForecast(doc.as<JsonVariantConst>(), data));
  doc["daily"]["precipitation_sum"][15] = "bad";
  assert(!readForecast(doc.as<JsonVariantConst>(), data));
  doc["daily"]["precipitation_sum"][15] = 0.0;
  assert(readForecast(doc.as<JsonVariantConst>(), data));
  doc["current"]["temperature_2m"] = std::numeric_limits<float>::infinity();
  assert(!readForecast(doc.as<JsonVariantConst>(), data));
  doc["current"]["temperature_2m"] = 0.0;
  doc["hourly"]["pressure_msl"].as<JsonArray>().remove(383);
  doc["hourly"]["pressure_msl"].as<JsonArray>().remove(382);
  doc["hourly"]["pressure_msl"].as<JsonArray>().remove(381);
  doc["hourly"]["pressure_msl"].as<JsonArray>().remove(380);
  for (int i = 379; i >= 372; --i) doc["hourly"]["pressure_msl"].as<JsonArray>().remove(i);
  assert(!readForecast(doc.as<JsonVariantConst>(), data));
  assert(graphCoordinate(1.25f, 1.0f, 1.5f, 100, 0) == 50);
  assert(graphCoordinate(2.0f, 2.0f, 2.0f, 100, 0) == 100);
  assert(validTileDimensions(256, 256));
  assert(!validTileDimensions(512, 256));
  assert(!validTileDimensions(256, 512));
  assert(!validTileDimensions(0, 256));
  assert(!refreshDue(3599999, 0, 3600000));
  assert(refreshDue(3600000, 0, 3600000));
  assert(refreshDue(20, UINT32_MAX - 9, 30));
  assert(!refreshDue(19, UINT32_MAX - 9, 30));
  std::cout << "Weather validation, tile bounds, and wrap-safe timing passed\n";
}
