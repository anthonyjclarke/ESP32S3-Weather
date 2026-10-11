#include "device_settings.h"
#include <cassert>
#include <limits>
#include <iostream>

int main() {
  DeviceSettings current;
  std::strcpy(current.locationName, "Sydney");
  std::strcpy(current.timezone, "AEST-10AEDT,M10.1.0,M4.1.0/3");
  std::strcpy(current.owmKey, "existing-key");
  std::strcpy(current.otaPassword, "existing-password");
  current.latitude = -33.86; current.longitude = 151.2;
  assert(validDeviceSettings(current));
  DynamicJsonDocument doc(2048);
  doc["locationName"] = "Putney";
  doc["latitude"] = -33.8261;
  doc["longitude"] = 151.1063;
  doc["timezone"] = "AEST-10AEDT,M10.1.0,M4.1.0/3";
  doc["owmKey"] = ""; doc["otaPassword"] = "";
  DeviceSettings next;
  assert(deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  assert(std::strcmp(next.locationName, "Putney") == 0);
  assert(std::strcmp(next.owmKey, "existing-key") == 0);
  assert(std::strcmp(next.otaPassword, "existing-password") == 0);
  doc["latitude"] = 90;
  assert(!deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  doc["latitude"] = "0";
  assert(!deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  doc["latitude"] = std::numeric_limits<double>::quiet_NaN();
  assert(!deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  doc["latitude"] = -33.8261;
  doc["longitude"] = 181;
  assert(!deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  doc["longitude"] = 151.1063;
  doc["locationName"] = std::string(64, 'x');
  assert(!deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  doc["locationName"] = "bad\nname";
  assert(!deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  doc["locationName"] = "Putney";
  doc["clearOwmKey"] = true;
  assert(deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  assert(next.owmKey[0] == 0 && current.owmKey[0] != 0);
  doc["owmKey"] = "new-key";
  assert(!deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  doc["clearOwmKey"] = false;
  assert(deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  assert(std::strcmp(next.owmKey, "new-key") == 0);
  doc["otaPassword"] = nullptr;
  assert(!deviceSettingsCandidate(doc.as<JsonVariantConst>(), current, next));
  std::cout << "Device settings validation and credential retention passed\n";
}
