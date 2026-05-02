// By mircemk, April 2026

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PNGdec.h>
#include <math.h>
#include "time.h"
#include <Wire.h>

const char* ssid     = "***********";
const char* password = "***********";
const char* ntpServer = "pool.ntp.org";

const char* weatherUrl =
  "https://api.open-meteo.com/v1/forecast?"
  "latitude=41.1171&longitude=20.8016"
  "&current=temperature_2m,weather_code"
  "&hourly=temperature_2m,weather_code,pressure_msl,cloud_cover"
  "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,"
  "relative_humidity_2m_mean,wind_speed_10m_max,uv_index_max,shortwave_radiation_sum"
  "&timezone=auto&forecast_days=16";

const char* owmApiKey = "*********************";

int myZoom = 6;
double myLat = 41.1171;
double myLon = 20.8016;
unsigned long lastUpdate = 0;
long radarTS = 0;
int appState = 0;
int lastMinute = -1;
int brightnessLevel = 100;
int mapStyle = 0;   // 0 = dark_all, 1 = opentopomap, 2 = openstreetmap
int layerStyle = 0; // 0 = Radar, 1 = Clouds, 2 = Rain

String radarHost = "https://tilecache.rainviewer.com";
String radarPath = "";

const char* mapUrls[] = {
  "https://basemaps.cartocdn.com/dark_all/",
  "https://tile.opentopomap.org/",
  "https://tile.openstreetmap.org/"
};

const char* mapNames[] = {"DARK", "TOPO", "OSM"};
const char* layerNames[] = {"RADAR", "CLOUDS", "RAIN"};
const char* owmLayerIds[] = {"", "clouds_new", "precipitation_new"};

