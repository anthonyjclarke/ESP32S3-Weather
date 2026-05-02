// By mircemk, April 2026

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "time.h"
#include <Wire.h>
#include <Preferences.h>
#include "config.h"
#include "debug.h"
#include "display_hw.h"
#include "secrets.h"
#include "touch.h"
#include "waveshare_display.h"

#ifndef SECRET_OTA_PASSWORD
#define SECRET_OTA_PASSWORD "change-me"
#endif

const char* ntpServer = "pool.ntp.org";

const char* owmApiKey = SECRET_OWM_API_KEY;

int myZoom = cfg::kMapZoom;
double myLat = cfg::kLocationLatitude;
double myLon = cfg::kLocationLongitude;
unsigned long lastUpdate = 0;
long radarTS = 0;
int appState = 0;
int lastMinute = -1;
int brightnessLevel = 100;
int mapStyle = cfg::kDefaultMapStyle;   // 0 = dark_all, 1 = opentopomap, 2 = openstreetmap
int layerStyle = 0; // 0 = Radar, 1 = Clouds, 2 = Rain
bool owmAuthFailed = false;
bool otaInProgress = false;

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
int overlayAlphaPercent[] = {
  cfg::kRadarOverlayAlphaPercent,
  cfg::kCloudOverlayAlphaPercent,
  cfg::kRainOverlayAlphaPercent
};

WaveshareDisplay gfx;
WaveshareDisplay& lcd = gfx;
WebServer webServer(80);
LGFX_Sprite renderScratch(&lcd);
LGFX_Sprite cacheRadar(&lcd);
LGFX_Sprite cacheClouds(&lcd);
LGFX_Sprite cacheRain(&lcd);
LGFX_Sprite* layerCacheSprites[] = {&cacheRadar, &cacheClouds, &cacheRain};
LGFX_Sprite* mapFront     = &cacheRadar;
LGFX_Sprite* renderTarget = &renderScratch;

struct LayerCacheState {
  bool valid;
  int zoom;
  int mapStyle;
  unsigned long updatedMs;
};

LayerCacheState layerCaches[3] = {};

enum RenderState : uint8_t { RENDER_IDLE, RENDER_BUSY, RENDER_READY };
volatile RenderState renderState = RENDER_IDLE;
volatile bool renderPending        = false;
volatile bool weatherRefreshPending = false;
bool          firstRenderDone = false;
unsigned long layerCycleLastMs = 0;
unsigned long realtimeRefreshLastMs = 0;
TaskHandle_t  renderTaskHandle = nullptr;
int renderLayerStyle = 0;
int renderMapStyle = 0;
int renderZoom = 0;
int pendingLayerStyle = 0;
bool pendingRenderForce = false;
volatile int  renderTilesDone  = 0;
volatile int  renderTilesTotal = 0;
volatile int  renderDiagTileIndex = 0;
volatile int  renderDiagTileX = 0;
volatile int  renderDiagTileY = 0;
volatile unsigned long renderDiagPhaseStartedMs = 0;
volatile unsigned long renderDiagLastProgressMs = 0;
volatile uint32_t screenFrameVersion = 0;
char renderDiagPhase[24] = "idle";
char renderDiagUrl[180] = "";
bool webUiStarted = false;
unsigned long lastUiTouchMs = 0;

// --- Night sleep schedule ---
Preferences prefs;
bool     sleepScheduleEnabled  = cfg::kSleepScheduleEnabled;
int      sleepOnHour           = cfg::kSleepOnHour;
int      sleepOnMinute         = cfg::kSleepOnMinute;
int      sleepOffHour          = cfg::kSleepOffHour;
int      sleepOffMinute        = cfg::kSleepOffMinute;
int      sleepWakeDurationSecs = cfg::kSleepWakeDurationSecs;

enum SleepPhase : uint8_t {
  SLEEP_AWAKE,        // normal operation
  SLEEP_PENDING,      // showing sleep message, BL off in 2 s
  SLEEP_DARK,         // backlight off
  SLEEP_TOUCH_WOKEN   // touch-woken during window, re-sleeps after timeout
};
SleepPhase   sleepPhase   = SLEEP_AWAKE;
unsigned long sleepPhaseMs = 0;

constexpr int kTileSize = 256;
constexpr int kMapCanvasHeight = 415;

float currentTemp, morningTemp, noonTemp, eveningTemp;
int morningCode, noonCode, eveningCode;
float dMax[16], dMin[16], dRain[16], dPress[16], dCloud[16];
float dHum[16], dWind[16], dUV[16], dSolar[16];
int dCode[16];

void setBrightnessFromTouchY(int ty);
void getWeatherData();
void renderRadarMap();
void triggerRenderForLayer(int targetLayer, bool forceRefresh = false);
void drawBottomDashboard();
void drawSideButtons();
void drawSignature();
void drawTopDate();
void drawMapBadges();
void drawProgressTimer();
void drawGraphPage(int type);
void drawWeatherIcon(int x, int y, int code);
void drawMiniWeatherIcon(int x, int y, int code);
int getDayOfMonthOffset(struct tm baseTime, int offsetDays);
bool fetchPngToBuffer(const String& url, uint8_t** outBuf, size_t* outLen, int* httpStatus = nullptr);
void updateLoadingProgress();
void updateRenderStatusOverlay(bool force = false);
void markScreenUpdated();
void setupWebUi(bool wifiOk);
void handleWebUiClient();
bool handleUiTouch(int tx, int ty, bool debounce);
void loadSleepSettings();
void saveSleepSettings();
bool isInSleepWindow();
void drawSleepScreen();
void exitSleepRestoreDashboard();
void pollSleepSchedule();

