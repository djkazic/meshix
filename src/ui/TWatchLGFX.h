#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class TWatchLGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_ST7789 _panel;
  lgfx::Light_PWM _light;
  lgfx::Touch_FT5x06 _touch;

public:
  TWatchLGFX() {
    {
      auto c = _bus.config();
      c.spi_host = SPI3_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.pin_sclk = 18;
      c.pin_mosi = 13;
      c.pin_miso = -1;
      c.pin_dc = 38;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs = 12;
      c.pin_rst = -1;
      c.pin_busy = -1;
      c.panel_width = 240;
      c.panel_height = 240;
      c.offset_x = 0;
      c.offset_y = 0;
      c.readable = false;
      c.invert = true;
      c.rgb_order = false;
      c.bus_shared = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl = 45;
      c.freq = 44100;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    {
      auto c = _touch.config();
      c.i2c_port = 1;
      c.i2c_addr = 0x38;
      c.pin_sda = 39;
      c.pin_scl = 40;
      c.pin_int = 16;
      c.pin_rst = -1;
      c.freq = 400000;
      c.offset_rotation = 2;
      c.x_min = 0;
      c.x_max = 239;
      c.y_min = 0;
      c.y_max = 239;
      _touch.config(c);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
