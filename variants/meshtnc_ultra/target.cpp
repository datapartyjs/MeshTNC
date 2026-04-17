#include <Arduino.h>
#include "target.h"

ESP32Board board;

// --- SX1281 2.4GHz (SPI2) ---
// Both radio NRESETs are tied to CHIP_PU (hardware reset line), not a GPIO — RADIOLIB_NC correct.
static SPIClass spi_sx1281;
CustomSX1281 radio_sx1281(new Module(P_SX1281_NSS, P_SX1281_DIO1, RADIOLIB_NC, P_SX1281_BUSY, spi_sx1281));
CustomSX1281Wrapper radio_driver_2ghz(radio_sx1281, board);

// --- SX1276 915MHz (SPI3) ---
static SPIClass spi_sx1276;
CustomSX1276 radio_sx1276(new Module(P_SX1276_NSS, P_SX1276_DIO0, RADIOLIB_NC, P_SX1276_DIO1, spi_sx1276));
CustomSX1276Wrapper radio_driver_915(radio_sx1276, board);

// Default active radio: 2.4GHz
RadioLibWrapper* active_radio = &radio_driver_2ghz;

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// Set active radio and route J2 coax via U8 RFASWA630ATF09:
//   LOW  → RF2 → AT2401C → SX1281 (2.4GHz)
//   HIGH → RF1 → U6      → SX1276 (915MHz)
static void select_radio(RadioLibWrapper* r) {
  active_radio = r;
  digitalWrite(P_SX1281_RF_SW, (r == &radio_driver_2ghz) ? LOW : HIGH);
}

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  pinMode(P_SX1281_RF_SW, OUTPUT);
  select_radio(&radio_driver_2ghz);  // default to 2.4GHz path during init

  // Init SX1281 — 2400 MHz, 812.5 kHz BW, SF9, CR4/7, 20 dBm
  bool ok_2ghz = radio_sx1281.std_init(2400.0, 812.5, 9, 7, 20, &spi_sx1281);
  if (!ok_2ghz) {
    Serial.println("WARN: SX1281 2.4GHz init failed, falling back to SX1276 915MHz");
  }

  // Init SX1276 — 915 MHz, 250 kHz BW, SF10, CR4/5, 20 dBm
  spi_sx1276.begin(P_SX1276_SCLK, P_SX1276_MISO, P_SX1276_MOSI);
  int status_915 = radio_sx1276.begin(915.0, 250.0, 10, 5,
                                      RADIOLIB_SX127X_SYNC_WORD, 20, 16);
  bool ok_915 = (status_915 == RADIOLIB_ERR_NONE);
  if (ok_915) {
    radio_sx1276.setCurrentLimit(120);
    radio_sx1276.setCRC(1);
  } else {
    Serial.print("WARN: SX1276 915MHz init failed: ");
    Serial.println(status_915);
  }

  // Prefer 2.4GHz, fall back to 915MHz
  if (ok_2ghz) {
    select_radio(&radio_driver_2ghz);
  } else if (ok_915) {
    select_radio(&radio_driver_915);
  } else {
    return false;  // both radios failed
  }

  return true;
}

uint32_t radio_get_rng_seed() {
  if (active_radio == &radio_driver_2ghz)
    return radio_sx1281.random(0x7FFFFFFF);
  return radio_sx1276.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) {
  select_radio(freq > 2000.f ? &radio_driver_2ghz : &radio_driver_915);

  if (active_radio == &radio_driver_2ghz) {
    radio_sx1281.setFrequency(freq);
    radio_sx1281.setSpreadingFactor(sf);
    radio_sx1281.setBandwidth(bw);
    radio_sx1281.setCodingRate(cr);
    radio_sx1281.setSyncWord(syncWord);  // bridged to setLoRaSyncWord()
  } else {
    radio_sx1276.setFrequency(freq);
    radio_sx1276.setSpreadingFactor(sf);
    radio_sx1276.setBandwidth(bw);
    radio_sx1276.setCodingRate(cr);
    radio_sx1276.setSyncWord(syncWord);
  }
}

void radio_set_tx_power(uint8_t dbm) {
  if (active_radio == &radio_driver_2ghz)
    radio_sx1281.setOutputPower(dbm);
  else
    radio_sx1276.setOutputPower(dbm);
}