PNG png;
int globalX, globalY;
uint16_t panelColor = 0x0841;
const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* dayShort2[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
const char* months[] = {"Jan.", "Feb.", "March", "April", "May", "June", "July", "Aug.", "Sept.", "Oct.", "Nov.", "Dec."};

struct SpiRamAllocator {
  void* allocate(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }

  void deallocate(void* pointer) {
    heap_caps_free(pointer);
  }

  void* reallocate(void* ptr, size_t newSize) {
    return heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;

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
  lcd.drawLine(x - 5, y + 7, x - 1, y + 11, color);
  lcd.drawLine(x - 1, y + 7, x - 5, y + 11, color);
  lcd.drawLine(x - 3, y + 6, x - 3, y + 12, color);
  lcd.drawLine(x - 6, y + 9, x, y + 9, color);

  lcd.drawLine(x + 3, y + 7, x + 7, y + 11, color);
  lcd.drawLine(x + 7, y + 7, x + 3, y + 11, color);
  lcd.drawLine(x + 5, y + 6, x + 5, y + 12, color);
  lcd.drawLine(x + 2, y + 9, x + 8, y + 9, color);

  if (heavy) {
    lcd.drawLine(x - 1, y + 10, x + 3, y + 14, color);
    lcd.drawLine(x + 3, y + 10, x - 1, y + 14, color);
    lcd.drawLine(x + 1, y + 9, x + 1, y + 15, color);
    lcd.drawLine(x - 2, y + 12, x + 4, y + 12, color);
  }
}

void drawMiniSnow(int x, int y, uint16_t color, bool heavy) {
  lcd.drawLine(x - 3, y + 4, x - 1, y + 6, color);
  lcd.drawLine(x - 1, y + 4, x - 3, y + 6, color);
  lcd.drawPixel(x - 2, y + 3, color);
  lcd.drawPixel(x - 2, y + 7, color);
  lcd.drawPixel(x - 4, y + 5, color);
  lcd.drawPixel(x,     y + 5, color);

  lcd.drawLine(x + 2, y + 4, x + 4, y + 6, color);
  lcd.drawLine(x + 4, y + 4, x + 2, y + 6, color);
  lcd.drawPixel(x + 3, y + 3, color);
  lcd.drawPixel(x + 3, y + 7, color);
  lcd.drawPixel(x + 1, y + 5, color);
  lcd.drawPixel(x + 5, y + 5, color);

  if (heavy) {
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

uint8_t clampColorChannel(int value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return (uint8_t)value;
}

uint16_t enhanceBaseMapPixel(uint16_t bigEndianRgb565) {
  uint16_t rgb565 = __builtin_bswap16(bigEndianRgb565);

  int r = (((rgb565 >> 11) & 0x1F) * 255 + 15) / 31;
  int g = (((rgb565 >> 5) & 0x3F) * 255 + 31) / 63;
  int b = ((rgb565 & 0x1F) * 255 + 15) / 31;

  r = ((r - 128) * cfg::kBaseMapContrastPercent) / 100 + 128 + cfg::kBaseMapBrightness;
  g = ((g - 128) * cfg::kBaseMapContrastPercent) / 100 + 128 + cfg::kBaseMapBrightness;
  b = ((b - 128) * cfg::kBaseMapContrastPercent) / 100 + 128 + cfg::kBaseMapBrightness;

  r = clampColorChannel(r);
  g = clampColorChannel(g);
  b = clampColorChannel(b);

  rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return __builtin_bswap16(rgb565);
}

uint16_t blendRgb565(uint16_t src, uint16_t dst, uint8_t alpha) {
  if (alpha == 0) return dst;
  if (alpha >= 255) return src;

  int sr = (src >> 11) & 0x1F;
  int sg = (src >> 5) & 0x3F;
  int sb = src & 0x1F;
  int dr = (dst >> 11) & 0x1F;
  int dg = (dst >> 5) & 0x3F;
  int db = dst & 0x1F;

  int r = (sr * alpha + dr * (255 - alpha)) / 255;
  int g = (sg * alpha + dg * (255 - alpha)) / 255;
  int b = (sb * alpha + db * (255 - alpha)) / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

int pngDrawCanvas(PNGDRAW *pDraw) {
  uint16_t pix[256];
  png.getLineAsRGB565(pDraw, pix, PNG_RGB565_BIG_ENDIAN, 0);
  for (int x = 0; x < pDraw->iWidth; x++) {
    pix[x] = enhanceBaseMapPixel(pix[x]);
  }
  renderTarget->pushImage(globalX, globalY + pDraw->y, pDraw->iWidth, 1, pix);
  return 1;
}

int pngDrawOverlayCanvas(PNGDRAW *pDraw) {
  uint16_t pix[256];
  png.getLineAsRGB565(pDraw, pix, PNG_RGB565_LITTLE_ENDIAN, 0);
  int alphaPercent = 100;
  if (renderLayerStyle >= 0 && renderLayerStyle < 3) {
    alphaPercent = constrain(overlayAlphaPercent[renderLayerStyle], 0, 100);
  }

  for (int x = 0; x < pDraw->iWidth; x++) {
    uint16_t c = pix[x];
    if (c == 0) continue;

    int sx = globalX + x;
    int sy = globalY + pDraw->y;
    if (sx < 0 || sx >= cfg::kScreenWidth || sy < 0 || sy >= kMapCanvasHeight) continue;
    if (alphaPercent <= 0) continue;

    if (alphaPercent < 100) {
      uint8_t alpha = (uint8_t)constrain((alphaPercent * 255) / 100, 0, 255);
      uint16_t base = renderTarget->readPixel(sx, sy);
      c = blendRgb565(c, base, alpha);
    }

    renderTarget->drawPixel(sx, sy, c);
  }
  return 1;
}

size_t readHttpPayload(WiFiClient* stream, uint8_t* buf, size_t expectedLen, uint32_t timeoutMs) {
  size_t total = 0;
  unsigned long lastProgress = millis();

  while (total < expectedLen && (millis() - lastProgress < timeoutMs)) {
    int available = stream->available();
    if (available <= 0) {
      delay(1);
      continue;
    }

    size_t remaining = expectedLen - total;
    size_t toRead = min((size_t)available, remaining);
    int n = stream->read(buf + total, toRead);

    if (n > 0) {
      total += (size_t)n;
      lastProgress = millis();
    } else {
      delay(1);
    }
  }

  return total;
}

String redactUrlForLog(const String& url) {
  int keyStart = url.indexOf("appid=");
  if (keyStart < 0) return url;

  int valueStart = keyStart + 6;
  int valueEnd = url.indexOf('&', valueStart);
  String redacted = url.substring(0, valueStart) + "<redacted>";
  if (valueEnd >= 0) redacted += url.substring(valueEnd);
  return redacted;
}

uint32_t renderStackHighWater() {
  return renderTaskHandle ? (uint32_t)uxTaskGetStackHighWaterMark(renderTaskHandle) : 0;
}

uint32_t largestInternalBlock() {
  return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
}

uint32_t largestPsramBlock() {
  return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void setRenderDiagPhase(const char* phase) {
  strlcpy(renderDiagPhase, phase, sizeof(renderDiagPhase));
  renderDiagPhaseStartedMs = millis();
}

void setRenderDiagContext(const char* phase, int tileIndex, int tileX, int tileY, const String& url) {
  renderDiagTileIndex = tileIndex;
  renderDiagTileX = tileX;
  renderDiagTileY = tileY;
  String safeUrl = redactUrlForLog(url);
  strlcpy(renderDiagUrl, safeUrl.c_str(), sizeof(renderDiagUrl));
  renderDiagLastProgressMs = millis();
  setRenderDiagPhase(phase);
}

void clearRenderDiag() {
  renderDiagTileIndex = renderTilesDone;
  renderDiagTileX = 0;
  renderDiagTileY = 0;
  renderDiagUrl[0] = '\0';
  renderDiagLastProgressMs = millis();
  setRenderDiagPhase("idle");
}

bool layerCacheMatches(int targetLayer) {
  if (targetLayer < 0 || targetLayer > 2) return false;
  LayerCacheState& cache = layerCaches[targetLayer];
  return cache.valid && cache.zoom == myZoom && cache.mapStyle == mapStyle;
}

bool layerCacheFresh(int targetLayer) {
  if (!layerCacheMatches(targetLayer)) return false;
  unsigned long ageMs = millis() - layerCaches[targetLayer].updatedMs;
  return ageMs < (unsigned long)cfg::kRealtimeRefreshSecs * 1000UL;
}

void invalidateLayerCaches() {
  for (int i = 0; i < 3; i++) {
    layerCaches[i].valid = false;
  }
}

void markLayerCacheUpdated(int targetLayer) {
  if (targetLayer < 0 || targetLayer > 2) return;
  layerCaches[targetLayer].valid = true;
  layerCaches[targetLayer].zoom = renderZoom;
  layerCaches[targetLayer].mapStyle = renderMapStyle;
  layerCaches[targetLayer].updatedMs = millis();
}

unsigned long layerCacheAgeSecs(int targetLayer) {
  if (targetLayer < 0 || targetLayer > 2 || !layerCaches[targetLayer].valid) return 0;
  return (millis() - layerCaches[targetLayer].updatedMs) / 1000UL;
}

bool layerRenderInProgress(int targetLayer) {
  return renderState == RENDER_BUSY && renderLayerStyle == targetLayer;
}

void formatLayerAgeLabel(int targetLayer, char* out, size_t outLen) {
  if (outLen == 0) return;

  if (targetLayer < 0 || targetLayer > 2) {
    strlcpy(out, "--", outLen);
    return;
  }

  if (layerCacheMatches(targetLayer)) {
    unsigned long ageSecs = layerCacheAgeSecs(targetLayer);
    if (ageSecs < 60) {
      strlcpy(out, "fresh", outLen);
    } else if (ageSecs < 3600) {
      snprintf(out, outLen, "%lum old", ageSecs / 60UL);
    } else if (ageSecs < 86400) {
      snprintf(out, outLen, "%luh %lum", ageSecs / 3600UL, (ageSecs % 3600UL) / 60UL);
    } else {
      snprintf(out, outLen, "%lud old", ageSecs / 86400UL);
    }
    return;
  }

  if (layerRenderInProgress(targetLayer)) {
    strlcpy(out, "updating", outLen);
    return;
  }

  if (renderPending && pendingLayerStyle == targetLayer) {
    strlcpy(out, "queued", outLen);
    return;
  }

  strlcpy(out, "no cache", outLen);
}

uint16_t layerAgeColor(int targetLayer) {
  if (targetLayer < 0 || targetLayer > 2) return TFT_DARKGREY;
  if (layerCacheMatches(targetLayer)) {
    unsigned long ageSecs = layerCacheAgeSecs(targetLayer);
    unsigned long refreshSecs = (unsigned long)cfg::kRealtimeRefreshSecs;
    if (ageSecs >= refreshSecs) return TFT_RED;
    if (ageSecs >= (refreshSecs * 3UL) / 4UL) return TFT_YELLOW;
    if (ageSecs < 60) return TFT_GREEN;
    return TFT_LIGHTGREY;
  }

  if (layerRenderInProgress(targetLayer) || (renderPending && pendingLayerStyle == targetLayer)) return TFT_YELLOW;
  return TFT_DARKGREY;
}

void markScreenUpdated() {
  screenFrameVersion = screenFrameVersion + 1;
}

const char* renderStateName() {
  switch (renderState) {
    case RENDER_IDLE:  return "idle";
    case RENDER_BUSY:  return "busy";
    case RENDER_READY: return "ready";
  }
  return "unknown";
}

String layerCacheStatusName(int layer) {
  if (layerRenderInProgress(layer)) return "updating";
  if (renderPending && pendingLayerStyle == layer) return "queued";
  if (!layerCaches[layer].valid) return "missing";
  if (!layerCacheMatches(layer)) return "mismatch";
  if (!layerCacheFresh(layer)) return "stale";
  return "fresh";
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

String buildStatusJson() {
  String json;
  json.reserve(2700);
  json += "{";
  json += "\"frameVersion\":" + String((uint32_t)screenFrameVersion);
  json += ",\"uptimeMs\":" + String(millis());
  json += ",\"appState\":" + String(appState);
  json += ",\"layer\":\"" + String(layerNames[layerStyle]) + "\"";
  json += ",\"map\":\"" + String(mapNames[mapStyle]) + "\"";
  json += ",\"zoom\":" + String(myZoom);
  json += ",\"renderState\":\"" + String(renderStateName()) + "\"";
  json += ",\"renderLayer\":\"" + String(layerNames[renderLayerStyle]) + "\"";
  json += ",\"renderTilesDone\":" + String((int)renderTilesDone);
  json += ",\"renderTilesTotal\":" + String((int)renderTilesTotal);
  json += ",\"lastUpdateAgeSec\":" + String((millis() - lastUpdate) / 1000UL);
  json += ",\"refreshSec\":" + String(cfg::kRealtimeRefreshSecs);
  json += ",\"brightness\":" + String(brightnessLevel);
  json += ",\"overlayAlpha\":{";
  json += "\"radar\":" + String(overlayAlphaPercent[0]);
  json += ",\"clouds\":" + String(overlayAlphaPercent[1]);
  json += ",\"rain\":" + String(overlayAlphaPercent[2]);
  json += "}";
  json += ",\"hardware\":{";
  json += "\"ip\":\"" + jsonEscape(WiFi.localIP().toString()) + "\"";
  json += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"heapFree\":" + String(ESP.getFreeHeap());
  json += ",\"heapMin\":" + String(ESP.getMinFreeHeap());
  json += ",\"heapLargest\":" + String(largestInternalBlock());
  json += ",\"psramFree\":" + String(ESP.getFreePsram());
  json += ",\"psramLargest\":" + String(largestPsramBlock());
  json += ",\"flashSize\":" + String(ESP.getFlashChipSize());
  json += ",\"chipModel\":\"" + jsonEscape(String(ESP.getChipModel())) + "\"";
  json += ",\"cpuMhz\":" + String(ESP.getCpuFreqMHz());
  json += "}";
  json += ",\"layers\":[";
  for (int i = 0; i < 3; i++) {
    if (i) json += ",";
    char ageLabel[16];
    formatLayerAgeLabel(i, ageLabel, sizeof(ageLabel));
    json += "{";
    json += "\"name\":\"" + String(layerNames[i]) + "\"";
    json += ",\"status\":\"" + layerCacheStatusName(i) + "\"";
    json += ",\"valid\":" + String(layerCaches[i].valid ? "true" : "false");
    json += ",\"matches\":" + String(layerCacheMatches(i) ? "true" : "false");
    json += ",\"fresh\":" + String(layerCacheFresh(i) ? "true" : "false");
    json += ",\"ageSec\":" + String(layerCacheAgeSecs(i));
    json += ",\"ageLabel\":\"" + String(ageLabel) + "\"";
    json += ",\"zoom\":" + String(layerCaches[i].zoom);
    json += ",\"map\":\"" + String(layerCaches[i].mapStyle >= 0 && layerCaches[i].mapStyle < 3 ? mapNames[layerCaches[i].mapStyle] : "--") + "\"";
    json += "}";
  }
  json += "]";

  // Sleep schedule state
  {
    char onBuf[6], offBuf[6];
    snprintf(onBuf,  sizeof(onBuf),  "%02d:%02d", sleepOnHour,  sleepOnMinute);
    snprintf(offBuf, sizeof(offBuf), "%02d:%02d", sleepOffHour, sleepOffMinute);
    const char* stateStr = sleepPhase == SLEEP_PENDING     ? "pending"
                         : sleepPhase == SLEEP_DARK        ? "dark"
                         : sleepPhase == SLEEP_TOUCH_WOKEN ? "woken"
                         : "awake";
    unsigned long wakeRemain = 0;
    if (sleepPhase == SLEEP_TOUCH_WOKEN && sleepPhaseMs > 0) {
      unsigned long elapsedS = (millis() - sleepPhaseMs) / 1000UL;
      wakeRemain = elapsedS < (unsigned long)sleepWakeDurationSecs
                 ? (unsigned long)sleepWakeDurationSecs - elapsedS : 0;
    }
    json += ",\"sleep\":{";
    json += "\"enabled\":"    + String(sleepScheduleEnabled ? "true" : "false");
    json += ",\"onTime\":\""  + String(onBuf)  + "\"";
    json += ",\"offTime\":\"" + String(offBuf) + "\"";
    json += ",\"state\":\""  + String(stateStr) + "\"";
    json += ",\"inWindow\":" + String(isInSleepWindow() ? "true" : "false");
    json += ",\"wakeSecs\":" + String(sleepWakeDurationSecs);
    json += ",\"wakeRemainSecs\":" + String(wakeRemain);
    json += "}";
  }

  json += "}";
  return json;
}

const char kWebUiHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32S3 Weather</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #071014;
      --panel: rgba(18, 31, 38, .82);
      --panel-strong: rgba(24, 43, 52, .95);
      --line: rgba(166, 205, 218, .18);
      --text: #edf8fb;
      --muted: #94aeb7;
      --accent: #4dd5ff;
      --accent2: #86efac;
      --warn: #f7cf6a;
      --bad: #fb7185;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: var(--bg);
      color: var(--text);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      padding: 28px 18px;
      background:
        radial-gradient(circle at 12% 8%, rgba(77, 213, 255, .18), transparent 32%),
        radial-gradient(circle at 82% 10%, rgba(134, 239, 172, .13), transparent 30%),
        linear-gradient(160deg, #071014 0%, #10242c 58%, #091216 100%);
    }
    main { width: min(1120px, 100%); margin: 0 auto; }
    header { display: flex; align-items: end; justify-content: space-between; gap: 16px; margin: 0 0 18px; }
    h1 { font-size: clamp(22px, 3vw, 34px); margin: 0; letter-spacing: 0; }
    .sub { margin: 5px 0 0; color: var(--muted); font-size: 14px; }
    .pill { border: 1px solid var(--line); border-radius: 999px; padding: 8px 12px; color: var(--muted); background: rgba(255,255,255,.04); font-size: 13px; white-space: nowrap; }
    .mirror-wrap {
      background: #000;
      border: 1px solid rgba(255,255,255,.24);
      border-radius: 12px;
      width: min(800px, 100%);
      line-height: 0;
      overflow: hidden;
      box-shadow: 0 24px 80px rgba(0,0,0,.42), 0 0 0 1px rgba(77,213,255,.06);
    }
    #mirror { width: 100%; height: auto; image-rendering: auto; cursor: crosshair; user-select: none; }
    .meta { display: flex; flex-wrap: wrap; gap: 10px; margin: 14px 0 18px; color: var(--muted); font-size: 13px; }
    .meta span { border: 1px solid var(--line); background: rgba(255,255,255,.045); border-radius: 999px; padding: 7px 10px; }
    .grid { display: grid; grid-template-columns: minmax(0, 1.15fr) minmax(280px, .85fr); gap: 16px; align-items: start; }
    .card {
      border: 1px solid var(--line);
      border-radius: 12px;
      background: var(--panel);
      backdrop-filter: blur(16px);
      box-shadow: 0 14px 44px rgba(0,0,0,.24);
      padding: 16px;
    }
    .card h2 { margin: 0 0 12px; font-size: 15px; letter-spacing: 0; }
    .control { display: grid; grid-template-columns: 104px 1fr 58px; gap: 12px; align-items: center; margin: 12px 0; }
    .control label { color: var(--muted); font-size: 13px; }
    output { color: var(--text); font-variant-numeric: tabular-nums; text-align: right; }
    input[type=range] { width: 100%; accent-color: var(--accent); }
    .actions { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 14px; }
    button {
      border: 1px solid var(--line);
      border-radius: 9px;
      background: rgba(255,255,255,.07);
      color: var(--text);
      padding: 10px 12px;
      font: inherit;
      cursor: pointer;
    }
    button:hover { border-color: rgba(77,213,255,.55); background: rgba(77,213,255,.12); }
    button.danger:hover { border-color: rgba(251,113,133,.7); background: rgba(251,113,133,.14); }
    table { border-collapse: collapse; width: 100%; font-size: 13px; }
    th, td { border-bottom: 1px solid var(--line); padding: 9px 8px; text-align: left; }
    th { color: var(--muted); font-weight: 650; }
    .fresh { color: var(--accent2); }
    .stale, .queued, .updating { color: var(--warn); }
    .missing, .mismatch { color: var(--bad); }
    .hw { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .metric { border: 1px solid var(--line); background: rgba(255,255,255,.045); border-radius: 10px; padding: 10px; min-width: 0; }
    .metric .k { display: block; color: var(--muted); font-size: 12px; margin-bottom: 4px; }
    .metric .v { display: block; overflow-wrap: anywhere; font-size: 14px; font-variant-numeric: tabular-nums; }
    footer { color: var(--muted); display: flex; flex-wrap: wrap; gap: 10px 16px; margin: 20px 0 0; font-size: 13px; }
    a { color: #7dd3fc; text-decoration: none; }
    a:hover { text-decoration: underline; }
    @media (max-width: 860px) { .grid { grid-template-columns: 1fr; } header { align-items: start; flex-direction: column; } .hw { grid-template-columns: 1fr; } }
    .sched-row { display:flex; align-items:center; justify-content:space-between; gap:8px; padding:9px 0; border-bottom:1px solid var(--line); }
    .sched-row:last-child { border-bottom:none; }
    .sched-row > label:first-child { color:var(--muted); font-size:13px; flex:1; }
    .toggle { position:relative; display:inline-block; width:40px; height:22px; flex-shrink:0; }
    .toggle input { opacity:0; width:0; height:0; position:absolute; }
    .slider { position:absolute; cursor:pointer; inset:0; background:#334155; border-radius:22px; transition:.2s; }
    .slider:before { content:''; position:absolute; height:16px; width:16px; left:3px; bottom:3px; background:#fff; border-radius:50%; transition:.2s; }
    .toggle input:checked + .slider { background:var(--accent); }
    .toggle input:checked + .slider:before { transform:translateX(18px); }
    .time-input { background:var(--panel-strong); border:1px solid var(--line); border-radius:6px; color:var(--text); padding:5px 8px; font:inherit; font-size:13px; width:100px; }
    .time-input:focus { outline:none; border-color:var(--accent); }
    .sched-status { margin-top:10px; font-size:12px; color:var(--muted); line-height:1.7; }
  </style>
</head>
<body>
  <main>
    <header>
      <div>
        <h1>ESP32S3 Weather</h1>
        <p class="sub">Live TFT mirror, cache telemetry, and LAN controls.</p>
      </div>
      <div class="pill" id="ipPill">LAN</div>
    </header>
    <div class="mirror-wrap"><img id="mirror" src="/screen.bmp?v=0" width="800" height="480" alt="TFT mirror"></div>
    <div class="meta">
      <span>Frame <strong id="frame">0</strong></span>
      <span>Layer <strong id="layer">--</strong></span>
      <span>Map <strong id="map">--</strong></span>
      <span>Zoom <strong id="zoom">--</strong></span>
      <span>Render <strong id="render">--</strong></span>
      <span class="dim" id="touchResult"></span>
    </div>
    <div class="grid">
      <section class="card">
        <h2>Layer Cache</h2>
        <table>
          <thead><tr><th>Layer</th><th>Status</th><th>Age</th><th>Cache Zoom</th><th>Cache Map</th></tr></thead>
          <tbody id="layers"></tbody>
        </table>
      </section>
      <section class="card">
        <h2>Controls</h2>
        <div class="control">
          <label for="zoomControl">Zoom</label>
          <input id="zoomControl" type="range" min="5" max="12" step="1" value="7">
          <output id="zoomOut">7</output>
        </div>
        <div class="control">
          <label for="brightnessControl">Brightness</label>
          <input id="brightnessControl" type="range" min="20" max="255" step="1" value="100">
          <output id="brightnessOut">100</output>
        </div>
        <div class="control">
          <label for="radarAlphaControl">Radar opacity</label>
          <input id="radarAlphaControl" type="range" min="0" max="100" step="1" value="55">
          <output id="radarAlphaOut">55%</output>
        </div>
        <div class="control">
          <label for="cloudAlphaControl">Cloud opacity</label>
          <input id="cloudAlphaControl" type="range" min="0" max="100" step="1" value="25">
          <output id="cloudAlphaOut">25%</output>
        </div>
        <div class="control">
          <label for="rainAlphaControl">Rain opacity</label>
          <input id="rainAlphaControl" type="range" min="0" max="100" step="1" value="25">
          <output id="rainAlphaOut">25%</output>
        </div>
      </section>
      <section class="card">
        <h2>Hardware</h2>
        <div class="hw" id="hardware"></div>
        <div class="actions">
          <button id="rebootBtn" class="danger">Reboot</button>
          <button id="resetWifiBtn" class="danger">Reset WiFi Settings</button>
        </div>
      </section>
      <section class="card">
        <h2>Night Schedule</h2>
        <div class="sched-row">
          <label for="sleepEnabled">Enable schedule</label>
          <label class="toggle"><input id="sleepEnabled" type="checkbox"><span class="slider"></span></label>
        </div>
        <div class="sched-row">
          <label for="sleepOnTime">Sleep at</label>
          <input id="sleepOnTime" type="time" class="time-input">
        </div>
        <div class="sched-row">
          <label for="sleepOffTime">Wake at</label>
          <input id="sleepOffTime" type="time" class="time-input">
        </div>
        <div id="sleepStatus" class="sched-status"></div>
      </section>
    </div>
    <footer>
      <span>Credits: Mirko Pavleski and Anthony Clarke.</span>
      <a href="https://bsky.app/profile/anthonyjclarke.bsky.social" target="_blank" rel="noopener">BlueSky</a>
      <a href="https://github.com/anthonyjclarke/ESP32S3-Weather" target="_blank" rel="noopener">GitHub Repo</a>
    </footer>
  </main>
  <script>
    const mirror = document.getElementById('mirror');
    const zoomControl = document.getElementById('zoomControl');
    const brightnessControl = document.getElementById('brightnessControl');
    const radarAlphaControl = document.getElementById('radarAlphaControl');
    const cloudAlphaControl = document.getElementById('cloudAlphaControl');
    const rainAlphaControl = document.getElementById('rainAlphaControl');
    const zoomOut = document.getElementById('zoomOut');
    const brightnessOut = document.getElementById('brightnessOut');
    const radarAlphaOut = document.getElementById('radarAlphaOut');
    const cloudAlphaOut = document.getElementById('cloudAlphaOut');
    const rainAlphaOut = document.getElementById('rainAlphaOut');
    const sleepEnabledEl = document.getElementById('sleepEnabled');
    const sleepOnEl      = document.getElementById('sleepOnTime');
    const sleepOffEl     = document.getElementById('sleepOffTime');
    const sleepStatusEl  = document.getElementById('sleepStatus');
    let lastFrame = -1;
    let settingTimer = 0;
    let activeControl = null;

    function text(id, value) { document.getElementById(id).textContent = value; }
    function kb(v) { return `${Math.round(v / 1024)} KB`; }

    function metric(label, value) {
      return `<div class="metric"><span class="k">${label}</span><span class="v">${value}</span></div>`;
    }

    function drawStatus(s) {
      text('frame', s.frameVersion);
      text('layer', s.layer);
      text('map', s.map);
      text('zoom', s.zoom);
      const progress = s.renderTilesTotal > 0 ? ` ${s.renderTilesDone}/${s.renderTilesTotal}` : '';
      text('render', `${s.renderState}${progress}`);
      document.getElementById('ipPill').textContent = s.hardware.ip || 'LAN';
      if (activeControl !== 'zoom') {
        zoomControl.value = s.zoom;
        zoomOut.textContent = s.zoom;
      }
      if (activeControl !== 'brightness') {
        brightnessControl.value = s.brightness;
        brightnessOut.textContent = s.brightness;
      }
      if (s.overlayAlpha) {
        if (activeControl !== 'radarAlpha') {
          radarAlphaControl.value = s.overlayAlpha.radar;
          radarAlphaOut.textContent = `${s.overlayAlpha.radar}%`;
        }
        if (activeControl !== 'cloudAlpha') {
          cloudAlphaControl.value = s.overlayAlpha.clouds;
          cloudAlphaOut.textContent = `${s.overlayAlpha.clouds}%`;
        }
        if (activeControl !== 'rainAlpha') {
          rainAlphaControl.value = s.overlayAlpha.rain;
          rainAlphaOut.textContent = `${s.overlayAlpha.rain}%`;
        }
      }
      document.getElementById('layers').innerHTML = s.layers.map(l =>
        `<tr><td>${l.name}</td><td class="${l.status}">${l.status}</td><td>${l.ageLabel} (${l.ageSec}s)</td><td>${l.valid ? l.zoom : '--'}</td><td>${l.valid ? l.map : '--'}</td></tr>`
      ).join('');
      document.getElementById('hardware').innerHTML = [
        metric('IP Address', s.hardware.ip),
        metric('SSID / RSSI', `${s.hardware.ssid || '--'} / ${s.hardware.rssi} dBm`),
        metric('Heap Free', kb(s.hardware.heapFree)),
        metric('Largest Heap Block', kb(s.hardware.heapLargest)),
        metric('Min Heap', kb(s.hardware.heapMin)),
        metric('PSRAM Free', kb(s.hardware.psramFree)),
        metric('Largest PSRAM Block', kb(s.hardware.psramLargest)),
        metric('Flash / CPU', `${kb(s.hardware.flashSize)} / ${s.hardware.cpuMhz} MHz`)
      ].join('');

      if (s.sleep) {
        const sl = s.sleep;
        if (document.activeElement !== sleepEnabledEl) sleepEnabledEl.checked = sl.enabled;
        if (document.activeElement !== sleepOnEl)  sleepOnEl.value  = sl.onTime  || '';
        if (document.activeElement !== sleepOffEl) sleepOffEl.value = sl.offTime || '';
        const stateColor = { awake:'var(--accent2)', pending:'var(--warn)', dark:'var(--muted)', woken:'var(--warn)' };
        const stateLabel = { awake:'awake', pending:'sleeping soon…', dark:'display off', woken:'touch-woken' };
        sleepStatusEl.innerHTML =
          'State: <span style="color:' + (stateColor[sl.state]||'inherit') + '">' + (stateLabel[sl.state]||sl.state) + '</span>'
          + (sl.inWindow ? ' &middot; <span style="color:var(--warn)">In window</span>' : '')
          + (sl.state === 'woken' && sl.wakeRemainSecs > 0 ? ' &middot; Re-sleep in ' + sl.wakeRemainSecs + 's' : '')
          + '<br>Window: ' + (sl.enabled ? sl.onTime + '–' + sl.offTime : '<em>disabled</em>');
      }
    }

    async function poll() {
      try {
        const s = await fetch('/api/status', { cache: 'no-store' }).then(r => r.json());
        drawStatus(s);
        if (s.frameVersion !== lastFrame) {
          lastFrame = s.frameVersion;
          mirror.src = `/screen.bmp?v=${s.frameVersion}&t=${Date.now()}`;
        }
      } catch (err) {
        text('render', 'offline');
      }
    }

    mirror.addEventListener('click', async (event) => {
      const r = mirror.getBoundingClientRect();
      const x = Math.max(0, Math.min(799, Math.round((event.clientX - r.left) * 800 / r.width)));
      const y = Math.max(0, Math.min(479, Math.round((event.clientY - r.top) * 480 / r.height)));
      const result = document.getElementById('touchResult');
      try {
        const response = await fetch('/api/touch', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ x, y })
        });
        result.textContent = response.ok ? `touch ${x},${y}` : 'touch failed';
        poll();
      } catch (err) {
        result.textContent = 'touch failed';
      }
    });

    async function setConfig(payload) {
      await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      poll();
    }

    function scheduleConfig(type, value) {
      activeControl = type;
      clearTimeout(settingTimer);
      settingTimer = setTimeout(async () => {
        const payload = {};
        payload[type] = Number(value);
        try { await setConfig(payload); } finally { activeControl = null; }
      }, 120);
    }

    zoomControl.addEventListener('input', () => {
      zoomOut.textContent = zoomControl.value;
      scheduleConfig('zoom', zoomControl.value);
    });

    brightnessControl.addEventListener('input', () => {
      brightnessOut.textContent = brightnessControl.value;
      scheduleConfig('brightness', brightnessControl.value);
    });

    radarAlphaControl.addEventListener('input', () => {
      radarAlphaOut.textContent = `${radarAlphaControl.value}%`;
      scheduleConfig('radarAlpha', radarAlphaControl.value);
    });

    cloudAlphaControl.addEventListener('input', () => {
      cloudAlphaOut.textContent = `${cloudAlphaControl.value}%`;
      scheduleConfig('cloudAlpha', cloudAlphaControl.value);
    });

    rainAlphaControl.addEventListener('input', () => {
      rainAlphaOut.textContent = `${rainAlphaControl.value}%`;
      scheduleConfig('rainAlpha', rainAlphaControl.value);
    });

    document.getElementById('rebootBtn').addEventListener('click', async () => {
      if (!confirm('Reboot the ESP32 now?')) return;
      await fetch('/api/reboot', { method: 'POST' });
      text('render', 'rebooting');
    });

    document.getElementById('resetWifiBtn').addEventListener('click', async () => {
      if (!confirm('Reset saved WiFi settings and reboot?')) return;
      await fetch('/api/reset-wifi', { method: 'POST' });
      text('render', 'resetting WiFi');
    });

    sleepEnabledEl.addEventListener('change', () => setConfig({ sleepEnabled: sleepEnabledEl.checked }));
    sleepOnEl.addEventListener('change',  () => setConfig({ sleepOnTime:  sleepOnEl.value }));
    sleepOffEl.addEventListener('change', () => setConfig({ sleepOffTime: sleepOffEl.value }));

    poll();
    setInterval(poll, 1000);
  </script>
</body>
</html>
)HTML";

void handleWebRoot() {
  webServer.send_P(200, "text/html", kWebUiHtml);
}

void handleWebStatus() {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", buildStatusJson());
}

void handleWebScreenBmp() {
  const int width = cfg::kScreenWidth;
  const int height = cfg::kScreenHeight;
  const int rowSize = (width * 3 + 3) & ~3;
  const uint32_t imageSize = rowSize * height;

  lgfx::bitmap_header_t bmp = {};
  bmp.bfType = 0x4D42;
  bmp.bfSize = sizeof(bmp) + imageSize;
  bmp.bfOffBits = sizeof(bmp);
  bmp.biSize = 40;
  bmp.biWidth = width;
  bmp.biHeight = height;
  bmp.biPlanes = 1;
  bmp.biBitCount = 24;
  bmp.biCompression = 0;
  bmp.biSizeImage = imageSize;

  webServer.sendHeader("Cache-Control", "no-store");
  webServer.setContentLength(bmp.bfSize);
  webServer.send(200, "image/bmp", "");

  NetworkClient& client = webServer.client();
  client.write((const uint8_t*)&bmp, sizeof(bmp));

  static uint8_t rowBuf[cfg::kScreenWidth * 3];
  for (int y = height - 1; y >= 0 && client.connected(); y--) {
    gfx.readRect(0, y, width, 1, (lgfx::rgb888_t*)rowBuf);
    client.write(rowBuf, rowSize);
    delay(1);
  }
}

void handleWebTouch() {
  int tx = -1;
  int ty = -1;

  if (webServer.hasArg("plain")) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, webServer.arg("plain")) == DeserializationError::Ok) {
      tx = doc["x"] | -1;
      ty = doc["y"] | -1;
    }
  }

  if (tx < 0 && webServer.hasArg("x")) tx = webServer.arg("x").toInt();
  if (ty < 0 && webServer.hasArg("y")) ty = webServer.arg("y").toInt();

  if (tx < 0 || tx >= cfg::kScreenWidth || ty < 0 || ty >= cfg::kScreenHeight) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid coordinates\"}");
    return;
  }

  if (sleepPhase == SLEEP_DARK) {
    setBacklightBrightness(brightnessLevel);
    exitSleepRestoreDashboard();
    sleepPhase   = SLEEP_TOUCH_WOKEN;
    sleepPhaseMs = millis();
    DBG_INFO("Sleep: woken via WebUI touch");
    webServer.send(200, "application/json", "{\"ok\":true,\"woke\":true}");
    return;
  }

  bool handled = handleUiTouch(tx, ty, true);
  webServer.send(200, "application/json", String("{\"ok\":") + (handled ? "true" : "false") + "}");
}

static bool parseHHMM(const String& s, int& h, int& m) {
  int colon = s.indexOf(':');
  if (colon < 1 || colon >= (int)s.length() - 1) return false;
  h = s.substring(0, colon).toInt();
  m = s.substring(colon + 1).toInt();
  return h >= 0 && h <= 23 && m >= 0 && m <= 59;
}

void handleWebConfig() {
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"missing body\"}");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, webServer.arg("plain"));
  if (err) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

  bool changed = false;
  bool currentLayerAlphaChanged = false;

  if (doc.containsKey("brightness")) {
    int requested = doc["brightness"] | brightnessLevel;
    brightnessLevel = constrain(requested, 20, 255);
    setBacklightBrightness(brightnessLevel);
    if (appState == 0) drawProgressTimer();
    changed = true;
  }

  if (doc.containsKey("zoom")) {
    int requested = doc["zoom"] | myZoom;
    int nextZoom = constrain(requested, cfg::kMapZoomMin, cfg::kMapZoomMax);
    if (nextZoom != myZoom) {
      myZoom = nextZoom;
      DBG_INFO("WebUI zoom → %d", myZoom);
      invalidateLayerCaches();
      layerCycleLastMs = millis();
      triggerRenderForLayer(layerStyle, true);

      if (appState == 0) {
        drawMapBadges();
      }
      changed = true;
    }
  }

  if (doc.containsKey("radarAlpha")) {
    int requested = doc["radarAlpha"] | overlayAlphaPercent[0];
    int nextAlpha = constrain(requested, 0, 100);
    if (nextAlpha != overlayAlphaPercent[0]) {
      overlayAlphaPercent[0] = nextAlpha;
      layerCaches[0].valid = false;
      currentLayerAlphaChanged = currentLayerAlphaChanged || layerStyle == 0;
      changed = true;
    }
  }

  if (doc.containsKey("cloudAlpha")) {
    int requested = doc["cloudAlpha"] | overlayAlphaPercent[1];
    int nextAlpha = constrain(requested, 0, 100);
    if (nextAlpha != overlayAlphaPercent[1]) {
      overlayAlphaPercent[1] = nextAlpha;
      layerCaches[1].valid = false;
      currentLayerAlphaChanged = currentLayerAlphaChanged || layerStyle == 1;
      changed = true;
    }
  }

  if (doc.containsKey("rainAlpha")) {
    int requested = doc["rainAlpha"] | overlayAlphaPercent[2];
    int nextAlpha = constrain(requested, 0, 100);
    if (nextAlpha != overlayAlphaPercent[2]) {
      overlayAlphaPercent[2] = nextAlpha;
      layerCaches[2].valid = false;
      currentLayerAlphaChanged = currentLayerAlphaChanged || layerStyle == 2;
      changed = true;
    }
  }

  if (currentLayerAlphaChanged) {
    DBG_INFO("WebUI opacity changed | layer=%s alpha=%d%%",
             layerNames[layerStyle], overlayAlphaPercent[layerStyle]);
    layerCycleLastMs = millis();
    triggerRenderForLayer(layerStyle, true);
  }

  if (doc.containsKey("sleepEnabled")) {
    bool requested = doc["sleepEnabled"].as<bool>();
    if (requested != sleepScheduleEnabled) {
      sleepScheduleEnabled = requested;
      saveSleepSettings();
      changed = true;
      DBG_INFO("Sleep schedule %s", sleepScheduleEnabled ? "enabled" : "disabled");
    }
  }

  if (doc.containsKey("sleepOnTime")) {
    int h, m;
    if (parseHHMM(doc["sleepOnTime"].as<String>(), h, m)) {
      sleepOnHour   = h;
      sleepOnMinute = m;
      saveSleepSettings();
      changed = true;
    }
  }

  if (doc.containsKey("sleepOffTime")) {
    int h, m;
    if (parseHHMM(doc["sleepOffTime"].as<String>(), h, m)) {
      sleepOffHour   = h;
      sleepOffMinute = m;
      saveSleepSettings();
      changed = true;
    }
  }

  webServer.send(200, "application/json", String("{\"ok\":true,\"changed\":") + (changed ? "true" : "false") + "}");
}