class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB   _bus;
  lgfx::Panel_RGB _panel;
  lgfx::Light_PWM _light;

  LGFX(void) {
    {
      auto cfg = _panel.config();
      cfg.memory_width = 800;
      cfg.memory_height = 480;
      cfg.panel_width = 800;
      cfg.panel_height = 480;
      _panel.config(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      cfg.pin_d0 = GPIO_NUM_15; cfg.pin_d1 = GPIO_NUM_7; cfg.pin_d2 = GPIO_NUM_6; cfg.pin_d3 = GPIO_NUM_5;
      cfg.pin_d4 = GPIO_NUM_4; cfg.pin_d5 = GPIO_NUM_9; cfg.pin_d6 = GPIO_NUM_46; cfg.pin_d7 = GPIO_NUM_3;
      cfg.pin_d8 = GPIO_NUM_8; cfg.pin_d9 = GPIO_NUM_16; cfg.pin_d10 = GPIO_NUM_1; cfg.pin_d11 = GPIO_NUM_14;
      cfg.pin_d12 = GPIO_NUM_21; cfg.pin_d13 = GPIO_NUM_47; cfg.pin_d14 = GPIO_NUM_48; cfg.pin_d15 = GPIO_NUM_45;
      cfg.pin_henable = GPIO_NUM_41; cfg.pin_vsync = GPIO_NUM_40; cfg.pin_hsync = GPIO_NUM_39; cfg.pin_pclk = GPIO_NUM_0;
      cfg.freq_write = 12000000;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch = 40;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch = 13;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = GPIO_NUM_2;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

LGFX lcd;
LGFX_Sprite mapCanvas(&lcd);

// Weather variables mapping for ZLATEN section
float currentTemp, morningTemp, noonTemp, eveningTemp;
int morningCode, noonCode, eveningCode;
float dMax[16], dMin[16], dRain[16], dPress[16], dCloud[16];
float dHum[16], dWind[16], dUV[16], dSolar[16];
int dCode[16];

// Прототипи на функции
void setBrightnessFromTouchY(int ty);
void getWeatherData();
void renderRadarMap();
void drawBottomDashboard();
void drawSideButtons();
void drawSignature();
void drawTopDate();
void drawProgressTimer();
void drawGraphPage(int type);
void drawWeatherIcon(int x, int y, int code);
void drawMiniWeatherIcon(int x, int y, int code);
int getDayOfMonthOffset(struct tm baseTime, int offsetDays);
bool fetchPngToBuffer(const String& url, uint8_t** outBuf, size_t* outLen);

#include "touch.h"
PNG png;
int globalX, globalY;
uint16_t panelColor = 0x0841;
const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* dayShort2[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
const char* months[] = {"Jan.", "Feb.", "March", "April", "May", "June", "July", "Aug.", "Sept.", "Oct.", "Nov.", "Dec."};

// --- ICON ENGINE ---
void drawSun(int x, int y, uint16_t color) {
  lcd.fillCircle(x, y, 9, color);
  for (int i = 0; i < 360; i += 45) {
    float r = i * DEG_TO_RAD;
    lcd.drawLine(x + cos(r) * 11, y + sin(r) * 11, x + cos(r) * 17, y + sin(r) * 17, color);
  }
}

void drawCloud(int x, int y, uint16_t color, int scale = 1) {
  int off = (scale == 0) ? -2 : 0;
  lcd.fillCircle(x - 6 + off, y + 2, 7 + off, color);
  lcd.fillCircle(x + 2 + off, y - 3, 9 + off, color);
  lcd.fillCircle(x + 10 + off, y + 2, 7 + off, color);
  lcd.fillRect(x - 6 + off, y + 2, 16, 7 + off, color);
}

void drawRainDrops(int x, int y, uint16_t color, bool heavy) {
  lcd.drawLine(x - 4, y + 6, x - 6, y + 13, color);
  lcd.drawLine(x + 4, y + 6, x + 2, y + 13, color);
  if (heavy) lcd.drawLine(x, y + 8, x - 2, y + 15, color);
}

void drawSnowFlakes(int x, int y, uint16_t color, bool heavy) {
  // left flake
  lcd.drawLine(x - 5, y + 7, x - 1, y + 11, color);
  lcd.drawLine(x - 1, y + 7, x - 5, y + 11, color);
  lcd.drawLine(x - 3, y + 6, x - 3, y + 12, color);
  lcd.drawLine(x - 6, y + 9, x, y + 9, color);

  // right flake
  lcd.drawLine(x + 3, y + 7, x + 7, y + 11, color);
  lcd.drawLine(x + 7, y + 7, x + 3, y + 11, color);
  lcd.drawLine(x + 5, y + 6, x + 5, y + 12, color);
  lcd.drawLine(x + 2, y + 9, x + 8, y + 9, color);

  if (heavy) {
    // center flake
    lcd.drawLine(x - 1, y + 10, x + 3, y + 14, color);
    lcd.drawLine(x + 3, y + 10, x - 1, y + 14, color);
    lcd.drawLine(x + 1, y + 9, x + 1, y + 15, color);
    lcd.drawLine(x - 2, y + 12, x + 4, y + 12, color);
  }
}

void drawMiniSnow(int x, int y, uint16_t color, bool heavy) {
  // left flake
  lcd.drawLine(x - 3, y + 4, x - 1, y + 6, color);
  lcd.drawLine(x - 1, y + 4, x - 3, y + 6, color);
  lcd.drawPixel(x - 2, y + 3, color);
  lcd.drawPixel(x - 2, y + 7, color);
  lcd.drawPixel(x - 4, y + 5, color);
  lcd.drawPixel(x,     y + 5, color);

  // right flake
  lcd.drawLine(x + 2, y + 4, x + 4, y + 6, color);
  lcd.drawLine(x + 4, y + 4, x + 2, y + 6, color);
  lcd.drawPixel(x + 3, y + 3, color);
  lcd.drawPixel(x + 3, y + 7, color);
  lcd.drawPixel(x + 1, y + 5, color);
  lcd.drawPixel(x + 5, y + 5, color);

  if (heavy) {
    // small center flake
    lcd.drawPixel(x, y + 6, color);
    lcd.drawPixel(x, y + 5, color);
    lcd.drawPixel(x, y + 7, color);
    lcd.drawPixel(x - 1, y + 6, color);
    lcd.drawPixel(x + 1, y + 6, color);
  }
}

void drawMiniSun(int x, int y, uint16_t color) {
  lcd.fillCircle(x, y, 4, color);
  for (int i = 0; i < 360; i += 45) {
    float r = i * DEG_TO_RAD;
    lcd.drawLine(x + cos(r) * 6, y + sin(r) * 6,
                 x + cos(r) * 8, y + sin(r) * 8, color);
  }
}

void drawMiniCloud(int x, int y, uint16_t color) {
  lcd.fillCircle(x - 4, y + 1, 4, color);
  lcd.fillCircle(x + 1, y - 2, 5, color);
  lcd.fillCircle(x + 6, y + 1, 4, color);
  lcd.fillRect(x - 4, y + 1, 11, 4, color);
}

void drawMiniRain(int x, int y, uint16_t color, bool heavy) {
  lcd.drawLine(x - 3, y + 4, x - 4, y + 8, color);
  lcd.drawLine(x + 2, y + 4, x + 1, y + 8, color);
  if (heavy) lcd.drawLine(x, y + 5, x - 1, y + 9, color);
}

void drawMiniWeatherIcon(int x, int y, int code) {
  uint16_t sunCol = TFT_YELLOW;
  uint16_t cloudCol = 0x9E7F;
  uint16_t rainCol = TFT_CYAN;
  uint16_t snowCol = TFT_WHITE;

  if (code == 0) {
    drawMiniSun(x, y, sunCol);
  }
  else if (code == 1) {
    drawMiniSun(x - 2, y - 1, sunCol);
    drawMiniCloud(x + 4, y + 2, cloudCol);
  }
  else if (code == 2) {
    drawMiniSun(x + 2, y - 3, sunCol);
    drawMiniCloud(x, y + 1, cloudCol);
  }
  else if (code == 3) {
    drawMiniCloud(x - 2, y, 0x7BEF);
    drawMiniCloud(x + 3, y + 2, cloudCol);
  }
  // Rain / drizzle / rain showers
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    drawMiniCloud(x, y - 1, cloudCol);
    drawMiniRain(x, y + 1, rainCol, (code == 65 || code == 82));
  }
  // Snow / snow grains / snow showers
  else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    drawMiniCloud(x, y - 1, cloudCol);
    drawMiniSnow(x, y + 1, snowCol, (code == 75 || code == 86));
  }
  else {
    drawMiniCloud(x, y, cloudCol);
  }
}

void drawWeatherIcon(int x, int y, int code) {
  uint16_t sunCol = TFT_YELLOW;
  uint16_t cloudCol = 0x9E7F;
  uint16_t rainCol = TFT_CYAN;
  uint16_t snowCol = TFT_WHITE;

  if (code == 0) {
    drawSun(x, y, sunCol);
  }
  else if (code == 1) {
    drawSun(x, y, sunCol);
    drawCloud(x + 10, y + 4, cloudCol, 0);
  }
  else if (code == 2) {
    drawSun(x + 6, y - 5, sunCol);
    drawCloud(x, y, cloudCol);
  }
  else if (code == 3) {
    drawCloud(x - 4, y - 2, 0x7BEF);
    drawCloud(x + 4, y + 2, cloudCol);
  }
  // Rain / drizzle / rain showers
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    drawCloud(x, y - 3, cloudCol);
    drawRainDrops(x, y, rainCol, (code == 65 || code == 82));
  }
  // Snow / snow grains / snow showers
  else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    drawCloud(x, y - 3, cloudCol);
    drawSnowFlakes(x, y, snowCol, (code == 75 || code == 86));
  }
  else {
    drawCloud(x, y, cloudCol);
  }
}

int pngDrawCanvas(PNGDRAW *pDraw) {
  uint16_t pix[256];
  png.getLineAsRGB565(pDraw, pix, PNG_RGB565_BIG_ENDIAN, 0);
  mapCanvas.pushImage(globalX, globalY + pDraw->y, pDraw->iWidth, 1, pix);
  return 1;
}

