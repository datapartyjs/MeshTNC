#include <Arduino.h>
#include "target.h"

ESP32Board board;

static SPIClass spi;

#if defined(RADIO_IS_SX128X)
  // SX1281: NSS, DIO1, RESET, BUSY
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  // SX1276: NSS, DIO0, RESET, DIO1
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, spi);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  return radio.std_init(&spi);
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
  radio.setSyncWord(syncWord);
}

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}