void handleWebReboot() {
  webServer.send(200, "application/json", "{\"ok\":true,\"action\":\"reboot\"}");
  delay(200);
  ESP.restart();
}

void handleWebResetWifi() {
  webServer.send(200, "application/json", "{\"ok\":true,\"action\":\"reset-wifi\"}");
  delay(200);
  WiFiManager wm;
  wm.resetSettings();
  WiFi.disconnect(true, true);
  delay(300);
  ESP.restart();
}

void setupWebUi(bool wifiOk) {
  if (!wifiOk) {
    DBG_WARN("WebUI disabled: WiFi is not connected");
    return;
  }

  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/api/status", HTTP_GET, handleWebStatus);
  webServer.on("/screen.bmp", HTTP_GET, handleWebScreenBmp);
  webServer.on("/api/touch", HTTP_POST, handleWebTouch);
  webServer.on("/api/touch", HTTP_GET, handleWebTouch);
  webServer.on("/api/config", HTTP_POST, handleWebConfig);
  webServer.on("/api/reboot", HTTP_POST, handleWebReboot);
  webServer.on("/api/reset-wifi", HTTP_POST, handleWebResetWifi);
  webServer.onNotFound([]() {
    webServer.send(404, "text/plain", "Not found");
  });
  webServer.begin();
  webUiStarted = true;
  DBG_INFO("WebUI ready | http://%s/", WiFi.localIP().toString().c_str());
}

