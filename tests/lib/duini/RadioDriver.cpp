#include "RadioDriver.h"

extern bool DUINI_EXPORTS radio_init() {
  //rtc_clock.begin(Wire);
  //return radio.std_init(&SPI);
  std::cerr << "Called radio_init()" << std::endl;
  return true;
}

extern uint32_t DUINI_EXPORTS radio_get_rng_seed() {
  //return radio.random(0x7FFFFFFF);
  std::random_device rd;
  std::default_random_engine engine(rd());

  std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
  uint32_t val = dist(engine);
  std::cerr << "Called radio_get_rnd_seed() = " << val << std::endl;
  return val;
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) { 
  /*
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
  radio.setSyncWord(syncWord);
  */
  std::cerr << "Called radio_set_params(" << freq << "," << bw << "," << sf
    << "," << cr << "," << syncWord << ")" << std::endl;
}

extern void DUINI_EXPORTS radio_set_tx_power(uint8_t dbm) {
  //radio.setOutputPower(dbm);
  std::cerr << "Called radio_set_tx_poert(" << dbm << ")" << std::endl;
}
