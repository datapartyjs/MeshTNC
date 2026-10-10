#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
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

// Pointer to the currently active mesh radio — swap to switch bands
extern RadioLibWrapper* active_radio;

extern WRAPPER_CLASS radio_driver;

extern AutoDiscoverRTCClock rtc_clock;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord);
void radio_set_tx_power(uint8_t dbm);

// BYOMesh: strict config. false = radio refused it (both radios then sleep: no RX/TX)
bool radio_apply_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord);
bool radio_apply_tx_power(uint8_t dbm);
void radio_disable_all();

// power off (see target.cpp): radios numbered by frequency, 1 = SX1276, 2 = SX1281
int radio_count();
const char* radio_name(int n);
bool radio_is_active(int n);
bool radio_power_off(int n);
void board_power_off();   // doesn't return

// MCU light sleep (see target.cpp)
#define BOARD_WAKE_TIMER   0
#define BOARD_WAKE_LORA    1
#define BOARD_WAKE_SERIAL  2
#define BOARD_WAKE_OTHER   3
bool board_rx_irq_pending();
bool board_rx_path_ready();
int board_light_sleep(uint32_t max_ms, bool lora_wake);   // returns BOARD_WAKE_*