void handleWebUiClient() {
  if (webUiStarted) webServer.handleClient();
}

void logRenderProgressSummary(int targetLayer, int tilesOk, int tilesErr, unsigned long renderStart) {
#if DEBUG_LEVEL == 3
  const int done = renderTilesDone;
  const int total = renderTilesTotal;
  if (total <= 0) return;
  if (done >= total || (done % 4) != 0) return;

  DBG_INFO("Map render progress | layer=%s %d/%d tiles | ok=%d err=%d | %lums",
           layerNames[targetLayer], done, total, tilesOk, tilesErr,
           millis() - renderStart);
#else
  (void)targetLayer;
  (void)tilesOk;
  (void)tilesErr;
  (void)renderStart;
#endif
}

bool fetchPngToBuffer(const String& url, uint8_t** outBuf, size_t* outLen, int* httpStatus) {
  *outBuf = nullptr;
  *outLen = 0;
  if (httpStatus) *httpStatus = 0;

  unsigned long fetchStart = millis();
  String safeUrl = redactUrlForLog(url);
  DBG_VERBOSE("Fetch start | tile=%d/%d | phase=%s | heap=%u largest=%u psram=%u stackHW=%u | %s",
              renderDiagTileIndex, renderTilesTotal, renderDiagPhase,
              ESP.getFreeHeap(), largestInternalBlock(), ESP.getFreePsram(),
              renderStackHighWater(), safeUrl.c_str());

  // Explicit client with stream timeout so TLS handshake can't hang indefinitely.
  // http.begin(url) creates an internal client with no timeout — fatal when heap is fragmented.
  WiFiClientSecure secClient;
  secClient.setInsecure();
  secClient.setTimeout(15);  // seconds — caps TLS handshake + stream read

  HTTPClient http;
  http.setTimeout(15000);
  http.setConnectTimeout(15000);

  setRenderDiagPhase("http_begin");
  if (!http.begin(secClient, url)) {
    if (httpStatus) *httpStatus = -1;
    DBG_WARN("HTTP begin failed: %s", safeUrl.c_str());
    return false;
  }

  // Request uncompressed response — PNGdec needs raw bytes, not gzip.
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("User-Agent", "ESP32-WeatherDisplay/1.0");
  http.useHTTP10(true);   // disables chunked transfer-encoding; some tile servers require it
	
  setRenderDiagPhase("http_get");
  int code = http.GET();
  if (httpStatus) *httpStatus = code;
  if (code != HTTP_CODE_OK) {
    DBG_WARN("HTTP GET failed: %d | %lums | %s", code, millis() - fetchStart, safeUrl.c_str());
    http.end();
    return false;
  }

  int len = http.getSize();

#if DEBUG_LEVEL >= 4
  String ctype = http.header("Content-Type");
  String cenc  = http.header("Content-Encoding");
  DBG_VERBOSE("HTTP 200 | tile=%d/%d | get=%lums | type=%s | enc=%s | len=%d",
              renderDiagTileIndex, renderTilesTotal, millis() - fetchStart,
              ctype.c_str(), cenc.c_str(), len);
#endif

  WiFiClient* stream = http.getStreamPtr();

  // No Content-Length: read stream manually until connection closes or timeout.
  if (len <= 0) {
    setRenderDiagPhase("http_read_chunked");
    const size_t maxChunkedSize = 100000;
    uint8_t* buf = (uint8_t*)ps_malloc(maxChunkedSize);
    if (!buf) {
      DBG_ERROR("PSRAM alloc failed (chunked)");
      http.end();
      return false;
    }

    size_t total = 0;
    unsigned long t0 = millis();

    while (http.connected() && (millis() - t0 < 15000)) {
      while (stream->available()) {
        if (total >= maxChunkedSize) {
          DBG_WARN("Chunked payload too large");
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
      DBG_WARN("Payload too small: %u bytes", (unsigned)total);
      free(buf);
      http.end();
      return false;
    }

    *outBuf = buf;
    *outLen = total;
    http.end();
    setRenderDiagPhase("fetch_done");
    DBG_VERBOSE("Fetch done | tile=%d/%d | bytes=%u | %lums | heap=%u largest=%u stackHW=%u",
                renderDiagTileIndex, renderTilesTotal, (unsigned)total,
                millis() - fetchStart, ESP.getFreeHeap(), largestInternalBlock(),
                renderStackHighWater());
    return true;
  }

  uint8_t* buf = (uint8_t*)ps_malloc(len);
  if (!buf) {
    DBG_ERROR("PSRAM alloc failed: %d bytes", len);
    http.end();
    return false;
  }

  setRenderDiagPhase("http_read");
  size_t actuallyRead = readHttpPayload(stream, buf, (size_t)len, 15000);
  http.end();

  if (actuallyRead != (size_t)len || len < 16) {
    DBG_WARN("Read failed: expected %d got %u | %lums", len, (unsigned)actuallyRead, millis() - fetchStart);
    free(buf);
    return false;
  }

  *outBuf = buf;
  *outLen = len;
  setRenderDiagPhase("fetch_done");
  DBG_VERBOSE("Fetch done | tile=%d/%d | bytes=%u | %lums | heap=%u largest=%u stackHW=%u",
              renderDiagTileIndex, renderTilesTotal, (unsigned)len,
              millis() - fetchStart, ESP.getFreeHeap(), largestInternalBlock(),
              renderStackHighWater());
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
  int sw = 220;
  lcd.fillRect(sx, ry, sw, rh, panelColor);
  lcd.drawRect(sx, ry, sw, rh, TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.drawString("by mircemk & anthonyjclarke", sx + sw / 2, ry + 11);

  lcd.fillRect(35, 388, 80, 22, panelColor);
  lcd.drawRect(35, 388, 80, 22, TFT_WHITE);
  lcd.drawString("OHRID", 35 + 40, 388 + 11);
  markScreenUpdated();
}

void setBrightnessFromTouchY(int ty) {
  int bx = 4, by = 4, bh = 409;

  if (ty < by) ty = by;
  if (ty > by + bh) ty = by + bh;

  // top of bar = dim, bottom = bright
  brightnessLevel = map(ty, by, by + bh, 255, 20);

  if (brightnessLevel < 20) brightnessLevel = 20;
  if (brightnessLevel > 255) brightnessLevel = 255;

  setBacklightBrightness(brightnessLevel);
}

static bool initWiFi() {
  WiFiManager wm;
  bool hasSaved = WiFi.SSID().length() > 0;
  bool hasSecret = strlen(SECRET_WIFI_SSID) > 0;

  wm.setConfigPortalTimeout((hasSaved || hasSecret) ? 15 : 0);

  if (hasSecret && !hasSaved) {
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
  }

  return wm.autoConnect(cfg::kWifiApName);
}

void drawOtaStatus(const char* line1, const char* line2, int percent = -1) {
  if (appState != 0) return;

  int x = 260, y = 185, w = 280, h = 110;
  lcd.fillRect(x, y, w, h, panelColor);
  lcd.drawRect(x, y, w, h, TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_CYAN);
  lcd.drawString(line1, x + w / 2, y + 28);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(line2, x + w / 2, y + 55);

  if (percent >= 0) {
    int barX = x + 24, barY = y + 78, barW = w - 48, barH = 14;
    int fillW = (barW - 2) * percent / 100;
    lcd.drawRect(barX, barY, barW, barH, TFT_WHITE);
    lcd.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_GREEN);
    if (fillW < barW - 2) {
      lcd.fillRect(barX + 1 + fillW, barY + 1, (barW - 2) - fillW, barH - 2, TFT_BLACK);
    }
  }
  markScreenUpdated();
}

void setupOta(bool wifiOk) {
  if (!wifiOk) {
    DBG_WARN("OTA disabled: WiFi is not connected");
    return;
  }

  ArduinoOTA.setHostname(cfg::kOtaHostname);
  if (strlen(SECRET_OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(SECRET_OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    const char* target = ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem";
    DBG_INFO("OTA start | target=%s", target);
    drawOtaStatus("OTA Update", "Receiving firmware...", 0);
  });

  ArduinoOTA.onEnd([]() {
    otaInProgress = false;
    DBG_INFO("OTA complete; rebooting");
    drawOtaStatus("OTA Complete", "Rebooting...", 100);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static int lastPercent = -1;
    if (total == 0) return;
    int percent = (progress * 100U) / total;
    if (percent == lastPercent) return;
    lastPercent = percent;
    drawOtaStatus("OTA Update", "Receiving firmware...", percent);
    if ((percent % 10) == 0) {
      DBG_INFO("OTA progress | %d%%", percent);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    const char* reason = "unknown";
    if (error == OTA_AUTH_ERROR) reason = "auth";
    else if (error == OTA_BEGIN_ERROR) reason = "begin";
    else if (error == OTA_CONNECT_ERROR) reason = "connect";
    else if (error == OTA_RECEIVE_ERROR) reason = "receive";
    else if (error == OTA_END_ERROR) reason = "end";

    DBG_ERROR("OTA failed | error=%u reason=%s", (unsigned)error, reason);
    drawOtaStatus("OTA Failed", reason);
  });

  ArduinoOTA.begin();
  DBG_INFO("OTA ready | host=%s.local | auth=%s",
           cfg::kOtaHostname, strlen(SECRET_OTA_PASSWORD) > 0 ? "enabled" : "disabled");
}

void drawProgressTimer() {
  if (appState != 0) return;

  int bx = 4, by = 4, bw = 24, bh = 409;

  lcd.drawRect(bx, by, bw, bh, TFT_WHITE);
  lcd.drawRect(bx + 2, by + 2, bw - 4, bh - 4, TFT_WHITE);
  lcd.fillRect(bx + 3, by + 3, bw - 6, bh - 6, TFT_BLACK);

  // Progress do sledezen realtime update
  float p = (float)(millis() - lastUpdate) / ((float)cfg::kRealtimeRefreshSecs * 1000.0f);
  if (p > 1.0) p = 1.0;
  int cH = (int)(397 * p);

  for (int y = 0; y < cH; y += 4) {
    lcd.drawFastHLine(bx + 7, (by + bh - 7) - y, bw - 14, TFT_SKYBLUE);
  }

  // Brightness marker
  int markerY = map(brightnessLevel, 255, 20, by, by + bh);
  lcd.drawFastHLine(bx + 4, markerY, bw - 8, TFT_YELLOW);
  lcd.drawFastHLine(bx + 4, markerY - 1, bw - 8, TFT_YELLOW);
  markScreenUpdated();
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
  markScreenUpdated();
}

void getWeatherData() {
  unsigned long t0 = millis();
  HTTPClient http;
  http.setTimeout(20000);

  String weatherUrl =
    String("https://api.open-meteo.com/v1/forecast?"
           "latitude=") + String(cfg::kLocationLatitude, 6) +
    "&longitude=" + String(cfg::kLocationLongitude, 6) +
    "&current=temperature_2m,weather_code"
    "&hourly=temperature_2m,weather_code,pressure_msl,cloud_cover"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,"
    "relative_humidity_2m_mean,wind_speed_10m_max,uv_index_max,shortwave_radiation_sum"
    "&timezone=auto&forecast_days=16";

  http.begin(weatherUrl);

  int weatherCode = http.GET();
  if (weatherCode == HTTP_CODE_OK) {
    String payload = http.getString();
    SpiRamJsonDocument doc(96 * 1024);
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
      DBG_ERROR("Forecast JSON parse failed: %s | payload=%u | PSRAM=%u",
                err.c_str(), (unsigned)payload.length(), ESP.getFreePsram());
    } else {
      JsonArray hourlyTemp = doc["hourly"]["temperature_2m"].as<JsonArray>();
      JsonArray hourlyCode = doc["hourly"]["weather_code"].as<JsonArray>();
      JsonArray hourlyPress = doc["hourly"]["pressure_msl"].as<JsonArray>();
      JsonArray hourlyCloud = doc["hourly"]["cloud_cover"].as<JsonArray>();
      JsonArray dailyMax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
      JsonArray dailyMin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
      JsonArray dailyCode = doc["daily"]["weather_code"].as<JsonArray>();
      JsonArray dailyRain = doc["daily"]["precipitation_sum"].as<JsonArray>();
      JsonArray dailyHum = doc["daily"]["relative_humidity_2m_mean"].as<JsonArray>();
      JsonArray dailyWind = doc["daily"]["wind_speed_10m_max"].as<JsonArray>();
      JsonArray dailyUV = doc["daily"]["uv_index_max"].as<JsonArray>();
      JsonArray dailySolar = doc["daily"]["shortwave_radiation_sum"].as<JsonArray>();

      bool hasForecast =
        !doc["current"]["temperature_2m"].isNull() &&
        hourlyTemp.size() >= 22 &&
        hourlyCode.size() >= 22 &&
        hourlyPress.size() >= 373 &&
        hourlyCloud.size() >= 373 &&
        dailyMax.size() >= 16 &&
        dailyMin.size() >= 16 &&
        dailyCode.size() >= 16 &&
        dailyRain.size() >= 16 &&
        dailyHum.size() >= 16 &&
        dailyWind.size() >= 16 &&
        dailyUV.size() >= 16 &&
        dailySolar.size() >= 16;

      if (!hasForecast) {
        DBG_WARN("Forecast JSON missing fields | hourlyTemp=%u hourlyPress=%u dailyMax=%u",
                 (unsigned)hourlyTemp.size(), (unsigned)hourlyPress.size(), (unsigned)dailyMax.size());
      } else {
        currentTemp = doc["current"]["temperature_2m"];
        morningTemp = hourlyTemp[9];
        morningCode = hourlyCode[9];
        noonTemp = hourlyTemp[14];
        noonCode = hourlyCode[14];
        eveningTemp = hourlyTemp[21];
        eveningCode = hourlyCode[21];

        for (int i = 0; i < 16; i++) {
          dMax[i]   = dailyMax[i];
          dMin[i]   = dailyMin[i];
          dCode[i]  = dailyCode[i];
          dRain[i]  = dailyRain[i];
          dPress[i] = hourlyPress[i * 24 + 12];
          dCloud[i] = hourlyCloud[i * 24 + 12];

          dHum[i]   = dailyHum[i];
          dWind[i]  = dailyWind[i];
          dUV[i]    = dailyUV[i];
          dSolar[i] = dailySolar[i];
        }

        DBG_INFO("Weather OK: %s | %.1f C | 16d forecast | %lums",
                 cfg::kLocationName, currentTemp, millis() - t0);
      }
    }
  } else {
    DBG_WARN("Forecast HTTP failed: %d | %s", weatherCode, weatherUrl.c_str());
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
    DBG_VERBOSE("RainViewer frame time=%ld", radarTS);
    DBG_VERBOSE("RainViewer path=%s", radarPath.c_str());
  } else {
    DBG_WARN("RainViewer: no past radar frames available");
  }
} else {
  DBG_WARN("RainViewer API request failed");
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
  markScreenUpdated();
}

void latLonToWorldPixels(double lat, double lon, int zoom, double* worldX, double* worldY) {
  double n = pow(2.0, zoom);
  double latRad = lat * M_PI / 180.0;

  *worldX = ((lon + 180.0) / 360.0) * n * kTileSize;
  *worldY = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) / 2.0 * n * kTileSize;
}

void drawLocationMarker() {
  int px = cfg::kScreenWidth / 2;
  int py = kMapCanvasHeight / 2;

  // Marker
  renderTarget->fillCircle(px, py, 5, TFT_RED);
  renderTarget->drawCircle(px, py, 6, TFT_WHITE);
  renderTarget->drawPixel(px, py, TFT_WHITE);
}

void renderRadarMap() {
  unsigned long renderStart = millis();
  int tilesOk = 0, tilesErr = 0;
  int targetLayer = renderLayerStyle;
  int targetMapStyle = renderMapStyle;
  int targetZoom = renderZoom;

  renderTarget->fillSprite(TFT_BLACK);

  double centerWorldX = 0.0;
  double centerWorldY = 0.0;
  latLonToWorldPixels(myLat, myLon, targetZoom, &centerWorldX, &centerWorldY);

  double topLeftWorldX = centerWorldX - (cfg::kScreenWidth / 2.0);
  double topLeftWorldY = centerWorldY - (kMapCanvasHeight / 2.0);
  int startTX = (int)floor(topLeftWorldX / kTileSize);
  int endTX = (int)floor((topLeftWorldX + cfg::kScreenWidth - 1) / kTileSize);
  int startTY = (int)floor(topLeftWorldY / kTileSize);
  int endTY = (int)floor((topLeftWorldY + kMapCanvasHeight - 1) / kTileSize);
  int tileCount = 1 << targetZoom;
  renderTilesDone  = 0;
  renderTilesTotal = (endTX - startTX + 1) * (endTY - startTY + 1);
  renderDiagLastProgressMs = millis();
  setRenderDiagPhase("render_start");
#if DEBUG_LEVEL >= 4
  DBG_VERBOSE("Map render start | layer=%s zoom=%d map=%s | %d tiles | x=%d..%d y=%d..%d | heap=%u largest=%u psram=%u psramLargest=%u stackHW=%u",
              layerNames[targetLayer], targetZoom, mapNames[targetMapStyle], renderTilesTotal,
              startTX, endTX, startTY, endTY,
              ESP.getFreeHeap(), largestInternalBlock(), ESP.getFreePsram(),
              largestPsramBlock(), renderStackHighWater());
#else
  DBG_INFO("Map render start | layer=%s zoom=%d map=%s | %d tiles",
           layerNames[targetLayer], targetZoom, mapNames[targetMapStyle], renderTilesTotal);
#endif
  bool skipOwmOverlay = false;

  if (targetLayer != 0) {
    if (strlen(owmApiKey) == 0) {
      DBG_WARN("OpenWeatherMap API key missing — skipping CLOUDS/RAIN overlay");
      skipOwmOverlay = true;
    } else if (owmAuthFailed) {
      DBG_WARN("OpenWeatherMap auth previously failed — skipping CLOUDS/RAIN overlay until reboot");
      skipOwmOverlay = true;
    }
  }

  for (int tileX = startTX; tileX <= endTX; tileX++) {
    for (int tileY = startTY; tileY <= endTY; tileY++) {
      if (tileY < 0 || tileY >= tileCount) continue;

      // Guard against attempting a TLS handshake when heap is too fragmented.
      // mbedTLS needs ~40KB contiguous; below 80KB we skip rather than hang.
      if (ESP.getFreeHeap() < 80000) {
        DBG_WARN("Tile %d/%d skipped — heap low: %u bytes",
                 renderTilesDone + 1, renderTilesTotal, ESP.getFreeHeap());
        renderTilesDone += 1;
        renderDiagLastProgressMs = millis();
        continue;
      }

      int wrappedTileX = ((tileX % tileCount) + tileCount) % tileCount;
      globalX = (int)round((tileX * kTileSize) - topLeftWorldX);
      globalY = (int)round((tileY * kTileSize) - topLeftWorldY);

      String mU = String(mapUrls[targetMapStyle]) + String(targetZoom) + "/" + String(wrappedTileX) + "/" + String(tileY) + ".png";

      uint8_t* baseBuf = nullptr;
      size_t baseLen = 0;

      int tileIndex = renderTilesDone + 1;
      setRenderDiagContext("base_fetch", tileIndex, wrappedTileX, tileY, mU);
      DBG_VERBOSE("Tile %d/%d base start | layer=%s z=%d x=%d y=%d | screen=%d,%d | heap=%u largest=%u",
                  tileIndex, renderTilesTotal, layerNames[targetLayer], targetZoom, wrappedTileX, tileY,
                  globalX, globalY, ESP.getFreeHeap(), largestInternalBlock());

      if (fetchPngToBuffer(mU, &baseBuf, &baseLen)) {
        setRenderDiagPhase("base_decode");
#if DEBUG_LEVEL >= 4
        unsigned long decodeStart = millis();
#endif
        if (png.openRAM(baseBuf, baseLen, pngDrawCanvas) == PNG_SUCCESS) {
          png.decode(NULL, 0);
          png.close();
          tilesOk++;
#if DEBUG_LEVEL >= 4
          DBG_VERBOSE("Tile %d/%d base decoded | bytes=%u | %lums",
                      tileIndex, renderTilesTotal, (unsigned)baseLen, millis() - decodeStart);
#endif
        } else {
          DBG_WARN("Base PNG open failed: %s", mU.c_str());
          tilesErr++;
        }
        free(baseBuf);
      } else {
        DBG_WARN("Base fetch failed: %s", mU.c_str());
        tilesErr++;
      }

      String layerUrl = "";

if (targetLayer == 0) {
	  if (radarPath.length() > 0) {
	    layerUrl = radarHost + radarPath + "/256/" +
	               String(targetZoom) + "/" +
	               String(wrappedTileX) + "/" +
	               String(tileY) + "/1/1_1.png";
	  }
		} else if (!skipOwmOverlay) {
	        layerUrl = "https://tile.openweathermap.org/map/" +
	                   String(owmLayerIds[targetLayer]) + "/" +
	                   String(targetZoom) + "/" +
	                   String(wrappedTileX) + "/" +
	                   String(tileY) +
	                   ".png?appid=" + String(owmApiKey);
	      }

 if (layerUrl.length() > 0) {
  uint8_t* overlayBuf = nullptr;
  size_t overlayLen = 0;

    String safeLayerUrl = redactUrlForLog(layerUrl);
    setRenderDiagContext("overlay_fetch", tileIndex, wrappedTileX, tileY, layerUrl);
    DBG_VERBOSE("Tile %d/%d overlay start | layer=%s | heap=%u largest=%u | %s",
                tileIndex, renderTilesTotal, layerNames[targetLayer],
                ESP.getFreeHeap(), largestInternalBlock(), safeLayerUrl.c_str());

    int overlayStatus = 0;
	  if (fetchPngToBuffer(layerUrl, &overlayBuf, &overlayLen, &overlayStatus)) {

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
      DBG_WARN("Overlay is NOT PNG! len=%u", (unsigned)overlayLen);
#if DEBUG_LEVEL >= 4
      Serial.print("[VERB]  First 32 bytes HEX: ");
      for (size_t k = 0; k < 32 && k < overlayLen; k++) {
        Serial.printf("%02X ", overlayBuf[k]);
      }
      Serial.println();
      Serial.print("[VERB]  First 120 chars TXT: ");
      for (size_t k = 0; k < 120 && k < overlayLen; k++) {
        char c = (char)overlayBuf[k];
        if (c >= 32 && c <= 126) Serial.print(c);
        else Serial.print('.');
      }
      Serial.println();
#endif
      free(overlayBuf);
    } else {
      setRenderDiagPhase("overlay_decode");
#if DEBUG_LEVEL >= 4
      unsigned long decodeStart = millis();
#endif
      int rc = png.openRAM(overlayBuf, overlayLen, pngDrawOverlayCanvas);
      if (rc == PNG_SUCCESS) {
        int decRc = png.decode(NULL, 0);
        if (decRc != PNG_SUCCESS) {
          DBG_WARN("PNG decode failed rc=%d", decRc);
        }
        png.close();
#if DEBUG_LEVEL >= 4
        DBG_VERBOSE("Tile %d/%d overlay decoded | bytes=%u | %lums",
                    tileIndex, renderTilesTotal, (unsigned)overlayLen, millis() - decodeStart);
#endif
      } else {
        DBG_WARN("Overlay PNG open failed rc=%d", rc);
      }
      free(overlayBuf);
    }

  } else {
    if (overlayStatus == HTTP_CODE_UNAUTHORIZED && targetLayer != 0) {
      owmAuthFailed = true;
      DBG_ERROR("OpenWeatherMap overlay unauthorized (401) — check SECRET_OWM_API_KEY");
    } else {
      DBG_WARN("Overlay fetch failed: %s", safeLayerUrl.c_str());
    }
  }
}

      renderTilesDone += 1;
      renderDiagLastProgressMs = millis();
      DBG_VERBOSE("Tile %d/%d done | ok=%d err=%d | heap=%u largest=%u psram=%u stackHW=%u",
                  renderTilesDone, renderTilesTotal, tilesOk, tilesErr,
                  ESP.getFreeHeap(), largestInternalBlock(), ESP.getFreePsram(),
                  renderStackHighWater());
      logRenderProgressSummary(targetLayer, tilesOk, tilesErr, renderStart);
      delay(50);  // yield between tiles — lets FreeRTOS process deferred heap cleanup
    }
  }

  setRenderDiagPhase("marker");
  drawLocationMarker();
#if DEBUG_LEVEL >= 4
  DBG_VERBOSE("Map render done | layer=%s zoom=%d map=%s | %d ok %d err | %lums | heap=%u",
              layerNames[targetLayer], targetZoom, mapNames[targetMapStyle], tilesOk, tilesErr,
              millis() - renderStart, ESP.getFreeHeap());
#else
  DBG_INFO("Map render done | layer=%s zoom=%d map=%s | %d ok %d err | %lums",
           layerNames[targetLayer], targetZoom, mapNames[targetMapStyle], tilesOk, tilesErr,
           millis() - renderStart);
#endif
  clearRenderDiag();
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
  markScreenUpdated();
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
  markScreenUpdated();
}

// ---------------------------------------------------------------------------
// drawMapBadges — map-style and layer-name overlay badges on the LCD.
// Must only be called from Core 0 after a sprite push.
// ---------------------------------------------------------------------------
void drawMapBadges() {
  int mapX = 740, mapY = 387, mapW = 60, mapH = 22;
  lcd.fillRect(mapX, mapY, mapW, mapH, panelColor);
  lcd.drawRect(mapX, mapY, mapW, mapH, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_center);
  lcd.drawString(mapNames[mapStyle], mapX + mapW / 2, mapY + mapH / 2);

  int layerX = 348, layerY = 4, layerW = 104, layerH = 40;
  lcd.fillRect(layerX, layerY, layerW, layerH, panelColor);
  lcd.drawRect(layerX, layerY, layerW, layerH, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_center);
  lcd.drawString(layerNames[layerStyle], layerX + layerW / 2, layerY + 12);

  char ageLabel[16];
  formatLayerAgeLabel(layerStyle, ageLabel, sizeof(ageLabel));
  lcd.setTextColor(layerAgeColor(layerStyle));
  lcd.drawString(ageLabel, layerX + layerW / 2, layerY + 28);
  markScreenUpdated();
}

// ---------------------------------------------------------------------------
// triggerRenderForLayer — request a background render only when the cache is
// missing/stale/forced. Fresh layer caches are pushed by the main loop without
// touching network APIs.
// ---------------------------------------------------------------------------
void triggerRenderForLayer(int targetLayer, bool forceRefresh) {
  if (renderTaskHandle == nullptr) {
    DBG_ERROR("Render request ignored — RenderTask is not available");
    return;
  }

  if (targetLayer < 0 || targetLayer > 2) targetLayer = 0;

  if (!forceRefresh && layerCacheFresh(targetLayer)) {
    mapFront = layerCacheSprites[targetLayer];
    DBG_INFO("Layer cache hit | layer=%s age=%lus | zoom=%d map=%s",
             layerNames[targetLayer], layerCacheAgeSecs(targetLayer), myZoom, mapNames[mapStyle]);

    if (renderState == RENDER_BUSY) {
      if (firstRenderDone && appState == 0) {
        lcd.fillScreen(TFT_BLACK);
        mapFront->pushSprite(0, 0);
        drawMapBadges();
        drawTopDate();
        drawBottomDashboard();
        drawSideButtons();
        drawSignature();
        drawProgressTimer();
      }
      return;
    }

    renderState = RENDER_READY;
    return;
  }

  if (renderState == RENDER_BUSY) {
      renderPending = true;
      pendingLayerStyle = targetLayer;
      pendingRenderForce = forceRefresh;
      DBG_INFO("Render queued | layer=%s force=%d", layerNames[targetLayer], forceRefresh ? 1 : 0);
      updateRenderStatusOverlay(true);
      return;
    }
  renderTarget = &renderScratch;
  renderLayerStyle = targetLayer;
  renderMapStyle = mapStyle;
  renderZoom = myZoom;
  renderState = RENDER_BUSY;
#if DEBUG_LEVEL >= 4
  DBG_VERBOSE("Render scheduled | layer=%s force=%d cacheValid=%d cacheAge=%lus refresh=%ds",
              layerNames[targetLayer], forceRefresh ? 1 : 0,
              layerCaches[targetLayer].valid ? 1 : 0,
              layerCacheAgeSecs(targetLayer),
              cfg::kRealtimeRefreshSecs);
#else
  DBG_INFO("Render scheduled | layer=%s force=%d",
           layerNames[targetLayer], forceRefresh ? 1 : 0);
#endif
  updateRenderStatusOverlay(true);
  xTaskNotifyGive(renderTaskHandle);
}

// ---------------------------------------------------------------------------
// drawStartupScreen — full-screen info panel shown while first render loads.
// ---------------------------------------------------------------------------
void drawStartupScreen(bool wifiOk, const char* ssid, const char* ip) {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(middle_center);

  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(3);
  lcd.drawString("ESP32S3-Weather  v" + String(kFwVersion), 400, 55);

  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextSize(2);
  lcd.drawString(cfg::kLocationName, 400, 105);

  if (wifiOk) {
    lcd.setTextColor(TFT_GREEN);
    char wBuf[80];
    snprintf(wBuf, sizeof(wBuf), "WiFi: %s   %s", ssid, ip);
    lcd.drawString(wBuf, 400, 150);
  } else {
    lcd.setTextColor(TFT_RED);
    lcd.drawString("WiFi: not connected", 400, 150);
  }

  lcd.setTextColor(TFT_LIGHTGREY);
  lcd.drawString(cfg::kNtpTimezone, 400, 190);

  char mBuf[64];
  snprintf(mBuf, sizeof(mBuf), "PSRAM: %uKB free   Heap: %uKB free",
           ESP.getFreePsram() / 1024, ESP.getFreeHeap() / 1024);
  lcd.drawString(mBuf, 400, 230);

  lcd.setTextColor(strlen(owmApiKey) > 0 ? TFT_GREEN : TFT_DARKGREY);
  lcd.drawString(strlen(owmApiKey) > 0 ? "OWM: key present" : "OWM: no key  (RADAR only)", 400, 270);

  char cBuf[72];
  snprintf(cBuf, sizeof(cBuf), "Layer cycle: %ds   Realtime: %dmin",
           cfg::kLayerCycleSecs, cfg::kRealtimeRefreshSecs / 60);
  lcd.setTextColor(TFT_DARKGREY);
  lcd.drawString(cBuf, 400, 310);

  lcd.setTextColor(TFT_CYAN);
  lcd.setTextSize(3);
  lcd.drawString("Loading map...", 400, 390);
  markScreenUpdated();
}

// ---------------------------------------------------------------------------
// updateLoadingProgress — draws progress bar on startup screen.
// Called from loop() while !firstRenderDone. Redraws only when tile count
// changes to avoid display bus thrash.
// ---------------------------------------------------------------------------
void updateLoadingProgress() {
  static int lastDrawn = -1;
  int done  = renderTilesDone;
  int total = renderTilesTotal;
  if (done == lastDrawn) return;
  lastDrawn = done;

  constexpr int barX = 80;
  constexpr int barY = 420;
  constexpr int barW = 640;
  constexpr int barH = 18;

  lcd.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
  if (total > 0) {
    int filled = (int)((long)done * barW / total);
    if (filled > 0) lcd.fillRect(barX, barY, filled, barH, TFT_CYAN);
    if (filled < barW) lcd.fillRect(barX + filled, barY, barW - filled, barH, TFT_BLACK);
  }

  lcd.setTextDatum(middle_center);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextSize(1);
  char buf[32];
  if (total > 0) snprintf(buf, sizeof(buf), "%d / %d tiles", done, total);
  else           snprintf(buf, sizeof(buf), "Fetching tiles...");
  lcd.drawString(buf, 400, 448);
  markScreenUpdated();
}

void updateRenderStatusOverlay(bool force) {
  static int lastDone = -1;
  static int lastTotal = -1;
  static unsigned long lastDrawMs = 0;

  if (!firstRenderDone || appState != 0 || renderState != RENDER_BUSY) {
    lastDone = -1;
    lastTotal = -1;
    lastDrawMs = 0;
    return;
  }

  int done = renderTilesDone;
  int total = renderTilesTotal;
  unsigned long now = millis();
  if (!force && done == lastDone && total == lastTotal && now - lastDrawMs < 1000) return;

  lastDone = done;
  lastTotal = total;
  lastDrawMs = now;

  constexpr int x = 245;
  constexpr int y = 170;
  constexpr int w = 310;
  constexpr int h = 96;
  constexpr int barX = x + 22;
  constexpr int barY = y + 66;
  constexpr int barW = w - 44;
  constexpr int barH = 12;

  lcd.fillRect(x, y, w, h, panelColor);
  lcd.drawRect(x, y, w, h, TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextSize(2);
  lcd.drawString("Loading map", x + w / 2, y + 20);

  char status[64];
  if (total > 0) {
    snprintf(status, sizeof(status), "%s  zoom %d  %d/%d",
             layerNames[renderLayerStyle], renderZoom, done, total);
  } else {
    snprintf(status, sizeof(status), "%s  zoom %d  starting",
             layerNames[renderLayerStyle], renderZoom);
  }

  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.drawString(status, x + w / 2, y + 47);

  lcd.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
  int fillW = 0;
  if (total > 0) fillW = (int)((long)done * (barW - 2) / total);
  if (fillW > 0) lcd.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_CYAN);
  if (fillW < barW - 2) {
    lcd.fillRect(barX + 1 + fillW, barY + 1, (barW - 2) - fillW, barH - 2, TFT_BLACK);
  }

  markScreenUpdated();
}

// ---------------------------------------------------------------------------
// logStartupBanner — serial summary printed once at end of setup().
// ---------------------------------------------------------------------------
void logStartupBanner(bool wifiOk, const char* ip) {
  DBG_INFO("=== ESP32S3-Weather v%s ===========================", kFwVersion);
  DBG_INFO("Location  : %s (%.4f, %.4f)",
           cfg::kLocationName, cfg::kLocationLatitude, cfg::kLocationLongitude);
  if (wifiOk) {
    DBG_INFO("WiFi      : connected | SSID: %s | IP: %s", WiFi.SSID().c_str(), ip);
    DBG_INFO("NTP       : configured | TZ: %s", cfg::kNtpTimezone);
  } else {
    DBG_INFO("WiFi      : NOT connected");
  }
  DBG_INFO("PSRAM free: %u bytes", ESP.getFreePsram());
  DBG_INFO("Heap free : %u bytes", ESP.getFreeHeap());
  DBG_INFO("OWM key   : %s | Layer cycle: %ds | Realtime refresh: %ds",
           strlen(owmApiKey) > 0 ? "present" : "absent (RADAR only)",
           cfg::kLayerCycleSecs, cfg::kRealtimeRefreshSecs);
  DBG_INFO("============================================================");
}

// ---------------------------------------------------------------------------
// pollRenderWatchdog — Core 0/loop-side visibility into a stuck render task.
// It cannot interrupt a blocking TLS/HTTP call, but it tells us exactly which
// phase and URL were active when progress stopped.
// ---------------------------------------------------------------------------
void pollRenderWatchdog() {
  static unsigned long lastWarnMs = 0;

  if (renderState != RENDER_BUSY) {
    lastWarnMs = 0;
    return;
  }

  unsigned long now = millis();
  unsigned long phaseAge = now - renderDiagPhaseStartedMs;
  unsigned long progressAge = now - renderDiagLastProgressMs;

  if (phaseAge < cfg::kRenderStallWarnMs && progressAge < cfg::kRenderStallWarnMs) return;
  if (lastWarnMs != 0 && now - lastWarnMs < cfg::kRenderStallRepeatMs) return;
  lastWarnMs = now;

  char phase[sizeof(renderDiagPhase)];
  char url[sizeof(renderDiagUrl)];
  strlcpy(phase, renderDiagPhase, sizeof(phase));
  strlcpy(url, renderDiagUrl, sizeof(url));

  DBG_WARN("Render busy %lums | phase=%s age=%lums | tile=%d/%d x=%d y=%d | heap=%u largest=%u minHeap=%u psram=%u psramLargest=%u stackHW=%u | %s",
           progressAge, phase, phaseAge,
           renderDiagTileIndex, renderTilesTotal, renderDiagTileX, renderDiagTileY,
           ESP.getFreeHeap(), largestInternalBlock(), ESP.getMinFreeHeap(),
           ESP.getFreePsram(), largestPsramBlock(), renderStackHighWater(),
           url[0] ? url : "<none>");
}

// ---------------------------------------------------------------------------
// renderTaskFn — Core 1 task. Waits for notification, renders into the scratch
// sprite, copies it into the target layer cache, then signals READY. It never
// touches the display bus directly.
// ---------------------------------------------------------------------------
void renderTaskFn(void*) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (weatherRefreshPending) {
      weatherRefreshPending = false;
      getWeatherData();
    }
    int completedLayer = renderLayerStyle;
    DBG_VERBOSE("RenderTask wake | core=%d | stackHW=%u | heap=%u largest=%u",
                xPortGetCoreID(), renderStackHighWater(), ESP.getFreeHeap(), largestInternalBlock());
    renderRadarMap();
    setRenderDiagPhase("cache_copy");
    renderScratch.pushSprite(layerCacheSprites[completedLayer], 0, 0);
    markLayerCacheUpdated(completedLayer);
    if (completedLayer == layerStyle) {
      mapFront = layerCacheSprites[completedLayer];
    }
    renderState = RENDER_READY;
#if DEBUG_LEVEL >= 4
    DBG_VERBOSE("RenderTask ready | layer=%s cacheAge=%lus | stackHW=%u | heap=%u largest=%u",
                layerNames[completedLayer], layerCacheAgeSecs(completedLayer),
                renderStackHighWater(), ESP.getFreeHeap(), largestInternalBlock());
#else
    DBG_INFO("Render ready | layer=%s cacheAge=%lus",
             layerNames[completedLayer], layerCacheAgeSecs(completedLayer));
#endif
  }
}

