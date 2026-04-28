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
  ch422gSetPin(CH422G_LCD_BL, brightness > 0 ? 1 : 0);
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
