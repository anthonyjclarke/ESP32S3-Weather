#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>
#include <cstdint>

struct DeviceSettings {
  uint32_t schema = 1;
  char locationName[64] = {};
  double latitude = 0;
  double longitude = 0;
  char timezone[96] = {};
  char owmKey[129] = {};
  char otaPassword[64] = {};
};

inline bool validSettingText(const char* text, size_t capacity, bool allowEmpty = false) {
  if (!text || (!allowEmpty && !text[0]) || strlen(text) >= capacity) return false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    if (*p < 32 || *p == 127) return false;
  }
  return true;
}

inline bool validDeviceSettings(const DeviceSettings& s) {
  return s.schema == 1 && std::isfinite(s.latitude) && std::isfinite(s.longitude) &&
    std::abs(s.latitude) <= 85.05112878 && std::abs(s.longitude) <= 180 &&
    validSettingText(s.locationName, sizeof(s.locationName)) &&
    validSettingText(s.timezone, sizeof(s.timezone)) &&
    validSettingText(s.owmKey, sizeof(s.owmKey), true) &&
    validSettingText(s.otaPassword, sizeof(s.otaPassword), true);
}

// Build a candidate only. The caller saves it atomically as one NVS blob and
// reboots to apply it, leaving active worker configuration immutable.
inline bool deviceSettingsCandidate(JsonVariantConst doc, const DeviceSettings& current, DeviceSettings& next) {
  next = current;
  const char* name = doc["locationName"].as<const char*>();
  const char* timezone = doc["timezone"].as<const char*>();
  if (!validSettingText(name, sizeof(next.locationName)) ||
      !validSettingText(timezone, sizeof(next.timezone)) ||
      !doc["latitude"].is<double>() || !doc["longitude"].is<double>()) return false;
  std::strcpy(next.locationName, name);
  std::strcpy(next.timezone, timezone);
  next.latitude = doc["latitude"].as<double>();
  next.longitude = doc["longitude"].as<double>();
  const char* fields[] = {"owmKey", "otaPassword"};
  const char* clears[] = {"clearOwmKey", "clearOtaPassword"};
  char* destinations[] = {next.owmKey, next.otaPassword};
  const size_t capacities[] = {sizeof(next.owmKey), sizeof(next.otaPassword)};
  for (int i = 0; i < 2; ++i) {
    if (doc.containsKey(clears[i]) && !doc[clears[i]].is<bool>()) return false;
    const bool clear = doc[clears[i]] | false;
    if (doc.containsKey(fields[i])) {
      const char* value = doc[fields[i]].as<const char*>();
      if (!validSettingText(value, capacities[i], true) || (clear && value[0])) return false;
      if (value[0]) std::strcpy(destinations[i], value);
    }
    if (clear) destinations[i][0] = '\0';
  }
  return validDeviceSettings(next);
}
