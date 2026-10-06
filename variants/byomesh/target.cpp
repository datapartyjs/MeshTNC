#include <Arduino.h>
#include "target.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "driver/uart.h"

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

// Route J2 coax via U8 RFASWA630ATF09 (RFC = J2):
//   RF2 <- AT2401C ANT <- AT2401C TXRX <- SX1281 RFIO   (2.4 GHz)
//   RF1 <- U6 <- SX1276                                  (sub-GHz)
// RFASWA630ATF09 datasheet logic table: VCTL low -> RF1 on, VCTL high -> RF2 on.
// (This used to drive VCTL LOW for the SX1281, i.e. the 2.4 GHz PA transmitted into the
// switch's off port, 20 dB isolation, while the antenna was connected to the SX1276 side.)
// Override with -D P_SX1281_RF_SW_2G_LEVEL=LOW if the board inverts VCTL.
#ifndef P_SX1281_RF_SW_2G_LEVEL
  #define P_SX1281_RF_SW_2G_LEVEL  HIGH
#endif
static bool rf_sw_on_2g = false;   // what set_rf_switch() last selected

static void set_rf_switch(RadioLibWrapper* r) {
  rf_sw_on_2g = (r == &radio_driver_2ghz);
  digitalWrite(P_SX1281_RF_SW, rf_sw_on_2g ? P_SX1281_RF_SW_2G_LEVEL : !P_SX1281_RF_SW_2G_LEVEL);
}

// TX interlocks (checked by RadioLibWrapper::startSendRaw): a radio may only transmit while
// it is the active radio and U8 connects its path to the antenna. Above all, the AT2401C PA
// must never drive the switch's off port.
static bool sx1281_tx_allowed() { return rf_sw_on_2g && active_radio == &radio_driver_2ghz; }
static bool sx1276_tx_allowed() { return !rf_sw_on_2g && active_radio == &radio_driver; }

// --- 2.4 GHz TX power: SX1281 RFIO -> AT2401C TXRX..ANT -> U8 RF2..RFC -> J2 ----------------
//   SX1281 output        -18..+13 dBm (setOutputPower)
//   AT2401C, TX          gain 25 dB small-signal, Psat +22 dBm, abs max input +5 dBm
//   U8 RFASWA630ATF09    insertion loss 0.50 dB typ / 0.65 dB max at 2.2-2.7 GHz,
//                        abs max RF input +35 dBm
// The configured tx power is the power wanted at J2. The SX1281 is driven at
// (power - PA gain), so J2 sees about 0.5 dB less than requested (switch loss, plus any
// SX1281-to-PA matching loss), and near the top the PA compresses, so +20 dBm at the PA
// is more like +18..19 dBm. Every error is on the low side: never more than requested.
// PA output is capped at BYOMESH_PA_MAX_OUT_DBM (~ +19.5 dBm at J2), PA input at
// BYOMESH_PA_MAX_IN_DBM.
#ifndef BYOMESH_PA_GAIN_DB
  #define BYOMESH_PA_GAIN_DB      25
#endif
#ifndef BYOMESH_PA_MAX_OUT_DBM
  #define BYOMESH_PA_MAX_OUT_DBM  20    // stay below the +22 dBm saturation point
#endif
#ifndef BYOMESH_PA_MAX_IN_DBM
  #define BYOMESH_PA_MAX_IN_DBM   0     // 5 dB margin below the +5 dBm absolute maximum
#endif
#define SX1281_MIN_DBM  (-18)
#define SX1281_MAX_DBM  13
static_assert(BYOMESH_PA_MAX_IN_DBM <= 5, "AT2401C absolute maximum RF input is +5 dBm");

// SX1281 output power for a wanted power at the antenna
static int8_t sx1281_drive_for(int antenna_dbm) {
  if (antenna_dbm > BYOMESH_PA_MAX_OUT_DBM) antenna_dbm = BYOMESH_PA_MAX_OUT_DBM;
  int drive = antenna_dbm - BYOMESH_PA_GAIN_DB;
  if (drive > BYOMESH_PA_MAX_IN_DBM) drive = BYOMESH_PA_MAX_IN_DBM;
  if (drive > SX1281_MAX_DBM) drive = SX1281_MAX_DBM;
  if (drive < SX1281_MIN_DBM) drive = SX1281_MIN_DBM;   // ~ +7 dBm at the antenna
  return (int8_t) drive;
}

// --- AT2401C PA/LNA enables: TXEN = GPIO 39 (P_SX1281_TXEN), RXEN = GPIO 40 (P_SX1281_RXEN) ---
// SX1281 RFIO -> AT2401C TXRX. Both low = PA and LNA off (shutdown); never both high.
// Once the SX1281 is initialised RadioLib switches them with each mode change (see
// CustomSX1281::std_init()); at2401c_off() covers boot and every path where RadioLib may
// not have driven them (SX1281 failed to init, failed to wake, or radios disabled).
static void at2401c_off() {   // (U8 is left alone: a PA that is off can't drive any port)
  digitalWrite(P_SX1281_TXEN, LOW);   // set the level before enabling the output: no glitch high
  digitalWrite(P_SX1281_RXEN, LOW);
  pinMode(P_SX1281_TXEN, OUTPUT);
  pinMode(P_SX1281_RXEN, OUTPUT);
  digitalWrite(P_SX1281_TXEN, LOW);
  digitalWrite(P_SX1281_RXEN, LOW);
}

