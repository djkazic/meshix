#include "TWatchS3Board.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

static portMUX_TYPE sleepMux = portMUX_INITIALIZER_UNLOCKED;

void TWatchS3Board::begin() {
  ESP32Board::begin();

  pmu_ok = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_BOARD_SDA, PIN_BOARD_SCL);
  if (!pmu_ok) return;

  pmu.setALDO1Voltage(3300);
  pmu.enableALDO1();
  pmu.setALDO2Voltage(3300);
  pmu.enableALDO2();
  pmu.setALDO3Voltage(3300);
  pmu.enableALDO3();
  pmu.setALDO4Voltage(3300);
  pmu.enableALDO4();
  pmu.setBLDO2Voltage(3300);
  pmu.enableBLDO2();

  pmu.disableDC2();
  pmu.disableDC3();
  pmu.disableDC4();
  pmu.disableDC5();
  pmu.disableBLDO1();
  pmu.disableDLDO1();
  pmu.disableDLDO2();

  pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF);
  pmu.enableBattVoltageMeasure();
  pmu.enableVbusVoltageMeasure();
  pmu.enableSystemVoltageMeasure();
  pmu.enableGauge();

  haptic_ok = haptic.begin(&Wire);
  if (haptic_ok) {
    haptic.selectLibrary(1);
    haptic.setMode(DRV2605_MODE_INTTRIG);
  }
}

void TWatchS3Board::idleSleep() {
  gpio_num_t lora = (gpio_num_t)P_LORA_DIO_1;
  esp_sleep_enable_timer_wakeup(200000ULL);  // 200ms: poll touch + housekeeping

  portENTER_CRITICAL(&sleepMux);
  if (gpio_get_level(lora) == HIGH) {  // packet pending, don't sleep
    portEXIT_CRITICAL(&sleepMux);
    return;
  }
  esp_sleep_enable_gpio_wakeup();
  gpio_wakeup_enable(lora, GPIO_INTR_HIGH_LEVEL);
  esp_light_sleep_start();
  gpio_wakeup_disable(lora);
  gpio_set_intr_type(lora, GPIO_INTR_POSEDGE);
  portEXIT_CRITICAL(&sleepMux);
}

void TWatchS3Board::buzz() {
  if (!haptic_ok) return;
  haptic.setMode(DRV2605_MODE_REALTIME);
  haptic.setRealtimeValue(0x7F);
  _buzz_seg = 0;
  _buzz_t = millis() + 140;
}

void TWatchS3Board::buzzTick() {
  if (_buzz_seg < 0 || millis() < _buzz_t) return;
  if (++_buzz_seg > 5) {
    haptic.setRealtimeValue(0);
    _buzz_seg = -1;
    return;
  }
  bool on = (_buzz_seg & 1) == 0;
  haptic.setRealtimeValue(on ? 0x7F : 0);
  _buzz_t = millis() + (on ? 140 : 90);
}

uint16_t TWatchS3Board::getBattMilliVolts() {
  return pmu_ok ? pmu.getBattVoltage() : 0;
}

uint16_t TWatchS3Board::getVbusMilliVolts() {
  return pmu_ok ? pmu.getVbusVoltage() : 0;
}

bool TWatchS3Board::isCharging() {
  return pmu_ok && pmu.isCharging();
}

static int mvToPercent(int mv) {
  static const int curve[][2] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 45},
    {3700, 25}, {3600, 12}, {3500, 5}, {3300, 0},
  };
  int n = sizeof(curve) / sizeof(curve[0]);
  if (mv >= curve[0][0]) return 100;
  if (mv <= curve[n - 1][0]) return 0;
  for (int i = 0; i < n - 1; i++) {
    if (mv <= curve[i][0] && mv > curve[i + 1][0]) {
      int v0 = curve[i][0], p0 = curve[i][1], v1 = curve[i + 1][0], p1 = curve[i + 1][1];
      return p1 + (mv - v1) * (p0 - p1) / (v0 - v1);
    }
  }
  return 0;
}

int TWatchS3Board::batteryPercent() {
  if (!pmu_ok) return 0;

  int p = pmu.getBatteryPercent();
  if (p >= 0 && p <= 100) return p;  // hardware fuel gauge

  // fallback: smoothed LiPo voltage curve
  uint32_t now = millis();
  if (_batt_ema == 0 || now - _batt_ms > 2000) {
    _batt_ms = now;
    int mv = pmu.getBattVoltage();
    _batt_ema = _batt_ema == 0 ? mv : _batt_ema + (mv - _batt_ema) / 4;
  }
  return mvToPercent(_batt_ema);
}
