#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <Preamble.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1281Wrapper.h>
#include <helpers/radiolib/CustomSX1276Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>

extern ESP32Board board;

// Both radio instances — same PCB, RF switch selects active antenna path
extern CustomSX1281 radio_sx1281;
extern CustomSX1276 radio_sx1276;

extern CustomSX1281Wrapper radio_driver_2ghz;
extern CustomSX1276Wrapper radio_driver_915;

// Pointer to the currently active mesh radio — swap to switch bands
extern RadioLibWrapper* active_radio;

// Alias used by simple_repeater/main.cpp which expects radio_driver by name
#define radio_driver (*active_radio)

extern AutoDiscoverRTCClock rtc_clock;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord);
void radio_set_tx_power(uint8_t dbm);
