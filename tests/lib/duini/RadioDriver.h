#pragma once

#include "duini.h"

#include <random>
#include <iostream>

extern bool DUINI_EXPORTS radio_init();
extern uint32_t DUINI_EXPORTS radio_get_rng_seed();
extern void DUINI_EXPORTS radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord);
extern void DUINI_EXPORTS radio_set_tx_power(uint8_t dbm);
