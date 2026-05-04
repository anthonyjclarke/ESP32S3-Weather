#pragma once

#include <Arduino.h>
#include "config.h"

void initBoardHardware(uint8_t brightness);
void setBacklightBrightness(uint8_t brightness);

#ifdef CFG_BACKLIGHT_PWM_ENABLED
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
// Mutex protecting the shared Wire bus between the PWM task (Core 0) and
// touch polling (Core 1). Taken automatically by both paths when PWM is enabled.
extern SemaphoreHandle_t gWireMutex;
// Start the 100 Hz PWM FreeRTOS task. Must be called BEFORE touch_init() — touch
// polling takes gWireMutex, so the mutex must exist first.
void startBacklightPwm();
// Set backlight to a proportional brightness (0=off, 255=full, 1–254=PWM dim).
// Use only where dimming is explicitly wanted; normal brightness remains steady
// on/off through setBacklightBrightness().
void setBacklightDim(uint8_t brightness);
#endif
