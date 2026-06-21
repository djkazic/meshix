#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <esp_random.h>
#include "board/TWatchS3Board.h"

class EspRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override { esp_fill_random(dest, sz); }
};

extern TWatchS3Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
