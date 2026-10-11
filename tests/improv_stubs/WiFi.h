#pragma once
#include <string>
#include <cstdint>
constexpr int WIFI_AUTH_OPEN = 0;
constexpr int WL_CONNECTED = 3;
struct IPAddress { uint8_t operator[](int index) const { return index == 0 ? 192 : (index == 1 ? 168 : 1); } };
struct StubWiFi {
  int status() const { return WL_CONNECTED; }
  IPAddress localIP() const { return {}; }
  void disconnect() {}
  void begin(const char*, const char*) {}
  int scanNetworks() const { return 0; }
  std::string SSID(int) const { return "test"; }
  int RSSI(int) const { return -50; }
  int encryptionType(int) const { return 0; }
};
inline StubWiFi WiFi;
inline void delay(int) {}
