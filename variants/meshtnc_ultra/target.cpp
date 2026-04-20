#include <Arduino.h>
#include "target.h"

ESP32Board board;

// --- SX1281 2.4GHz (SPI2 / FSPI) ---
// Both radio NRESETs are tied to CHIP_PU (hardware reset line), not a GPIO — RADIOLIB_NC correct.
static SPIClass spi_sx1281(FSPI);
CustomSX1281 radio_sx1281(new Module(P_SX1281_NSS, P_SX1281_DIO1, RADIOLIB_NC, P_SX1281_BUSY, spi_sx1281));
CustomSX1281Wrapper radio_driver_2ghz(radio_sx1281, board);

// --- SX1276 915MHz (SPI3 / HSPI) ---
static SPIClass spi_sx1276(HSPI);
CustomSX1276 radio_sx1276(new Module(P_SX1276_NSS, P_SX1276_DIO0, RADIOLIB_NC, P_SX1276_DIO1, spi_sx1276));
CustomSX1276Wrapper radio_driver_915(radio_sx1276, board);

// Default active radio: 2.4GHz. setup() calls the_mesh.setRadio(*active_radio) after
// radio_init() to rebind the mesh to whichever radio actually initialized successfully.
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

  // Init SX1281 — 2400 MHz, 203.125 kHz BW, SF9, CR4/7, 20 dBm
  bool ok_2ghz = radio_sx1281.std_init(2400.0, 203.125f, 9, 7, 20, &spi_sx1281);
  if (!ok_2ghz) {
    Serial.println("WARN: SX1281 2.4GHz init failed, falling back to SX1276 915MHz");
  } else {
    Serial.println("SX1281 ready");
  }

  // Init SX1276 — 915 MHz, 250 kHz BW, SF10, CR4/5, 20 dBm
  spi_sx1276.begin(P_SX1276_SCLK, P_SX1276_MISO, P_SX1276_MOSI);

  // Pre-warm SX1276 crystal oscillator. RadioLib's begin() transitions
  // LoRa sleep → standby in ~20µs; crystal cold-start needs ~2ms. Fix:
  // manually step FSK sleep → LoRa sleep (5ms startup) → LoRa standby
  // so crystal is oscillating when begin() is called. begin() re-sleeps
  // the chip for only ~20µs — crystal is at ~82% amplitude and restarts
  // instantly (decay constant τ ≈ 100µs for a 32MHz crystal).
  {
    auto wr = [&](uint8_t val) {
      digitalWrite(P_SX1276_NSS, LOW);
      spi_sx1276.transfer(0x81);   // write OpMode register (0x01 | 0x80)
      spi_sx1276.transfer(val);
      digitalWrite(P_SX1276_NSS, HIGH);
    };
    pinMode(P_SX1276_NSS, OUTPUT);
    digitalWrite(P_SX1276_NSS, HIGH);
    spi_sx1276.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    wr(0x08);  delay(2);  // FSK sleep
    wr(0x88);  delay(5);  // LoRa sleep — crystal starts
    wr(0x89);  delay(2);  // LoRa standby — crystal oscillating
    spi_sx1276.endTransaction();
  }

  int status_915 = radio_sx1276.begin(915.0, 250.0, 10, 5,
                                      RADIOLIB_SX127X_SYNC_WORD, 20, 16);
  bool ok_915 = (status_915 == RADIOLIB_ERR_NONE);
  if (ok_915) {
    radio_sx1276.setCurrentLimit(120);
    radio_sx1276.setCRC(1);
    Serial.println("SX1276 ready");
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
  select_radio(freq > 2000.f ? (RadioLibWrapper*) &radio_driver_2ghz : (RadioLibWrapper*) &radio_driver_915);

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
