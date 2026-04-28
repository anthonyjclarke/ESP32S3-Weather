#include "touch.h"

#include <Wire.h>
#include "config.h"
#include "debug.h"

void resetTouchController();

int touch_last_x = 0;
int touch_last_y = 0;

static bool touchReady = false;
static bool touchActive = false;
static uint32_t lastPollMs = 0;

static bool gt911WriteReg(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool gt911ReadBytes(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, len) != len) return false;
  for (size_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

static void transformTouch(uint16_t rawX, uint16_t rawY) {
  int x = rawX;
  int y = rawY;

  if (TOUCH_SWAP_XY) {
    int tmp = x;
    x = y;
    y = tmp;
  }
  if (TOUCH_INVERT_X) x = cfg::kScreenWidth - 1 - x;
  if (TOUCH_INVERT_Y) y = cfg::kScreenHeight - 1 - y;

  touch_last_x = constrain(x, 0, cfg::kScreenWidth - 1);
  touch_last_y = constrain(y, 0, cfg::kScreenHeight - 1);
}

static void pollTouch() {
  uint32_t now = millis();
  if (now - lastPollMs < 20) return;
  lastPollMs = now;

  uint8_t status = 0;
  if (!gt911ReadBytes(0x814E, &status, 1)) {
    touchActive = false;
    return;
  }

  if (!(status & 0x80)) return;

  uint8_t count = status & 0x0F;
  if (count > 0) {
    uint8_t point[8] = {};
    if (gt911ReadBytes(0x8150, point, sizeof(point))) {
      uint16_t x = point[0] | (point[1] << 8);
      uint16_t y = point[2] | (point[3] << 8);
      transformTouch(x, y);
      touchActive = true;
    }
  } else {
    touchActive = false;
  }

  gt911WriteReg(0x814E, 0x00);
}

void touch_init() {
  resetTouchController();

  uint8_t id[4] = {};
  touchReady = gt911ReadBytes(0x8140, id, sizeof(id));
  if (touchReady) {
    DBG_INFO("GT911 touch OK: %c%c%c", id[0], id[1], id[2]);
  } else {
    DBG_WARN("GT911 touch not responding at 0x%02X", TOUCH_I2C_ADDR);
  }
}

bool touch_has_signal() {
  pollTouch();
  return touchReady;
}

bool touch_touched() {
  pollTouch();
  return touchActive;
}
