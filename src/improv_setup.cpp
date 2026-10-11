#include <Arduino.h>
#include <WiFi.h>
#include <HWCDC.h>
#include <ImprovWiFiLibrary.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include "config.h"
#include "debug.h"
#include "improv_setup.h"

// Native USB Serial/JTAG is used for browser provisioning. Keep the existing
// UART Serial/logging configuration and LCD/task placement unchanged.
#if ARDUINO_USB_CDC_ON_BOOT
static HWCDC& installerUsb = HWCDCSerial;
#else
static HWCDC installerUsb;
#endif
static ImprovWiFi improv(&installerUsb);
static bool provisioned = false;

static bool connectWifi(const char* ssid, const char* password) {
  // Start STA without removing a running WiFiManager AP.
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
  else if (mode == WIFI_AP) WiFi.mode(WIFI_AP_STA);
  wifi_config_t previous = {};
  const bool restore = esp_wifi_get_config(WIFI_IF_STA, &previous) == ESP_OK && previous.sta.ssid[0];
  WiFi.persistent(true);
  WiFi.begin(ssid, password);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000UL) delay(100);
  if (WiFi.status() == WL_CONNECTED) return true;
  if (restore) {
    WiFi.begin(reinterpret_cast<const char*>(previous.sta.ssid),
               reinterpret_cast<const char*>(previous.sta.password));
  } else {
    WiFi.disconnect(false, true); // don't retain a failed first-boot credential
  }
  return false;
}

static void provisionTask(void*) {
  for (;;) {
    for (int i = 0; i < 64 && installerUsb.available(); ++i) improv.handleSerial();
    if (provisioned) {
      installerUsb.flush();
      delay(200);
      ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void beginInstallerProvisioning() {
  installerUsb.begin(115200);
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char name[40];
  snprintf(name, sizeof(name), "ESP32S3-Weather-%02X%02X", mac[4], mac[5]);
  improv.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, PROJECT_NAME,
                       FIRMWARE_VERSION, name, "http://{LOCAL_IPV4}/");
  improv.setCustomConnectWiFi(connectWifi);
  improv.onImprovConnected([](const char*, const char*) { provisioned = true; });
  if (xTaskCreatePinnedToCore(provisionTask, "improv", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
    DBG_ERROR("Improv task allocation failed; use captive portal provisioning");
  }
}
