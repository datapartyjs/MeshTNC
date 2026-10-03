#include <Arduino.h>
#include "target.h"

ESP32Board board;

// --- SX1281 2.4GHz (SPI2 / FSPI) ---
// On this board both radio NRESETs are tied to CHIP_PU, not a GPIO, so there's no reset pin.
// A board revision that wires the SX1281 NRESET to a GPIO can define P_SX1281_RESET; the
// SX1281 then wakes from sleep through RadioLib instead of CustomSX1281::nssWake().
#ifdef P_SX1281_RESET
  #define SX1281_RESET_PIN  P_SX1281_RESET
#else
  #define SX1281_RESET_PIN  RADIOLIB_NC
#endif
static SPIClass spi_sx1281(FSPI);
CustomSX1281 radio_sx1281(new Module(P_SX1281_NSS, P_SX1281_DIO1, SX1281_RESET_PIN, P_SX1281_BUSY, spi_sx1281));
CustomSX1281Wrapper radio_driver_2ghz(radio_sx1281, board);

// --- SX1276 915MHz (SPI3 / HSPI) ---
static SPIClass spi_sx1276(HSPI);
CustomSX1276 radio_sx1276(new Module(P_SX1276_NSS, P_SX1276_DIO0, RADIOLIB_NC, P_SX1276_DIO1, spi_sx1276));
CustomSX1276Wrapper radio_driver(radio_sx1276, board);

// Active radio: only changed by radio_apply_params(), once a config is fully applied to it
// (>2000 MHz = SX1281). MyMesh::applyRadioConfig() then binds the Dispatcher to it.
RadioLibWrapper* active_radio = &radio_driver;

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// Route J2 coax via U8 RFASWA630ATF09:
//   LOW  → RF2 → AT2401C → SX1281 (2.4GHz)
//   HIGH → RF1 → U6      → SX1276 (915MHz)
static void set_rf_switch(RadioLibWrapper* r) {
  digitalWrite(P_SX1281_RF_SW, (r == &radio_driver_2ghz) ? LOW : HIGH);
}

static RadioLibWrapper* radio_for_freq(float freq) {
  return freq > 2000.f ? (RadioLibWrapper*) &radio_driver_2ghz : (RadioLibWrapper*) &radio_driver;
}

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  pinMode(P_SX1281_BUSY, INPUT);
  pinMode(P_SX1281_RF_SW, OUTPUT);
  set_rf_switch(&radio_driver);

  spi_sx1281.setFrequency(13000000);
  spi_sx1281.begin();
  //spi_sx1281.setFrequency(13000000);


  auto wr = [&](uint8_t val) {
    digitalWrite(P_SX1281_NSS, LOW);
    spi_sx1281.transfer(0x84);
    spi_sx1281.transfer(0x01);
    digitalWrite(P_SX1281_NSS, HIGH);
  };
  pinMode(P_SX1281_NSS, OUTPUT);
  digitalWrite(P_SX1281_NSS, HIGH);
  spi_sx1281.beginTransaction(SPISettings(13000000, MSBFIRST, SPI_MODE0));
  wr(0x08);  delay(2);  //sleep mode
  spi_sx1281.endTransaction();

  // Init SX1281 — 2400 MHz, 203.125 kHz BW, SF9, CR4/7, 20 dBm
  bool ok_2ghz = radio_sx1281.std_init(2400.0, 203.125f, 9, 7, 20, &spi_sx1281);
  if (!ok_2ghz) {
    Serial.println("WARN: SX1281 2.4GHz init failed, 2.4GHz radio configs will be refused");
  } else {
    // keep the 2-byte CRC set in std_init(): without it corrupted frames reach the host
    Serial.println("SX1281 ready");
  }

  // Init SX1276 — 915 MHz, 250 kHz BW, SF10, CR4/5, 20 dBm
  spi_sx1276.begin(P_SX1276_SCLK, P_SX1276_MISO, P_SX1276_MOSI);

  bool ok_915 = radio_sx1276.std_init(&spi_sx1276);
  if (ok_915) {
    //radio_sx1276.setCurrentLimit(120);
    // keep the CRC set in std_init(): without it corrupted frames reach the host
    Serial.println("SX1276 ready");
  } else {
    Serial.print("WARN: SX1276 init failed, sub-GHz radio configs will be refused: ");
    //Serial.println(status_915);
  }

  // Neither radio is usable until radio_apply_params() has fully configured one; until
  // then MyMesh holds the Dispatcher's radio gate shut. The SX1281 sleeps until selected
  // (a 2.4 GHz config wakes it again right away, which also exercises the wake path).
  if (ok_2ghz) radio_driver_2ghz.sleepRadio();

  return true;
}