// ---------------------------------------------------------------------------

bool handleUiTouch(int tx, int ty, bool debounce) {
  if (appState == 0 && tx >= 0 && tx <= 32 && ty >= 4 && ty <= 413) {
    setBrightnessFromTouchY(ty);
    drawProgressTimer();
    return true;
  }

  if (debounce && millis() - lastUiTouchMs <= 600) return false;

  bool handled = false;

  if (appState == 0) {
    // Layer toggle (top centre strip)
    if (ty > 0 && ty < 40 && tx > 300 && tx < 500) {
      layerStyle = (layerStyle + 1) % 3;
      if (layerStyle != 0 && (owmAuthFailed || strlen(owmApiKey) == 0)) layerStyle = 0;
      layerCycleLastMs = millis();
      triggerRenderForLayer(layerStyle, false);
      drawMapBadges();
      handled = true;
    }
    // Map style toggle (bottom centre strip)
    else if (tx > 310 && tx < 490 && ty > 385 && ty < 480) {
      mapStyle = (mapStyle + 1) % 3;
      invalidateLayerCaches();
      layerCycleLastMs = millis();
      triggerRenderForLayer(layerStyle, true);
      drawMapBadges();
      handled = true;
    }
    // Right side graph buttons
    else if (tx > 760) {
      if      (ty > 50  && ty < 125) appState = 1, drawGraphPage(1), handled = true;
      else if (ty > 135 && ty < 210) appState = 2, drawGraphPage(2), handled = true;
      else if (ty > 220 && ty < 295) appState = 3, drawGraphPage(3), handled = true;
      else if (ty > 305 && ty < 380) appState = 4, drawGraphPage(4), handled = true;
    }
    // Left side graph buttons
    else if (tx > 32 && tx < 66) {
      if      (ty > 50  && ty < 125) appState = 5, drawGraphPage(5), handled = true;
      else if (ty > 135 && ty < 210) appState = 6, drawGraphPage(6), handled = true;
      else if (ty > 220 && ty < 295) appState = 7, drawGraphPage(7), handled = true;
      else if (ty > 305 && ty < 380) appState = 8, drawGraphPage(8), handled = true;
    }
    // Zoom (centre map area)
    else if (tx > 250 && tx < 550 && ty > 100 && ty < 380) {
      myZoom++;
      if (myZoom > cfg::kMapZoomMax) myZoom = cfg::kMapZoomMin;
      DBG_INFO("Zoom → %d", myZoom);
      invalidateLayerCaches();
      layerCycleLastMs = millis();
      triggerRenderForLayer(layerStyle, true);
      drawMapBadges();
      handled = true;
    }
  }
  // Back from graph page
  else if (tx < 150 && ty > 400) {
    appState = 0;
    lcd.fillScreen(TFT_BLACK);
    mapFront->pushSprite(0, 0);
    drawMapBadges();
    drawBottomDashboard();
    drawSideButtons();
    drawSignature();
    drawTopDate();
    handled = true;
  }

  if (handled) lastUiTouchMs = millis();
  return handled;
}

