#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
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

static void printHelp() {
  Serial.println("commands: adv | bat | time <epoch> | addchan <name> <psk> | help");
}

static void handleLine(char* s) {
  while (*s == ' ') s++;
  if (!*s) return;

  if (!strcmp(s, "adv")) {
    the_mesh.advertSelf();
  } else if (!strcmp(s, "bat")) {
    Serial.printf("vbat=%dmV vbus=%dmV charging=%d\n", board.getBattMilliVolts(),
                  board.getVbusMilliVolts(), board.isCharging());
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

  SPIFFS.begin(true);
  the_mesh.begin();

  mesh::LocalIdentity& id = the_mesh.self_id;
  Serial.printf("node %s  pubkey %02X%02X%02X%02X\n", the_mesh.nodeName(),
                id.pub_key[0], id.pub_key[1], id.pub_key[2], id.pub_key[3]);
  Serial.printf("radio %.3fMHz bw=%.1f sf=%d cr=%d\n", (float)LORA_FREQ, (float)LORA_BW,
                (int)LORA_SF, (int)LORA_CR);
  printHelp();

  ui.begin();
  the_mesh.advertSelf();
}

void loop() {
  the_mesh.loop();
  the_mesh.persistTick();
  rtc_clock.tick();
  ui.loop();
  pollSerial();

  if (ui.isAsleep() && !board.isCharging()) board.idleSleep();
}
