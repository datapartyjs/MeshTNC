#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <MinewsemiME25LS01Board.h>
#include <Preamble.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomLR1110Wrapper.h>
#include <helpers/ArduinoHelpers.h>

extern MinewsemiME25LS01Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord);
void radio_set_tx_power(uint8_t dbm);
