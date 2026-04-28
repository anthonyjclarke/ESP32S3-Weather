#pragma once

#include <Arduino.h>

extern int touch_last_x;
extern int touch_last_y;

void touch_init();
bool touch_has_signal();
bool touch_touched();