// ---------------------------------------------------------------------------
// Sleep schedule helpers
// ---------------------------------------------------------------------------

void loadSleepSettings() {
  prefs.begin("sleepsch", true);
  sleepScheduleEnabled  = prefs.getBool("enabled",  cfg::kSleepScheduleEnabled);
  sleepOnHour           = prefs.getInt("onHour",    cfg::kSleepOnHour);
  sleepOnMinute         = prefs.getInt("onMin",     cfg::kSleepOnMinute);
  sleepOffHour          = prefs.getInt("offHour",   cfg::kSleepOffHour);
  sleepOffMinute        = prefs.getInt("offMin",    cfg::kSleepOffMinute);
  sleepWakeDurationSecs = prefs.getInt("wakeSecs",  cfg::kSleepWakeDurationSecs);
  prefs.end();
  DBG_INFO("Sleep settings loaded | enabled=%d on=%02d:%02d off=%02d:%02d wake=%ds",
           sleepScheduleEnabled, sleepOnHour, sleepOnMinute,
           sleepOffHour, sleepOffMinute, sleepWakeDurationSecs);
}

void saveSleepSettings() {
  prefs.begin("sleepsch", false);
  prefs.putBool("enabled",  sleepScheduleEnabled);
  prefs.putInt("onHour",    sleepOnHour);
  prefs.putInt("onMin",     sleepOnMinute);
  prefs.putInt("offHour",   sleepOffHour);
  prefs.putInt("offMin",    sleepOffMinute);
  prefs.putInt("wakeSecs",  sleepWakeDurationSecs);
  prefs.end();
}