int pngDrawOverlayCanvas(PNGDRAW *pDraw) {
  uint16_t pix[256];
  png.getLineAsRGB565(pDraw, pix, PNG_RGB565_BIG_ENDIAN, 0);

  for (int x = 0; x < pDraw->iWidth; x++) {
    uint16_t c = pix[x];
    if (c == 0) continue;

    if (layerStyle == 1) {
      if (((globalX + x + globalY + pDraw->y) & 1) != 0) continue;
    }

    mapCanvas.drawPixel(globalX + x, globalY + pDraw->y, c);
  }
  return 1;
}

bool fetchPngToBuffer(const String& url, uint8_t** outBuf, size_t* outLen) {
  *outBuf = nullptr;
  *outLen = 0;

  HTTPClient http;
  http.setTimeout(15000);

  if (!http.begin(url)) {
    Serial.printf("HTTP begin failed: %s\n", url.c_str());
    return false;
  }

  // VAZHNO: bara nekopresiran odgovor
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("User-Agent", "ESP32-WeatherDisplay/1.0");
  http.useHTTP10(true);   // pomaga kaj nekoi tile serveri

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP GET failed: %d | %s\n", code, url.c_str());
    http.end();
    return false;
  }

  String ctype = http.header("Content-Type");
  String cenc  = http.header("Content-Encoding");
  int len = http.getSize();

  Serial.printf("HTTP 200 | type=%s | enc=%s | len=%d\n",
                ctype.c_str(), cenc.c_str(), len);

  WiFiClient* stream = http.getStreamPtr();

  // Ako nema content-length, citaj stream rachno
  if (len <= 0) {
    const size_t maxChunkedSize = 100000;
    uint8_t* buf = (uint8_t*)ps_malloc(maxChunkedSize);
    if (!buf) {
      Serial.println("PSRAM alloc failed (chunked)");
      http.end();
      return false;
    }

    size_t total = 0;
    unsigned long t0 = millis();

    while (http.connected() && (millis() - t0 < 15000)) {
      while (stream->available()) {
        if (total >= maxChunkedSize) {
          Serial.println("Chunked payload too large");
          free(buf);
          http.end();
          return false;
        }
        buf[total++] = stream->read();
        t0 = millis();
      }
      delay(1);
    }

    if (total < 16) {
      Serial.printf("Payload too small: %u bytes\n", (unsigned)total);
      free(buf);
      http.end();
      return false;
    }

    *outBuf = buf;
    *outLen = total;
    http.end();
    return true;
  }

  uint8_t* buf = (uint8_t*)ps_malloc(len);
  if (!buf) {
    Serial.printf("PSRAM alloc failed: %d bytes\n", len);
    http.end();
    return false;
  }

  int actuallyRead = stream->readBytes(buf, len);
  http.end();

  if (actuallyRead != len || len < 16) {
    Serial.printf("Read failed: expected %d got %d\n", len, actuallyRead);
    free(buf);
    return false;
  }

  *outBuf = buf;
  *outLen = len;
  return true;
}

