#include "display_hw.h"

#include <Wire.h>
#include "config.h"
#include "debug.h"

static constexpr uint8_t CH422G_ADDR_WR_SET = 0x24;
static constexpr uint8_t CH422G_ADDR_WR_IO  = 0x38;

static uint8_t ch422gWrIo = 0xFF;

static void ch422gSetPin(uint8_t pin, uint8_t val) {
  if (val) {
    ch422gWrIo |= (1u << pin);
  } else {
    ch422gWrIo &= ~(1u << pin);
  }

  Wire.beginTransmission(CH422G_ADDR_WR_IO);
  Wire.write(ch422gWrIo);
  Wire.endTransmission();
}

// ---------------------------------------------------------------------------
// Software PWM — only compiled when CFG_BACKLIGHT_PWM_ENABLED is defined.
// To remove: comment out #define CFG_BACKLIGHT_PWM_ENABLED in config.h.
// ---------------------------------------------------------------------------
#ifdef CFG_BACKLIGHT_PWM_ENABLED

SemaphoreHandle_t gWireMutex  = nullptr;
static volatile uint8_t sPwmBrightness = 255;
static TaskHandle_t     sPwmTask       = nullptr;

// 100 Hz PWM task running on Core 0. Holds pin steady at 0 or 1 when brightness
// is 0 or 255 respectively; otherwise toggles at 100 Hz with proportional duty.
static void backlightPwmTask(void*) {
  uint8_t lastSteadyB = 0;
  bool steadyApplied = false;

  while (true) {
    uint8_t b = sPwmBrightness;

    if (b == 0) {
      if (!steadyApplied || lastSteadyB != 0) {
        if (xSemaphoreTake(gWireMutex, portMAX_DELAY)) {
          ch422gSetPin(CH422G_LCD_BL, 0);
          xSemaphoreGive(gWireMutex);
        }
        lastSteadyB = 0;
        steadyApplied = true;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    if (b == 255) {
      if (!steadyApplied || lastSteadyB != 255) {
        if (xSemaphoreTake(gWireMutex, portMAX_DELAY)) {
          ch422gSetPin(CH422G_LCD_BL, 1);
          xSemaphoreGive(gWireMutex);
        }
        lastSteadyB = 255;
        steadyApplied = true;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    // Proportional PWM: 10 ms period (100 Hz), 1 ms tick resolution.
    // Effective duty steps: 10%, 20%, …, 90%. Fine enough for night dimming.
    steadyApplied = false;
    constexpr uint32_t kPeriodMs = 10;
    uint32_t onMs = (kPeriodMs * b + 127u) / 255u;
    if (onMs < 1) onMs = 1;
    if (onMs >= kPeriodMs) onMs = kPeriodMs - 1;
    uint32_t offMs = kPeriodMs - onMs;

    if (xSemaphoreTake(gWireMutex, portMAX_DELAY)) {
      ch422gSetPin(CH422G_LCD_BL, 1);
      xSemaphoreGive(gWireMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(onMs));

    if (xSemaphoreTake(gWireMutex, portMAX_DELAY)) {
      ch422gSetPin(CH422G_LCD_BL, 0);
      xSemaphoreGive(gWireMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(offMs));
  }
}

void startBacklightPwm() {
  gWireMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(backlightPwmTask, "bl_pwm", 2048, nullptr, 2, &sPwmTask, 0);
  DBG_INFO("Backlight PWM task started (100 Hz, Core 0)");
}

void setBacklightDim(uint8_t brightness) {
  sPwmBrightness = brightness;
}

#endif // CFG_BACKLIGHT_PWM_ENABLED
// ---------------------------------------------------------------------------

void initBoardHardware(uint8_t brightness) {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  Wire.beginTransmission(CH422G_ADDR_WR_SET);
  Wire.write(0x01);
  Wire.endTransmission();

  Wire.beginTransmission(CH422G_ADDR_WR_IO);
  Wire.write(ch422gWrIo);
  Wire.endTransmission();

  ch422gSetPin(CH422G_LCD_BL, 0);
  ch422gSetPin(CH422G_LCD_RST, 0);
  delay(10);
  ch422gSetPin(CH422G_LCD_RST, 1);
  delay(100);

  setBacklightBrightness(brightness);
}

void setBacklightBrightness(uint8_t brightness) {
#ifdef CFG_BACKLIGHT_PWM_ENABLED
  // Normal display brightness is steady on/off on this CH422G-controlled board.
  // Software PWM is reserved for explicit sleep dimming via setBacklightDim().
  sPwmBrightness = (brightness > 0) ? 255 : 0;
#else
  ch422gSetPin(CH422G_LCD_BL, brightness > 0 ? 1 : 0);
#endif
}

void resetTouchController() {
  pinMode(GT911_INT_PIN, OUTPUT);
  digitalWrite(GT911_INT_PIN, LOW);
  delay(10);

  ch422gSetPin(CH422G_TP_RST, 0);
  delay(100);
  ch422gSetPin(CH422G_TP_RST, 1);
  delay(200);

  pinMode(GT911_INT_PIN, INPUT);
}
