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
// Start the 100 Hz PWM FreeRTOS task. Call once at end of setup(), after touch_init().
void startBacklightPwm();
// Set backlight to a proportional brightness (0=off, 255=full, 1–254=PWM dim).
// Unlike setBacklightBrightness(), this uses actual duty-cycle control.
void setBacklightDim(uint8_t brightness);
#endif