void drawSignature() {
  if (appState != 0) return;

  int rssi = WiFi.RSSI();
  int rx = 35, ry = 4, rw = 80, rh = 22;
  lcd.fillRect(rx, ry, rw, rh, panelColor);
  lcd.drawRect(rx, ry, rw, rh, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_left);
  char rStr[15];
  sprintf(rStr, "%d dBm", rssi);
  lcd.drawString(rStr, rx + 5, ry + 11);

  int bars = 1;
  if (rssi > -50) bars = 4;
  else if (rssi > -60) bars = 3;
  else if (rssi > -70) bars = 2;

  for (int i = 0; i < 4; i++) {
    uint16_t bCol = (i < bars) ? TFT_GREEN : TFT_DARKGREY;
    lcd.fillRect(rx + 55 + (i * 5), ry + rh - 5 - (i * 3), 3, 3 + (i * 3), bCol);
  }

  int sx = rx + rw + 5;
  lcd.fillRect(sx, ry, 100, rh, panelColor);
  lcd.drawRect(sx, ry, 100, rh, TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.drawString("by mircemk", sx + 50, ry + 11);

  lcd.fillRect(35, 388, 80, 22, panelColor);
  lcd.drawRect(35, 388, 80, 22, TFT_WHITE);
  lcd.drawString("OHRID", 35 + 40, 388 + 11);
}

void setBrightnessFromTouchY(int ty) {
  int bx = 4, by = 4, bh = 409;

  if (ty < by) ty = by;
  if (ty > by + bh) ty = by + bh;

  // gore = poslabo, dolu = pojako
  brightnessLevel = map(ty, by, by + bh, 255, 20);

  if (brightnessLevel < 20) brightnessLevel = 20;
  if (brightnessLevel > 255) brightnessLevel = 255;

  lcd.setBrightness(brightnessLevel);
}

void drawProgressTimer() {
  if (appState != 0) return;

  int bx = 4, by = 4, bw = 24, bh = 409;

  lcd.drawRect(bx, by, bw, bh, TFT_WHITE);
  lcd.drawRect(bx + 2, by + 2, bw - 4, bh - 4, TFT_WHITE);
  lcd.fillRect(bx + 3, by + 3, bw - 6, bh - 6, TFT_BLACK);

  // Progress do sledezen update
  float p = (float)(millis() - lastUpdate) / 600000.0;
  if (p > 1.0) p = 1.0;
  int cH = (int)(397 * p);

  for (int y = 0; y < cH; y += 4) {
    lcd.drawFastHLine(bx + 7, (by + bh - 7) - y, bw - 14, TFT_SKYBLUE);
  }

  // Brightness marker
  int markerY = map(brightnessLevel, 255, 20, by, by + bh);
  lcd.drawFastHLine(bx + 4, markerY, bw - 8, TFT_YELLOW);
  lcd.drawFastHLine(bx + 4, markerY - 1, bw - 8, TFT_YELLOW);
}

void drawBottomDashboard() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  int midW = 180;
  int midH = 95;
  int midX = (800 - midW) / 2;
  int midY = 480 - midH;

  lcd.fillRect(midX, midY, midW, midH, panelColor);
  lcd.drawRect(midX, midY, midW, midH, TFT_WHITE);

  char clockStr[10];
  sprintf(clockStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(5);
  lcd.setTextDatum(top_center);
  lcd.drawString(clockStr, midX + (midW / 2), midY + 15);

  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextSize(2);
  lcd.setTextDatum(middle_center);
  char tempStr[15];
  sprintf(tempStr, "Temp: %d C", (int)currentTemp);
  lcd.drawString(tempStr, midX + (midW / 2), midY + 74);

  int sideH = 65;
  lcd.fillRect(0, 480 - sideH, midX, sideH, panelColor);
  lcd.drawRect(0, 480 - sideH, midX, sideH, TFT_DARKGREY);
  int partW = midX / 3;

  const char* lblL[] = {"Morning", "Noon", "Evening"};
  float tmpL[] = {morningTemp, noonTemp, eveningTemp};
  int codL[] = {morningCode, noonCode, eveningCode};

  for (int i = 0; i < 3; i++) {
    int cx = (partW * i) + (partW / 2);
    if (i > 0) lcd.drawFastVLine(partW * i, 480 - sideH, sideH, TFT_DARKGREY);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_LIGHTGREY);
    lcd.setTextDatum(top_center);
    lcd.drawString(lblL[i], cx, 480 - 57);
    drawWeatherIcon(partW * i + 25, 480 - 28, codL[i]);
    lcd.setTextColor(TFT_WHITE);
    lcd.setTextSize(3);
    lcd.setTextDatum(middle_left);
    lcd.drawNumber((int)tmpL[i], partW * i + 59, 480 - 28);
  }

  int rx = midX + midW;
  int rw = 800 - rx;
  lcd.fillRect(rx, 480 - sideH, rw, sideH, panelColor);
  lcd.drawRect(rx, 480 - sideH, rw, sideH, TFT_DARKGREY);
  int partRW = rw / 3;

  for (int i = 1; i < 4; i++) {
    int dayIdx = (timeinfo.tm_wday + i) % 7;
    int curX = rx + (partRW * (i - 1));
    int cx = curX + (partRW / 2);
    if (i > 1) lcd.drawFastVLine(curX, 480 - sideH, sideH, TFT_DARKGREY);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_LIGHTGREY);
    lcd.setTextDatum(top_center);
    lcd.drawString(days[dayIdx], cx, 480 - 57);
    drawWeatherIcon(curX + 25, 480 - 28, dCode[i]);
    lcd.setTextColor(TFT_WHITE);
    lcd.setTextSize(2);
    lcd.setTextDatum(top_left);
    lcd.drawNumber((int)dMax[i], curX + 59, 480 - 42);
    lcd.setTextColor(0x7BEF);
    lcd.drawNumber((int)dMin[i], curX + 59, 480 - 22);
  }
}

void getWeatherData() {
  HTTPClient http;
  http.begin(weatherUrl);

  if (http.GET() == 200) {
    DynamicJsonDocument doc(32768);
    deserializeJson(doc, http.getString());

    currentTemp = doc["current"]["temperature_2m"];
    morningTemp = doc["hourly"]["temperature_2m"][9];
    morningCode = doc["hourly"]["weather_code"][9];
    noonTemp = doc["hourly"]["temperature_2m"][14];
    noonCode = doc["hourly"]["weather_code"][14];
    eveningTemp = doc["hourly"]["temperature_2m"][21];
    eveningCode = doc["hourly"]["weather_code"][21];

    for (int i = 0; i < 16; i++) {
      dMax[i]   = doc["daily"]["temperature_2m_max"][i];
      dMin[i]   = doc["daily"]["temperature_2m_min"][i];
      dCode[i]  = doc["daily"]["weather_code"][i];
      dRain[i]  = doc["daily"]["precipitation_sum"][i];
      dPress[i] = doc["hourly"]["pressure_msl"][i * 24 + 12];
      dCloud[i] = doc["hourly"]["cloud_cover"][i * 24 + 12];

      dHum[i]   = doc["daily"]["relative_humidity_2m_mean"][i];
      dWind[i]  = doc["daily"]["wind_speed_10m_max"][i];
      dUV[i]    = doc["daily"]["uv_index_max"][i];
      dSolar[i] = doc["daily"]["shortwave_radiation_sum"][i];
    }
  }
  http.end();

http.begin("https://api.rainviewer.com/public/weather-maps.json");
if (http.GET() == 200) {
  DynamicJsonDocument rDoc(8192);
  deserializeJson(rDoc, http.getString());

  radarHost = rDoc["host"] | "https://tilecache.rainviewer.com";
  radarPath = "";

  JsonArray past = rDoc["radar"]["past"].as<JsonArray>();

  if (!past.isNull() && past.size() > 0) {
    radarTS = past[past.size() - 1]["time"] | 0;
    radarPath = (const char*)past[past.size() - 1]["path"];
    Serial.printf("RainViewer frame time=%ld\n", radarTS);
    Serial.printf("RainViewer path=%s\n", radarPath.c_str());
  } else {
    Serial.println("RainViewer: no past radar frames available");
  }
} else {
  Serial.println("RainViewer API request failed");
}
http.end();
}

int getDayOfMonthOffset(struct tm baseTime, int offsetDays) {
  time_t raw = mktime(&baseTime);
  raw += (offsetDays * 86400);
  struct tm *t2 = localtime(&raw);
  return t2->tm_mday;
}

