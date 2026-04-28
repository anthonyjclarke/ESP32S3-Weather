#pragma once

#include <Arduino.h>

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 3
#endif

#if DEBUG_LEVEL >= 1
#define DBG_ERROR(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_ERROR(fmt, ...) do {} while (0)
#endif

#if DEBUG_LEVEL >= 2
#define DBG_WARN(fmt, ...) Serial.printf("[WARN]  " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_WARN(fmt, ...) do {} while (0)
#endif

#if DEBUG_LEVEL >= 3
#define DBG_INFO(fmt, ...) Serial.printf("[INFO]  " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_INFO(fmt, ...) do {} while (0)
#endif

#if DEBUG_LEVEL >= 4
#define DBG_VERBOSE(fmt, ...) Serial.printf("[VERB]  " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_VERBOSE(fmt, ...) do {} while (0)
#endif
