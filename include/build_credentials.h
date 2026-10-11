#pragma once
// Public installer builds ignore local secrets even when the file exists.
#if !defined(WEATHER_PUBLIC_BUILD) && __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef SECRET_WIFI_SSID
#define SECRET_WIFI_SSID ""
#endif
#ifndef SECRET_WIFI_PASS
#define SECRET_WIFI_PASS ""
#endif
#ifndef SECRET_OWM_API_KEY
#define SECRET_OWM_API_KEY ""
#endif
#ifndef SECRET_OTA_PASSWORD
#define SECRET_OTA_PASSWORD ""
#endif
