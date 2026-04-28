#pragma once
// waveshare_display.h - Waveshare ESP32-S3-Touch-LCD-7 display class.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include "config.h"

class WaveshareDisplay : public lgfx::LGFX_Device {
 public:
  WaveshareDisplay() {
    {
      auto cfg = panel_.config();
      cfg.memory_width  = cfg::kScreenWidth;
      cfg.memory_height = cfg::kScreenHeight;
      cfg.panel_width   = cfg::kScreenWidth;
      cfg.panel_height  = cfg::kScreenHeight;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      panel_.config(cfg);
    }
    {
      auto cfg = panel_.config_detail();
      cfg.use_psram = 1;
      panel_.config_detail(cfg);
    }
    {
      auto cfg = bus_.config();
      cfg.panel            = &panel_;
      cfg.freq_write       = cfg::kRgbClockHz;
      cfg.pin_d0           = cfg::kPinD0;
      cfg.pin_d1           = cfg::kPinD1;
      cfg.pin_d2           = cfg::kPinD2;
      cfg.pin_d3           = cfg::kPinD3;
      cfg.pin_d4           = cfg::kPinD4;
      cfg.pin_d5           = cfg::kPinD5;
      cfg.pin_d6           = cfg::kPinD6;
      cfg.pin_d7           = cfg::kPinD7;
      cfg.pin_d8           = cfg::kPinD8;
      cfg.pin_d9           = cfg::kPinD9;
      cfg.pin_d10          = cfg::kPinD10;
      cfg.pin_d11          = cfg::kPinD11;
      cfg.pin_d12          = cfg::kPinD12;
      cfg.pin_d13          = cfg::kPinD13;
      cfg.pin_d14          = cfg::kPinD14;
      cfg.pin_d15          = cfg::kPinD15;
      cfg.pin_henable      = cfg::kPinDe;
      cfg.pin_vsync        = cfg::kPinVsync;
      cfg.pin_hsync        = cfg::kPinHsync;
      cfg.pin_pclk         = cfg::kPinPclk;
      cfg.hsync_polarity   = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch  = 8;
      cfg.vsync_polarity   = 0;
      cfg.vsync_front_porch = 16;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch  = 16;
      cfg.pclk_idle_high   = true;
      bus_.config(cfg);
    }
    panel_.setBus(&bus_);
    setPanel(&panel_);
  }

 private:
  lgfx::Panel_RGB panel_;
  lgfx::Bus_RGB   bus_;
};

extern WaveshareDisplay gfx;
