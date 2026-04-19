#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <Preamble.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "HT-CT62Board.h"
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>

extern Heltec_CT62_Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord);
void radio_set_tx_power(uint8_t dbm);