bool isInSleepWindow() {
  struct tm ti;
  if (!getLocalTime(&ti)) return false;
  int nowMins = ti.tm_hour * 60 + ti.tm_min;
  int onMins  = sleepOnHour  * 60 + sleepOnMinute;
  int offMins = sleepOffHour * 60 + sleepOffMinute;
  if (onMins >= offMins) {
    return nowMins >= onMins || nowMins < offMins;  // window crosses midnight
  }
  return nowMins >= onMins && nowMins < offMins;    // same-day window
}

void drawSleepScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(lcd.color565(40, 40, 40));
  lcd.setTextSize(2);
  lcd.setTextDatum(middle_center);
  lcd.drawString("In Sleep mode", cfg::kScreenWidth / 2, cfg::kScreenHeight / 2 - 16);
  lcd.drawString("touch to wake up", cfg::kScreenWidth / 2, cfg::kScreenHeight / 2 + 16);
  markScreenUpdated();
}

void exitSleepRestoreDashboard() {
  appState = 0;
  if (firstRenderDone && mapFront) {
    lcd.fillScreen(TFT_BLACK);
    mapFront->pushSprite(0, 0);
    drawMapBadges();
    drawTopDate();
    drawBottomDashboard();
    drawSideButtons();
    drawSignature();
    drawProgressTimer();
  }
  markScreenUpdated();
}

