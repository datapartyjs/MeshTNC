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

// Active radio, selected by radio_set_params() from the frequency (>2000 MHz = SX1281).
// MyMesh::bindActiveRadio() rebinds the Dispatcher to it whenever it changes.
RadioLibWrapper* active_radio = &radio_driver;

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// Set active radio and route J2 coax via U8 RFASWA630ATF09:
//   LOW  → RF2 → AT2401C → SX1281 (2.4GHz)
//   HIGH → RF1 → U6      → SX1276 (915MHz)
// The radio being switched to is woken first (it's asleep while unused); if it doesn't
// wake, the current radio stays active. The radio being left is put to sleep by
// MyMesh::bindActiveRadio(), once any transmit in progress on it has finished.
static bool select_radio(RadioLibWrapper* r) {
  if (r != active_radio && !r->wakeRadio()) {
    MESH_DEBUG_PRINTLN("select_radio: %s did not wake, staying on %s",
      r == &radio_driver_2ghz ? "SX1281" : "SX1276",
      active_radio == &radio_driver_2ghz ? "SX1281" : "SX1276");
    return false;
  }
  active_radio = r;
  digitalWrite(P_SX1281_RF_SW, (r == &radio_driver_2ghz) ? LOW : HIGH);
  return true;
}

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  pinMode(P_SX1281_BUSY, INPUT);
  pinMode(P_SX1281_RF_SW, OUTPUT);
  select_radio(&radio_driver);  

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
    Serial.println("WARN: SX1281 2.4GHz init failed, falling back to SX1276 915MHz");
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
    Serial.print("WARN: SX1276 915MHz init failed: ");
    //Serial.println(status_915);
  }

  select_radio(&radio_driver);

  // the SX1281 isn't the active radio yet: sleep it until radio_set_params() selects it
  // (a 2.4 GHz config wakes it again right away, which also exercises the wake path)
  if (ok_2ghz) radio_driver_2ghz.sleepRadio();

  return true;
}

uint32_t radio_get_rng_seed() {
  if (active_radio == &radio_driver_2ghz){
    return radio_sx1281.random(0x7FFFFFFF);
  }
  return radio_sx1276.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) {
  RadioLibWrapper* want = freq > 2000.f ? (RadioLibWrapper*) &radio_driver_2ghz : (RadioLibWrapper*) &radio_driver;
  if (!select_radio(want)) return;   // couldn't wake it: these params don't fit the current radio

  if (active_radio == &radio_driver_2ghz) {
    radio_sx1281.setFrequency(freq);
    radio_sx1281.setSpreadingFactor(sf);
    radio_sx1281.setBandwidth(bw);
    radio_sx1281.setCodingRate(cr);
    radio_sx1281.setSyncWord(syncWord);
  } else {
    radio_sx1276.setFrequency(freq);
    radio_sx1276.setSpreadingFactor(sf);
    radio_sx1276.setBandwidth(bw);
    radio_sx1276.setCodingRate(cr);
    radio_sx1276.setSyncWord(syncWord);
  }
}

void radio_set_tx_power(uint8_t dbm) {
  if (active_radio == &radio_driver_2ghz){
    radio_sx1281.setOutputPower(dbm);
  }else{
    radio_sx1276.setOutputPower(dbm);
  }
}
