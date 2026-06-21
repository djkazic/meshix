#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include "target.h"
#include "MeshixNode.h"
#include "ui/UI.h"

static ArduinoMillis ms_clock;
static EspRNG rng;
static SimpleMeshTables tables;
static StaticPoolPacketManager mgr(16);
static MeshixNode the_mesh(radio_driver, ms_clock, rng, rtc_clock, mgr, tables);
static TWatchLGFX tft;
static UI ui(tft, the_mesh, board);

static char line[128];
static uint8_t line_len;
static char boot_reason[24] = "?";

static void printHelp() {
  Serial.println("commands: adv | bat | boot | time <epoch> | addchan <name> <psk> | regen | help");
}

static void handleLine(char* s) {
  while (*s == ' ') s++;
  if (!*s) return;

  if (!strcmp(s, "adv")) {
    the_mesh.advertSelf();
  } else if (!strcmp(s, "bat")) {
    Serial.printf("vbat=%dmV vbus=%dmV charging=%d\n", board.getBattMilliVolts(),
                  board.getVbusMilliVolts(), board.isCharging());
  } else if (!strcmp(s, "regen")) {
    the_mesh.regenerateIdentity();
    mesh::LocalIdentity& id = the_mesh.self_id;
    Serial.printf("regenerated, pubkey %02X%02X%02X%02X\n", id.pub_key[0], id.pub_key[1],
                  id.pub_key[2], id.pub_key[3]);
  } else if (!strcmp(s, "boot")) {
    Serial.printf("last reset: %s, uptime %lus\n", boot_reason, (unsigned long)(millis() / 1000));
  } else if (!strcmp(s, "time")) {
    Serial.printf("rtc now %lu\n", (unsigned long)the_mesh.now());
  } else if (!strncmp(s, "time ", 5)) {
    uint32_t epoch = strtoul(s + 5, NULL, 10);
    rtc_clock.setCurrentTime(epoch);
    Serial.printf("rtc set to %lu\n", (unsigned long)epoch);
  } else if (!strncmp(s, "addchan ", 8)) {
    s += 8;
    char* sp = strchr(s, ' ');
    if (!sp) { Serial.println("usage: addchan <name> <psk_base64>"); return; }
    *sp = 0;
    if (the_mesh.addNamedChannel(s, sp + 1)) Serial.printf("added channel %s\n", s);
    else Serial.println("addchan failed (full?)");
  } else {
    printHelp();
  }
}

static void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[line_len] = 0;
      handleLine(line);
      line_len = 0;
    } else if (line_len < sizeof(line) - 1) {
      line[line_len++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Serial.println("\nmeshix");

  WiFi.mode(WIFI_OFF);
  btStop();

  board.begin();
  if (!radio_init()) {
    Serial.println("radio init failed, halting");
    while (true) delay(1000);
  }

  LittleFS.begin(true);
  the_mesh.begin();

  mesh::LocalIdentity& id = the_mesh.self_id;
  Serial.printf("node %s  pubkey %02X%02X%02X%02X\n", the_mesh.nodeName(),
                id.pub_key[0], id.pub_key[1], id.pub_key[2], id.pub_key[3]);
  Serial.printf("radio %.3fMHz bw=%.1f sf=%d cr=%d\n", (float)LORA_FREQ, (float)LORA_BW,
                (int)LORA_SF, (int)LORA_CR);
  printHelp();

  int reason = esp_reset_reason();
  const char* rr;
  switch (reason) {
    case ESP_RST_POWERON: rr = "power-on"; break;
    case ESP_RST_BROWNOUT: rr = "BROWNOUT"; break;
    case ESP_RST_PANIC: rr = "PANIC"; break;
    case ESP_RST_INT_WDT: rr = "INT_WDT"; break;
    case ESP_RST_TASK_WDT: rr = "TASK_WDT"; break;
    case ESP_RST_WDT: rr = "WDT"; break;
    case ESP_RST_SW: rr = "sw"; break;
    case ESP_RST_EXT: rr = "ext"; break;
    case ESP_RST_DEEPSLEEP: rr = "deepsleep"; break;
    default: rr = "other"; break;
  }
  snprintf(boot_reason, sizeof(boot_reason), "%s (%d)", rr, reason);
  Serial.printf("reset reason: %s\n", boot_reason);

  ui.begin();
  the_mesh.advertSelf();
}

void loop() {
  the_mesh.loop();
  the_mesh.persistTick();
  rtc_clock.tick();
  ui.loop();
  board.buzzTick();
  pollSerial();

  if (ui.isAsleep() && !board.isCharging()) board.idleSleep();
}