// Called by the Arduino core before setup(), so the AT2401C is shut down as early as
// software can: GPIO 39/40 are undriven from reset until here (they are also the ESP32-S3
// JTAG MTCK/MTDO pins), and setup() waits a second before radio_init().
extern "C" void initVariant() {
  // pins held by board_power_off() stay held until released (a reset may not clear it)
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t) P_SX1281_TXEN);
  gpio_hold_dis((gpio_num_t) P_SX1281_RXEN);
  gpio_hold_dis((gpio_num_t) P_SX1281_NSS);
  gpio_hold_dis((gpio_num_t) P_SX1276_NSS);
#if defined(P_LED_DATA) && defined(P_LED_CLK)
  gpio_hold_dis((gpio_num_t) P_LED_DATA);
  gpio_hold_dis((gpio_num_t) P_LED_CLK);
#endif
  at2401c_off();
  pinMode(P_SX1281_RF_SW, OUTPUT);
  set_rf_switch(&radio_driver);   // antenna on the SX1276 (non-PA) side until a radio is configured
}

static RadioLibWrapper* radio_for_freq(float freq) {
  return freq > 2000.f ? (RadioLibWrapper*) &radio_driver_2ghz : (RadioLibWrapper*) &radio_driver;
}

bool radio_init() {
  at2401c_off();   // again, in case the core didn't call initVariant()
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  pinMode(P_SX1281_BUSY, INPUT);
  pinMode(P_SX1281_RF_SW, OUTPUT);
  set_rf_switch(&radio_driver);
  radio_driver.tx_allowed = sx1276_tx_allowed;
  radio_driver_2ghz.tx_allowed = sx1281_tx_allowed;

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
  // start at the lowest drive; radio_apply_tx_power() sets the real one before any TX
  bool ok_2ghz = radio_sx1281.std_init(2400.0, 203.125f, 9, 7, SX1281_MIN_DBM, &spi_sx1281);
  if (!ok_2ghz) {
    at2401c_off();
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
  at2401c_off();

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
  if (other == &radio_driver_2ghz) at2401c_off();   // 2.4 GHz path unused: PA and LNA off

  if (!r->wakeRadio()) {
    MESH_DEBUG_PRINTLN("radio_apply_params: %s did not wake", r == &radio_driver_2ghz ? "SX1281" : "SX1276");
    r->sleepRadio();
    if (r == &radio_driver_2ghz) at2401c_off();
    return false;
  }

  bool ok = (r == &radio_driver_2ghz) ? configure_sx1281(freq, bw, sf, cr, syncWord)
                                      : configure_sx1276(freq, bw, sf, cr, syncWord);
  if (!ok) {
    r->sleepRadio();   // partially configured: must not be used
    if (r == &radio_driver_2ghz) at2401c_off();
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
  at2401c_off();
}

// kept for the common target API; BYOMesh code uses radio_apply_params() and its result
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord) {
  if (!radio_apply_params(freq, bw, sf, cr, syncWord)) radio_disable_all();
}

void radio_set_tx_power(uint8_t dbm) {
  radio_apply_tx_power(dbm);
}

// tx power for the active radio; dbm is the wanted power at the antenna.
// 2.4 GHz: converted to an SX1281 drive level for the AT2401C (see sx1281_drive_for()), so it
// is capped at BYOMESH_PA_MAX_OUT_DBM and can't go below ~+7 dBm.
// Not fatal on failure: the radio keeps its previous power (the SX1281 starts at its minimum).
bool radio_apply_tx_power(uint8_t dbm) {
  int16_t e;
  if (active_radio == &radio_driver_2ghz) {
    int8_t drive = sx1281_drive_for(dbm);
    e = radio_sx1281.setOutputPower(drive);
    MESH_DEBUG_PRINTLN("tx power: requested %d dBm, SX1281 drive %d dBm, ~%d dBm at antenna",
      (int)dbm, (int)drive, (int)drive + BYOMESH_PA_GAIN_DB);
  } else {
    e = radio_sx1276.setOutputPower(dbm);
  }
  if (e != RADIOLIB_ERR_NONE) {
    MESH_DEBUG_PRINTLN("radio_apply_tx_power(%d) failed (%d)", (int)dbm, e);
    return false;
  }
  return true;
}

// --- power off -----------------------------------------------------------------------------
// LoRa radios are numbered by frequency: 1 = SX1276 (sub-GHz), 2 = SX1281 (2.4 GHz).
// Neither can be power-gated on this board (both NRESETs are on CHIP_PU, no supply switch),
// so "off" is each chip's sleep mode. On the 2.4 GHz path the AT2401C goes to shutdown
// (TXEN and RXEN both low), its lowest-current mode; its RX mode keeps the LNA powered.
int radio_count() { return 2; }

const char* radio_name(int n) {
  return n == 1 ? "SX1276" : n == 2 ? "SX1281" : "?";
}

bool radio_is_active(int n) {
  return (n == 1 && active_radio == &radio_driver) || (n == 2 && active_radio == &radio_driver_2ghz);
}

bool radio_power_off(int n) {
  if (n == 1) return radio_driver.sleepRadio();
  if (n == 2) {
    bool ok = radio_driver_2ghz.sleepRadio();
    at2401c_off();
    return ok;
  }
  return false;
}

static void hold_level(int pin, int level) {
  digitalWrite(pin, level);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
  gpio_hold_en((gpio_num_t) pin);
}

// Radios to sleep, then the ESP32-S3 into deep sleep with no wake-up source: it stays
// there until a reset (EN, e.g. esptool's RTS reset) or a power cycle. Pins would float in
// deep sleep, so the ones that matter are held: the AT2401C enables low (PA and LNA off),
// both radios' NSS high (an NSS edge wakes the SX1281), the LED lines low.
void board_power_off() {
  radio_disable_all();
  hold_level(P_SX1281_TXEN, LOW);
  hold_level(P_SX1281_RXEN, LOW);
  hold_level(P_SX1281_NSS, HIGH);
  hold_level(P_SX1276_NSS, HIGH);
#if defined(P_LED_DATA) && defined(P_LED_CLK)
  hold_level(P_LED_DATA, LOW);
  hold_level(P_LED_CLK, LOW);
#endif
  gpio_deep_sleep_hold_en();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();   // doesn't return
}

// --- MCU light sleep (powersave / sleep) ----------------------------------------------------
// Only the ESP32-S3 sleeps: the active radio stays in RX, and the ESP32 is woken by
//   - LoRa: the active radio's DIO pin (SX1281 DIO1 / SX1276 DIO0) going high on RX done,
//   - the host link: activity on UART0 RX (the bytes that wake it are lost: hosts must
//     send a few FEND bytes first; a FEND-only frame is empty and ignored),
//   - a timer, for housekeeping.
// GPIOs are held at their levels while it sleeps, so the RF path stays exactly as RX left it:
// on 2.4 GHz the AT2401C in RX (RXEN high, TXEN low: LNA on, PA off) and U8 on its port.
static int active_dio_pin() {
  return active_radio == &radio_driver_2ghz ? P_SX1281_DIO1 : P_SX1276_DIO0;
}

// the active radio has an interrupt (packet) waiting to be read
bool board_rx_irq_pending() {
  return digitalRead(active_dio_pin()) == HIGH;
}

// the RF path is set up for RX on the active radio (checked before every sleep)
bool board_rx_path_ready() {
  if (active_radio == &radio_driver_2ghz) {
    return rf_sw_on_2g && digitalRead(P_SX1281_RXEN) == HIGH && digitalRead(P_SX1281_TXEN) == LOW;
  }
  return !rf_sw_on_2g;
}

static const int sleep_hold_pins[] = {
  P_SX1281_TXEN, P_SX1281_RXEN, P_SX1281_RF_SW, P_SX1281_NSS, P_SX1276_NSS,
#if defined(P_LED_DATA) && defined(P_LED_CLK)
  P_LED_DATA, P_LED_CLK,
#endif
};

int board_light_sleep(uint32_t max_ms, bool lora_wake) {
  const gpio_num_t dio = (gpio_num_t) active_dio_pin();

  for (int pin : sleep_hold_pins) gpio_hold_en((gpio_num_t) pin);

  if (lora_wake) {
    // the DIO ISR is edge triggered; light sleep can only wake on a level. Mask the ISR
    // while the pin is set up for wake-up, or a high level would trigger it endlessly.
    gpio_intr_disable(dio);
    gpio_wakeup_enable(dio, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
  uart_set_wakeup_threshold(UART_NUM_0, 3);   // RX edges needed to wake (the minimum)
  esp_sleep_enable_uart_wakeup(UART_NUM_0);
  esp_sleep_enable_timer_wakeup((uint64_t) max_ms * 1000ULL);

  esp_light_sleep_start();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (lora_wake) {
    gpio_wakeup_disable(dio);
    gpio_set_intr_type(dio, GPIO_INTR_POSEDGE);   // back to the edge-triggered RadioLib ISR
    gpio_intr_enable(dio);
  }
  for (int pin : sleep_hold_pins) gpio_hold_dis((gpio_num_t) pin);

  switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:  return BOARD_WAKE_LORA;
    case ESP_SLEEP_WAKEUP_UART:  return BOARD_WAKE_SERIAL;
    case ESP_SLEEP_WAKEUP_TIMER: return BOARD_WAKE_TIMER;
    default:                     return BOARD_WAKE_OTHER;
  }
}