void pollSleepSchedule() {
  if (!firstRenderDone) return;

  if (!sleepScheduleEnabled) {
    if (sleepPhase != SLEEP_AWAKE) {
      if (sleepPhase == SLEEP_DARK) setBacklightBrightness(brightnessLevel);
      exitSleepRestoreDashboard();
      sleepPhase = SLEEP_AWAKE;
    }
    return;
  }

  bool inWindow = isInSleepWindow();
  unsigned long now = millis();

  switch (sleepPhase) {
    case SLEEP_AWAKE:
      if (inWindow) {
        drawSleepScreen();
        sleepPhase   = SLEEP_PENDING;
        sleepPhaseMs = now;
        DBG_INFO("Sleep: pending (showing message, BL off in 2s)");
      }
      break;

    case SLEEP_PENDING:
      if (!inWindow) {
        exitSleepRestoreDashboard();
        sleepPhase = SLEEP_AWAKE;
      } else if (now - sleepPhaseMs >= 2000) {
        setBacklightBrightness(0);
        sleepPhase = SLEEP_DARK;
        DBG_INFO("Sleep: backlight off");
      }
      break;

    case SLEEP_DARK:
      if (!inWindow) {
        setBacklightBrightness(brightnessLevel);
        exitSleepRestoreDashboard();
        sleepPhase = SLEEP_AWAKE;
        DBG_INFO("Sleep: window ended, waking");
      }
      // Physical touch wakes handled in loop(); WebUI touch in handleWebTouch().
      break;

    case SLEEP_TOUCH_WOKEN:
      if (!inWindow) {
        sleepPhase   = SLEEP_AWAKE;
        sleepPhaseMs = 0;
      } else if (now - sleepPhaseMs >= (unsigned long)sleepWakeDurationSecs * 1000UL) {
        drawSleepScreen();
        sleepPhase   = SLEEP_PENDING;
        sleepPhaseMs = now;
        DBG_INFO("Sleep: touch-wake timeout, re-entering pending");
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  initBoardHardware(0);
  lcd.init();
  lcd.setRotation(0);
  lcd.setColorDepth(16);
  touch_init();
  setBacklightBrightness(brightnessLevel);

  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(3);
  lcd.setTextDatum(middle_center);
  lcd.drawString("Connecting...", 400, 240);
  markScreenUpdated();

  loadSleepSettings();

  bool wifiOk = initWiFi();
  String ip = "";
  if (wifiOk) {
    configTzTime(cfg::kNtpTimezone, ntpServer);
    ip = WiFi.localIP().toString();
  }
  setupOta(wifiOk);
  setupWebUi(wifiOk);

  renderScratch.setPsram(true);
  cacheRadar.setPsram(true);
  cacheClouds.setPsram(true);
  cacheRain.setPsram(true);
  if (!renderScratch.createSprite(cfg::kScreenWidth, kMapCanvasHeight)) DBG_ERROR("renderScratch alloc failed");
  if (!cacheRadar.createSprite(cfg::kScreenWidth, kMapCanvasHeight)) DBG_ERROR("cacheRadar alloc failed");
  if (!cacheClouds.createSprite(cfg::kScreenWidth, kMapCanvasHeight)) DBG_ERROR("cacheClouds alloc failed");
  if (!cacheRain.createSprite(cfg::kScreenWidth, kMapCanvasHeight)) DBG_ERROR("cacheRain alloc failed");
  DBG_INFO("Layer cache sprites allocated | PSRAM free=%u largest=%u",
           ESP.getFreePsram(), largestPsramBlock());

  drawStartupScreen(wifiOk, WiFi.SSID().c_str(), ip.c_str());

  lastUpdate = millis();
  getWeatherData();

  // Update startup screen status now that weather is done and map tiles are next.
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(TFT_BLACK, TFT_BLACK);
  lcd.setTextSize(3);
  lcd.drawString("Loading map...", 400, 390);  // erase old text
  lcd.setTextColor(TFT_CYAN);
  lcd.drawString("Fetching map tiles...", 400, 390);
  markScreenUpdated();

  BaseType_t renderTaskOk = xTaskCreatePinnedToCore(
    renderTaskFn,
    "RenderTask",
    cfg::kRenderTaskStackBytes,
    nullptr,
    cfg::kRenderTaskPriority,
    &renderTaskHandle,
    1);

  if (renderTaskOk != pdPASS || renderTaskHandle == nullptr) {
    DBG_ERROR("RenderTask create failed | rc=%d | heap=%u largest=%u",
              (int)renderTaskOk, ESP.getFreeHeap(), largestInternalBlock());
  } else {
    DBG_INFO("RenderTask created | stack=%u priority=%u core=1 | heap=%u largest=%u",
             (unsigned)cfg::kRenderTaskStackBytes,
             (unsigned)cfg::kRenderTaskPriority,
             ESP.getFreeHeap(), largestInternalBlock());
  }

  layerCycleLastMs = millis();
  realtimeRefreshLastMs = millis();
  lastUpdate = realtimeRefreshLastMs;
  triggerRenderForLayer(layerStyle, false);

  logStartupBanner(wifiOk, ip.c_str());
}

void loop() {
  static unsigned long lT = 0;
  static unsigned long lastSleepCheckMs = 0;
  struct tm ti;

  handleWebUiClient();

  if (WiFi.isConnected()) {
    ArduinoOTA.handle();
  }
  if (otaInProgress) {
    delay(1);
    return;
  }

  // --- Flip completed background render to screen ---
  if (renderState == RENDER_READY) {
    renderState    = RENDER_IDLE;
    firstRenderDone = true;
    if (sleepPhase == SLEEP_AWAKE || sleepPhase == SLEEP_TOUCH_WOKEN) {
      lcd.fillScreen(TFT_BLACK);
      mapFront->pushSprite(0, 0);
      drawMapBadges();
      drawTopDate();
      drawBottomDashboard();
      drawSideButtons();
      drawSignature();
      drawProgressTimer();
      markScreenUpdated();
    }
    if (renderPending) {
      int queuedLayer = pendingLayerStyle;
      bool queuedForce = pendingRenderForce;
      renderPending = false;
      pendingRenderForce = false;
      triggerRenderForLayer(queuedLayer, queuedForce);
    }
  }

  if (!firstRenderDone) {
    updateLoadingProgress();
    pollRenderWatchdog();
    delay(10);  // yield to RenderTask while the startup screen is waiting
    return;
  }

  // --- Night sleep schedule (check once per second) ---
  if (millis() - lastSleepCheckMs >= 1000) {
    lastSleepCheckMs = millis();
    pollSleepSchedule();
  }

  // The rest of loop() only runs when the display is showing normally.
  if (sleepPhase == SLEEP_DARK || sleepPhase == SLEEP_PENDING) {
    // Only handle touch to wake when the display is off.
    if (sleepPhase == SLEEP_DARK && touch_has_signal() && touch_touched()) {
      setBacklightBrightness(brightnessLevel);
      exitSleepRestoreDashboard();
      sleepPhase   = SLEEP_TOUCH_WOKEN;
      sleepPhaseMs = millis();
      DBG_INFO("Sleep: woken by touch | re-sleep in %ds", sleepWakeDurationSecs);
      delay(200);
    }
    return;
  }

  updateRenderStatusOverlay(false);
  pollRenderWatchdog();

  // --- Per-minute dashboard update ---
  if (appState == 0 && getLocalTime(&ti)) {
    if (ti.tm_min != lastMinute) {
      lastMinute = ti.tm_min;
      drawBottomDashboard();
      drawSignature();
    }
  }

  // --- Progress timer every 5 s ---
  if (appState == 0 && millis() - lT > 5000) {
    drawProgressTimer();
    drawMapBadges();
    lT = millis();
  }

  // --- Layer auto-cycle ---
  if (appState == 0 &&
      millis() - layerCycleLastMs >= (unsigned long)cfg::kLayerCycleSecs * 1000UL) {
    layerStyle = (layerStyle + 1) % 3;
    if (layerStyle != 0 && (owmAuthFailed || strlen(owmApiKey) == 0)) layerStyle = 0;
    layerCycleLastMs = millis();
    DBG_INFO("Layer auto → %s", layerNames[layerStyle]);
    triggerRenderForLayer(layerStyle, false);
    drawMapBadges();
  }

  // --- Touch ---
  if (touch_has_signal() && touch_touched()) {
    if (handleUiTouch(touch_last_x, touch_last_y, true)) {
      delay(25);
    }
  }

  // --- Realtime weather + map refresh ---
  if (appState == 0 &&
      millis() - realtimeRefreshLastMs > (unsigned long)cfg::kRealtimeRefreshSecs * 1000UL) {
    realtimeRefreshLastMs = millis();
    lastUpdate = realtimeRefreshLastMs;
    DBG_INFO("Realtime refresh due | interval=%ds | scheduling weather+render on Core 1",
             cfg::kRealtimeRefreshSecs);
    weatherRefreshPending = true;
    triggerRenderForLayer(layerStyle, true);
    drawMapBadges();
  }
}