void drawGraphPage(int type) {
  struct tm ti;
  getLocalTime(&ti);

  lcd.fillScreen(TFT_BLACK);
  lcd.drawRect(0, 0, 800, 480, TFT_WHITE);

  lcd.fillRect(10, 415, 110, 55, TFT_DARKGREY);
  lcd.drawRect(10, 415, 110, 55, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setTextDatum(middle_center);
  lcd.drawString("BACK", 65, 442);

  // Units label
const char* unitTxt = "";

if (type == 1) unitTxt = "Unit: C";
else if (type == 2) unitTxt = "Unit: hPa";
else if (type == 3) unitTxt = "Unit: mm";
else if (type == 4) unitTxt = "Unit: %";
else if (type == 5) unitTxt = "Unit: %";
else if (type == 6) unitTxt = "Unit: km/h";
else if (type == 7) unitTxt = "Unit: index";
else if (type == 8) unitTxt = "Unit: MJ/m2";

lcd.setTextDatum(middle_center);
lcd.setTextSize(3);
lcd.setTextColor(TFT_LIGHTGREY);
lcd.drawString(unitTxt, 370, 448 );

  int sX = 75, eX = 770, sY = 380, eY = 85, dW = 43;
  float minV, maxV;

  // Titles
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setTextDatum(top_center);

  if (type == 1) lcd.drawString("16-Day Temperature Forecast", 400, 8);
  else if (type == 2) lcd.drawString("16-Day Pressure Forecast", 400, 8);
  else if (type == 3) lcd.drawString("16-Day Rain Forecast", 400, 8);
  else if (type == 4) lcd.drawString("16-Day Cloud Cover Forecast", 400, 8);
  else if (type == 5) lcd.drawString("16-Day Humidity Forecast", 400, 8);
  else if (type == 6) lcd.drawString("16-Day Wind Speed Forecast", 400, 8);
  else if (type == 7) lcd.drawString("16-Day UV Index Forecast", 400, 8);
  else if (type == 8) lcd.drawString("16-Day Shortwave Radiation Forecast", 400, 8);

  // Range setup
  if (type == 1) {
    minV = 100;
    maxV = -100;
    for (int i = 0; i < 16; i++) {
      if (dMax[i] > maxV) maxV = dMax[i];
      if (dMin[i] < minV) minV = dMin[i];
    }
    minV = floor(minV / 5) * 5;
    maxV = ceil(maxV / 5) * 5;
  }
  else if (type == 2) {
    minV = 2000;
    maxV = 0;
    for (int i = 0; i < 16; i++) {
      if (dPress[i] > maxV) maxV = dPress[i];
      if (dPress[i] < minV) minV = dPress[i];
    }
    minV = floor(minV / 2) * 2 - 2;
    maxV = ceil(maxV / 2) * 2 + 2;
  }
  else if (type == 3) {
    minV = 0;
    maxV = 0;
    for (int i = 0; i < 16; i++) {
      if (dRain[i] > maxV) maxV = dRain[i];
    }
    if (maxV < 5) maxV = 5;
    else maxV = ceil(maxV / 5) * 5;
  }
  else if (type == 4) {
    minV = 0;
    maxV = 100;
  }
  else if (type == 5) {
    minV = 0;
    maxV = 100;
  }
  else if (type == 6) {
    minV = 0;
    maxV = 0;
    for (int i = 0; i < 16; i++) {
      if (dWind[i] > maxV) maxV = dWind[i];
    }
    maxV = ceil(maxV / 5) * 5;
    if (maxV < 10) maxV = 10;
  }
  else if (type == 7) {
    minV = 0;
    maxV = 0;
    for (int i = 0; i < 16; i++) {
      if (dUV[i] > maxV) maxV = dUV[i];
    }
    maxV = ceil(maxV);
    if (maxV < 5) maxV = 5;
  }
  else { // type == 8
    minV = 0;
    maxV = 0;
    for (int i = 0; i < 16; i++) {
      if (dSolar[i] > maxV) maxV = dSolar[i];
    }
    maxV = ceil(maxV / 5) * 5;
    if (maxV < 5) maxV = 5;
  }

  // Cloud shaded area only for C
  if (type == 4) {
    for (int i = 0; i < 15; i++) {
      int x1 = sX + (i * dW) + (dW / 2);
      float v1 = dCloud[i], v2 = dCloud[i + 1];
      for (int px = 0; px < dW; px++) {
        float interVal = v1 + (v2 - v1) * (float)px / dW;
        int interY = map(interVal, minV, maxV, sY, eY);
        lcd.drawFastVLine(x1 + px, interY, sY - interY, lcd.color565(35, 35, 35));
      }
    }
  }

  // Weekend backgrounds
  for (int i = 0; i < 16; i++) {
    int wD = (ti.tm_wday + i) % 7;
    if (wD == 6 || wD == 0) {
      uint16_t c = (wD == 6) ? lcd.color565(20, 20, 35) : lcd.color565(35, 20, 20);
      lcd.fillRect(sX + (i * dW), eY, dW, sY - eY, c);
    }
  }

  // Left scale
  lcd.setTextSize(2);
  lcd.setTextDatum(middle_right);

  float step = 5;
  if (type == 2) step = 2;
  else if (type == 3) step = maxV / 5;
  else if (type == 4) step = 20;
  else if (type == 5) step = 20;
  else if (type == 6) step = maxV / 5;
  else if (type == 7) step = 1;
  else if (type == 8) step = maxV / 5;

  if (step < 1) step = 1;

  for (float v = minV; v <= maxV; v += step) {
    int yP = map(v, minV, maxV, sY, eY);
    lcd.drawFastHLine(sX, yP, eX - sX, lcd.color565(60, 60, 60));
    lcd.drawNumber((int)v, sX - 12, yP);
  }

  // Main plot
  for (int i = 0; i < 16; i++) {
    int x1 = sX + (i * dW) + (dW / 2);
    int wD = (ti.tm_wday + i) % 7;
    int realDate = getDayOfMonthOffset(ti, i);

    // Day label
    lcd.setTextDatum(top_center);
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_LIGHTGREY);
    lcd.drawString(dayShort2[wD], x1, eY - 46);

    // Mini icon only on T
    if (type == 1) {
      drawMiniWeatherIcon(x1, eY - 17, dCode[i]);
    }

    // Bottom date
    lcd.setTextDatum(top_center);
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_WHITE);
    lcd.drawNumber(realDate, x1, sY + 8);

    // BAR charts: Rain + Solar
    if (type == 3 || type == 8) {
      float barVal = (type == 3) ? dRain[i] : dSolar[i];
      uint16_t barCol = (type == 3) ? TFT_BLUE : TFT_YELLOW;

      int barH = map(barVal, 0, maxV, 0, sY - eY);
      lcd.fillRect(x1 - 15, sY - barH, 30, barH, barCol);
      lcd.drawRect(x1 - 15, sY - barH, 30, barH, TFT_WHITE);

      if (barVal > 0) {
        lcd.setTextSize(2);
        lcd.setTextDatum(middle_center);
        lcd.setTextColor(TFT_WHITE);
        lcd.drawString(String(barVal, 1), x1, sY - barH - 16);
      }
    }
    else {
      float val1 = 0;
      uint16_t col = TFT_WHITE;

      if (type == 1) {
        val1 = dMax[i];
        col = TFT_RED;
      } else if (type == 2) {
        val1 = dPress[i];
        col = TFT_YELLOW;
      } else if (type == 4) {
        val1 = dCloud[i];
        col = TFT_WHITE;
      } else if (type == 5) {
        val1 = dHum[i];
        col = TFT_BLUE;
      } else if (type == 6) {
        val1 = dWind[i];
        col = lcd.color565(255, 140, 0);
      } else if (type == 7) {
        val1 = dUV[i];
        col = TFT_MAGENTA;
      }

      int y1 = map(val1, minV, maxV, sY, eY);

      if (i < 15) {
        float val2 = 0;

        if (type == 1) val2 = dMax[i + 1];
        else if (type == 2) val2 = dPress[i + 1];
        else if (type == 4) val2 = dCloud[i + 1];
        else if (type == 5) val2 = dHum[i + 1];
        else if (type == 6) val2 = dWind[i + 1];
        else if (type == 7) val2 = dUV[i + 1];

        int x2 = sX + ((i + 1) * dW) + (dW / 2);
        int y2 = map(val2, minV, maxV, sY, eY);
        lcd.drawLine(x1, y1, x2, y2, col);

        if (type == 1) {
          int yMin1 = map(dMin[i], minV, maxV, sY, eY);
          int yMin2 = map(dMin[i + 1], minV, maxV, sY, eY);
          lcd.drawLine(x1, yMin1, x2, yMin2, TFT_BLUE);
        }
      }

      lcd.fillCircle(x1, y1, 4, col);
      lcd.drawCircle(x1, y1, 4, TFT_WHITE);

      if (type == 1) {
        int yMin = map(dMin[i], minV, maxV, sY, eY);
        lcd.fillCircle(x1, yMin, 4, TFT_BLUE);
        lcd.drawCircle(x1, yMin, 4, TFT_WHITE);
      }
    }
  }
}

