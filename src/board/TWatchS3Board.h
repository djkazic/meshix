#pragma once

#include <MeshCore.h>
#include <helpers/ESP32Board.h>
#include <XPowersLib.h>
#include <Adafruit_DRV2605.h>

class TWatchS3Board : public ESP32Board {
  XPowersAXP2101 pmu;
  Adafruit_DRV2605 haptic;
  bool pmu_ok = false;
  bool haptic_ok = false;
  uint32_t _batt_ms = 0;
  int _batt_ema = 0;
  int _buzz_seg = -1;
  uint32_t _buzz_t = 0;

public:
  void begin();
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override { return "LilyGo T-Watch S3"; }

  bool isCharging();
  uint16_t getVbusMilliVolts();
  int batteryPercent();
  void buzz();
  void buzzTick();
  void idleSleep();
};