uint32_t radio_get_rng_seed() {
  if (active_radio == &radio_driver_2ghz){
    return radio_sx1281.random(0x7FFFFFFF);
  }
  return radio_sx1276.random(0x7FFFFFFF);
}

// RadioLib setters all return an int16_t status; stop at the first one that fails
#define TRY_RADIO(call)  do { int16_t _e = (call); if (_e != RADIOLIB_ERR_NONE) { \
    MESH_DEBUG_PRINTLN("radio_apply_params: %s failed (%d)", #call, _e); return false; } } while (0)

static bool configure_sx1281(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) {
  TRY_RADIO(radio_sx1281.setFrequency(freq));
  TRY_RADIO(radio_sx1281.setSpreadingFactor(sf));
  TRY_RADIO(radio_sx1281.setBandwidth(bw));
  TRY_RADIO(radio_sx1281.setCodingRate(cr));
  TRY_RADIO(radio_sx1281.setSyncWord(syncWord));
  return true;
}

static bool configure_sx1276(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) {
  TRY_RADIO(radio_sx1276.setFrequency(freq));
  TRY_RADIO(radio_sx1276.setSpreadingFactor(sf));
  TRY_RADIO(radio_sx1276.setBandwidth(bw));
  TRY_RADIO(radio_sx1276.setCodingRate(cr));
  TRY_RADIO(radio_sx1276.setSyncWord(syncWord));
  return true;
}

// Strict radio (re)configuration. Must only be called while nothing is transmitting
// (MyMesh::applyRadioConfig() holds the Dispatcher gate and waits for that).
// Never leaves a radio usable on anything but the requested config:
//  - the radio for the other band is put to sleep first, whatever happens next
//  - the radio for this band is woken and every parameter must be accepted
//  - on any failure that radio is put to sleep too, and false is returned
// active_radio and the RF switch only change once the config is fully applied.
bool radio_apply_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) {
  RadioLibWrapper* r = radio_for_freq(freq);
  RadioLibWrapper* other = (r == &radio_driver_2ghz) ? (RadioLibWrapper*) &radio_driver
                                                     : (RadioLibWrapper*) &radio_driver_2ghz;
  other->sleepRadio();

  if (!r->wakeRadio()) {
    MESH_DEBUG_PRINTLN("radio_apply_params: %s did not wake", r == &radio_driver_2ghz ? "SX1281" : "SX1276");
    r->sleepRadio();
    return false;
  }

  bool ok = (r == &radio_driver_2ghz) ? configure_sx1281(freq, bw, sf, cr, syncWord)
                                      : configure_sx1276(freq, bw, sf, cr, syncWord);
  if (!ok) {
    r->sleepRadio();   // partially configured: must not be used
    return false;
  }

  active_radio = r;
  set_rf_switch(r);
  return true;
}

// put both radios to sleep: used when no valid config could be applied
void radio_disable_all() {
  radio_driver.sleepRadio();
  radio_driver_2ghz.sleepRadio();
}

// kept for the common target API; BYOMesh code uses radio_apply_params() and its result
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) {
  if (!radio_apply_params(freq, bw, sf, cr, syncWord)) radio_disable_all();
}

void radio_set_tx_power(uint8_t dbm) {
  radio_apply_tx_power(dbm);
}

// tx power for the active radio. Not fatal: an out-of-range value (e.g. above the SX1281's
// 13 dBm) is rejected by RadioLib and the radio keeps its previous power.
bool radio_apply_tx_power(uint8_t dbm) {
  int16_t e = (active_radio == &radio_driver_2ghz) ? radio_sx1281.setOutputPower(dbm)
                                                   : radio_sx1276.setOutputPower(dbm);
  if (e != RADIOLIB_ERR_NONE) {
    MESH_DEBUG_PRINTLN("radio_apply_tx_power(%d) failed (%d)", (int)dbm, e);
    return false;
  }
  return true;
}