void drawLocationMarker() {
  double n = pow(2.0, myZoom);

  // Global pixel coordinates at current zoom
  double worldX = ((myLon + 180.0) / 360.0) * n * 256.0;
  double latRad = myLat * M_PI / 180.0;
  double worldY = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) / 2.0 * n * 256.0;

  // Center tile indices (isti kako vo renderRadarMap)
  int cTX = (int)(floor((myLon + 180.0) / 360.0 * n));
  int cTY = (int)(floor((1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) / 2.0 * n));

  // Top-left pixel of our composed 3x2 tile map
  double topLeftWorldX = (cTX - 1) * 256.0;
  double topLeftWorldY = (cTY + 0) * 256.0;

  // Convert to local sprite coordinates
  int px = (int)(worldX - topLeftWorldX) + 32;
  int py = (int)(worldY - topLeftWorldY) - 16;

  // Safety check
  if (px < 0 || px >= 800 || py < 0 || py >= 415) return;

  // Marker
  mapCanvas.fillCircle(px, py, 5, TFT_RED);
  mapCanvas.drawCircle(px, py, 6, TFT_WHITE);
  mapCanvas.drawPixel(px, py, TFT_WHITE);
}

void renderRadarMap() {
  if (appState == 0) {
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(3);
    lcd.setTextDatum(middle_center);
    lcd.drawString("Loading map...", 400, 200);
  }

  mapCanvas.fillSprite(TFT_BLACK);

  int cTX = (int)(floor((myLon + 180.0) / 360.0 * pow(2.0, myZoom)));
  int cTY = (int)(floor((1.0 - log(tan(myLat * M_PI / 180.0) + 1.0 / cos(myLat * M_PI / 180.0)) / M_PI) / 2.0 * pow(2.0, myZoom)));

  for (int i = -1; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      globalX = 32 + ((i + 1) * 256);
      globalY = (j * 256) - 16;

      String mU = String(mapUrls[mapStyle]) + String(myZoom) + "/" + String(cTX + i) + "/" + String(cTY + j) + ".png";

      uint8_t* baseBuf = nullptr;
      size_t baseLen = 0;

      if (fetchPngToBuffer(mU, &baseBuf, &baseLen)) {
        if (png.openRAM(baseBuf, baseLen, pngDrawCanvas) == PNG_SUCCESS) {
          png.decode(NULL, 0);
          png.close();
        } else {
          Serial.printf("Base PNG open failed: %s\n", mU.c_str());
        }
        free(baseBuf);
      } else {
        Serial.printf("Base fetch failed: %s\n", mU.c_str());
      }

      String layerUrl = "";

if (layerStyle == 0) {
  if (radarPath.length() > 0) {
    layerUrl = radarHost + radarPath + "/256/" +
               String(myZoom) + "/" +
               String(cTX + i) + "/" +
               String(cTY + j) + "/1/1_1.png";
  }
} else {
        layerUrl = "https://tile.openweathermap.org/map/" +
                   String(owmLayerIds[layerStyle]) + "/" +
                   String(myZoom) + "/" +
                   String(cTX + i) + "/" +
                   String(cTY + j) +
                   ".png?appid=" + String(owmApiKey);
      }

 if (layerUrl.length() > 0) {
  uint8_t* overlayBuf = nullptr;
  size_t overlayLen = 0;

  Serial.printf("Layer=%s\n", layerNames[layerStyle]);
  Serial.printf("URL=%s\n", layerUrl.c_str());
  Serial.printf("Heap=%u PSRAM=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  if (fetchPngToBuffer(layerUrl, &overlayBuf, &overlayLen)) {

    // PNG signature check
    bool isPng =
      overlayLen >= 8 &&
      overlayBuf[0] == 0x89 &&
      overlayBuf[1] == 0x50 &&
      overlayBuf[2] == 0x4E &&
      overlayBuf[3] == 0x47 &&
      overlayBuf[4] == 0x0D &&
      overlayBuf[5] == 0x0A &&
      overlayBuf[6] == 0x1A &&
      overlayBuf[7] == 0x0A;

    if (!isPng) {
      Serial.printf("Overlay is NOT PNG! len=%u\n", (unsigned)overlayLen);
      Serial.print("First 32 bytes HEX: ");
      for (size_t k = 0; k < 32 && k < overlayLen; k++) {
        Serial.printf("%02X ", overlayBuf[k]);
      }
      Serial.println();

      Serial.print("First 120 chars TXT: ");
      for (size_t k = 0; k < 120 && k < overlayLen; k++) {
        char c = (char)overlayBuf[k];
        if (c >= 32 && c <= 126) Serial.print(c);
        else Serial.print('.');
      }
      Serial.println();

      free(overlayBuf);
    } else {
      int rc = png.openRAM(overlayBuf, overlayLen, pngDrawOverlayCanvas);
      if (rc == PNG_SUCCESS) {
        int decRc = png.decode(NULL, 0);
        if (decRc != PNG_SUCCESS) {
          Serial.printf("PNG decode failed rc=%d | %s\n", decRc, layerUrl.c_str());
        }
        png.close();
      } else {
        Serial.printf("Overlay PNG open failed rc=%d | %s\n", rc, layerUrl.c_str());
      }
      free(overlayBuf);
    }

  } else {
    Serial.printf("Overlay fetch failed: %s\n", layerUrl.c_str());
  }
}

      delay(10);
    }
  }

    drawLocationMarker();
    mapCanvas.pushSprite(0, 0);

  int mapX = 740;
  int mapY = 387;
  int mapW = 60;
  int mapH = 22;

  lcd.fillRect(mapX, mapY, mapW, mapH, panelColor);
  lcd.drawRect(mapX, mapY, mapW, mapH, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_center);
  lcd.drawString(mapNames[mapStyle], mapX + (mapW / 2), mapY + (mapH / 2));

  int layerX = 360;
  int layerY = 10;
  int layerW = 70;
  int layerH = 22;

  lcd.fillRect(layerX, layerY, layerW, layerH, panelColor);
  lcd.drawRect(layerX, layerY, layerW, layerH, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_center);
  lcd.drawString(layerNames[layerStyle], layerX + (layerW / 2), layerY + (layerH / 2));
}

void drawTopDate() {
  if (appState != 0) return;
  struct tm ti;
  getLocalTime(&ti);

  char buf[40];
  sprintf(buf, "%s, %d. %s", days[ti.tm_wday], ti.tm_mday, months[ti.tm_mon]);

  lcd.fillRect(520, 0, 280, 35, panelColor);
  lcd.drawRect(520, 0, 280, 35, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setTextDatum(middle_center);
  lcd.drawString(buf, 660, 17);
}

void drawSideButtons() {
  if (appState != 0) return;

  int bw = 34, bh = 75;

  // RIGHT SIDE: T P R C
  int bxR = 765;
  const char* lblR[] = {"T", "P", "R", "C"};
  uint16_t colsR[] = {TFT_RED, TFT_GREEN, TFT_CYAN, TFT_WHITE};

  for (int i = 0; i < 4; i++) {
    lcd.fillRect(bxR, 50 + (i * 85), bw, bh, panelColor);
    lcd.drawRect(bxR, 50 + (i * 85), bw, bh, TFT_WHITE);
    lcd.setTextColor(colsR[i]);
    lcd.setTextSize(3);
    lcd.setTextDatum(middle_center);
    lcd.drawString(lblR[i], bxR + 17, 50 + (i * 85) + 37);
  }

  // LEFT SIDE: H W U S
  int bxL = 32;
  const char* lblL[] = {"H", "W", "U", "S"};
  uint16_t colsL[] = {
    TFT_BLUE,
    lcd.color565(255, 140, 0),
    TFT_MAGENTA,
    TFT_YELLOW
  };

  for (int i = 0; i < 4; i++) {
    lcd.fillRect(bxL, 50 + (i * 85), bw, bh, panelColor);
    lcd.drawRect(bxL, 50 + (i * 85), bw, bh, TFT_WHITE);
    lcd.setTextColor(colsL[i]);
    lcd.setTextSize(3);
    lcd.setTextDatum(middle_center);
    lcd.drawString(lblL[i], bxL + 17, 50 + (i * 85) + 37);
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(19, 20);
  Wire.beginTransmission(0x18);
  Wire.write(0x01);
  Wire.write(0x3B);
  Wire.endTransmission();

  touch_init();
  lcd.init();
  lcd.setBrightness(brightnessLevel);

  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(3);
  lcd.setTextDatum(middle_center);
  lcd.drawString("Connecting to internet...", 400, 240);

  WiFi.begin(ssid, password);
  unsigned long sw = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - sw < 15000) delay(500);

  lcd.fillScreen(TFT_BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    lcd.setTextColor(TFT_GREEN);
    lcd.drawString("Connected!", 400, 240);
    configTime(3600, 0, ntpServer);
  } else {
    lcd.setTextColor(TFT_RED);
    lcd.drawString("No Internet!", 400, 240);
  }

  delay(1500);
  lcd.fillScreen(TFT_BLACK);

  mapCanvas.setPsram(true);
  if (!mapCanvas.createSprite(800, 415)) Serial.println("PSRAM Sprite failed!");

  lastUpdate = millis();
  getWeatherData();
  renderRadarMap();
  drawTopDate();
  drawBottomDashboard();
  drawSideButtons();
  drawSignature();
}

void loop() {
  static unsigned long lT = 0, lTouch = 0;
  struct tm ti;

  if (appState == 0 && getLocalTime(&ti)) {
    if (ti.tm_min != lastMinute) {
      lastMinute = ti.tm_min;
      drawBottomDashboard();
      drawSignature();
    }
  }

  if (appState == 0 && millis() - lT > 5000) {
    drawProgressTimer();
    lT = millis();
  }

 if (touch_has_signal() && touch_touched()) {
  int tx = touch_last_x, ty = touch_last_y;

  // Brightness slider on left bar
  if (appState == 0 && tx >= 0 && tx <= 32 && ty >= 4 && ty <= 413) {
    setBrightnessFromTouchY(ty);
    drawProgressTimer();
    delay(25);
    return;
  }

    if (millis() - lTouch > 600) {
      if (appState == 0) {
        if (ty > 0 && ty < 40 && tx > 300 && tx < 500) {
          layerStyle = (layerStyle + 1) % 3;

          lcd.fillRect(250, 200, 300, 80, TFT_BLACK);
          lcd.drawRect(250, 200, 300, 80, TFT_WHITE);
          lcd.setTextColor(TFT_YELLOW);
          lcd.setTextSize(3);
          lcd.setTextDatum(middle_center);
          lcd.drawString("Layer: " + String(layerNames[layerStyle]), 400, 240);
          delay(800);

          renderRadarMap();
          drawTopDate();
          drawBottomDashboard();
          drawSideButtons();
          drawSignature();
          drawProgressTimer();
        }
        else if (tx > 310 && tx < 490 && ty > 385 && ty < 480) {
          mapStyle = (mapStyle + 1) % 3;

          lcd.fillRect(250, 200, 300, 80, TFT_BLACK);
          lcd.drawRect(250, 200, 300, 80, TFT_WHITE);
          lcd.setTextColor(TFT_YELLOW);
          lcd.setTextSize(3);
          lcd.setTextDatum(middle_center);
          lcd.drawString("Map: " + String(mapNames[mapStyle]), 400, 240);
          delay(800);

          renderRadarMap();
          drawTopDate();
          drawBottomDashboard();
          drawSideButtons();
          drawSignature();
        }
else if (tx > 760) {
  if (ty > 50 && ty < 125) appState = 1, drawGraphPage(1);
  else if (ty > 135 && ty < 210) appState = 2, drawGraphPage(2);
  else if (ty > 220 && ty < 295) appState = 3, drawGraphPage(3);
  else if (ty > 305 && ty < 380) appState = 4, drawGraphPage(4);
}
else if (tx > 32 && tx < 66) {
  if (ty > 50 && ty < 125) appState = 5, drawGraphPage(5);
  else if (ty > 135 && ty < 210) appState = 6, drawGraphPage(6);
  else if (ty > 220 && ty < 295) appState = 7, drawGraphPage(7);
  else if (ty > 305 && ty < 380) appState = 8, drawGraphPage(8);
}
        else if (tx > 250 && tx < 550 && ty > 100 && ty < 380) {
          myZoom++;
          if (myZoom > 7) myZoom = 5;

          lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
          lcd.setTextSize(6);
          lcd.setTextDatum(middle_center);
          lcd.drawString("ZOOM: " + String(myZoom), 400, 240);
          delay(800);

          renderRadarMap();
          drawTopDate();
          drawBottomDashboard();
          drawSideButtons();
          drawSignature();
        }
      } else if (tx < 150 && ty > 400) {
        appState = 0;
        lcd.fillScreen(TFT_BLACK);

        if (mapCanvas.getBuffer() != nullptr) {
          mapCanvas.pushSprite(0, 0);
        } else {
          renderRadarMap();
        }

        drawBottomDashboard();
        drawSideButtons();
        drawSignature();
        drawTopDate();

        int mapX = 740;
        int mapY = 387;
        int mapW = 60;
        int mapH = 22;

        lcd.fillRect(mapX, mapY, mapW, mapH, panelColor);
        lcd.drawRect(mapX, mapY, mapW, mapH, TFT_WHITE);
        lcd.setTextColor(TFT_WHITE);
        lcd.setTextSize(1);
        lcd.setTextDatum(middle_center);
        lcd.drawString(mapNames[mapStyle], mapX + (mapW / 2), mapY + (mapH / 2));

        int layerX = 360;
        int layerY = 10;
        int layerW = 70;
        int layerH = 22;

        lcd.fillRect(layerX, layerY, layerW, layerH, panelColor);
        lcd.drawRect(layerX, layerY, layerW, layerH, TFT_WHITE);
        lcd.setTextColor(TFT_WHITE);
        lcd.setTextSize(1);
        lcd.setTextDatum(middle_center);
        lcd.drawString(layerNames[layerStyle], layerX + (layerW / 2), layerY + (layerH / 2));
      }

      lTouch = millis();
    }
  }

  if (appState == 0 && millis() - lastUpdate > 600000) {
    lastUpdate = millis();
    getWeatherData();
    renderRadarMap();
    drawTopDate();
    drawBottomDashboard();
    drawSideButtons();
    drawSignature();
  }
}