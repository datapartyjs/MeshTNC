#include <Arduino.h>   // needed for PlatformIO
#include <Dispatcher.h>
#include <Mesh.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/CommonCLI.h>
#include <RTClib.h>
#include <target.h>

// --- status LED: one APA102 on P_LED_DATA (data) + P_LED_CLK (clock) ----------------------
#if defined(P_LED_DATA) && defined(P_LED_CLK)
  #define HAS_STATUS_LED 1
  #include <helpers/APA102Led.h>
  #ifndef LED_GLOBAL_BRIGHTNESS
    #define LED_GLOBAL_BRIGHTNESS  31     // APA102 global brightness, 0..31
  #endif
  static APA102Led status_led(P_LED_DATA, P_LED_CLK, LED_GLOBAL_BRIGHTNESS);
#elif defined(P_LED_DATA)
  #warning "P_LED_DATA is set but P_LED_CLK is not: an APA102 needs a clock pin too. Building without LED support."
#endif

// status mode colors (r, g, b) and timings
#define LED_COLOR_BOOT       0,   0,  64   // blue:   booting, and for LED_BOOT_SHOW_MS after
#define LED_COLOR_FATAL     64,   0,   0   // red:    blinking while the radio is disabled
#define LED_COLOR_CRITICAL  64,  24,   0   // orange: flash on a new TX/RX/queue error
#define LED_COLOR_CAD_BUSY  32,   0,  48   // purple: a packet is waiting for a busy channel
#define LED_COLOR_TX         0,  64,   0   // green:  transmitting, off when the send completes
#define LED_COLOR_RX         0,  32,  48   // cyan:   flash on each received packet
#define LED_COLOR_BLE_RX    24,  24,  24   // white:  flash on each received BLE advertisement
#define LED_COLOR_OVERTEMP  48,  40,   0   // yellow: slow blink while over temperature
#define LED_BOOT_SHOW_MS       1000
#define LED_CRITICAL_SHOW_MS    300
#define LED_RX_SHOW_MS           50
#define LED_BLE_RX_SHOW_MS       50
#define LED_OVERTEMP_BLINK_MS   500

// Over-temperature, from the ESP32-S3's on-die sensor read every TEMP_CHECK_MS. The ESP32-S3,
// SX128x and SX1276 are all rated for -40..+85 C, and the die reads hotter than the board
// around it, so 80 C on the die leaves margin for all three. Clears TEMP_HYST_C below that.
#ifndef TEMP_CHECK_MS
  #define TEMP_CHECK_MS           5000
#endif
#ifndef OVERTEMP_C
  #define OVERTEMP_C              80.0f
#endif
#ifndef OVERTEMP_HYST_C
  #define OVERTEMP_HYST_C          5.0f
#endif
#define LED_FATAL_BLINK_MS      250
#define LED_CRITICAL_ERR_MASK  (ERR_EVENT_FULL | ERR_EVENT_CAD_TIMEOUT | ERR_EVENT_STARTRX_TIMEOUT | \
                                ERR_EVENT_TX_FAIL | ERR_EVENT_TX_STUCK)

/* ------------------------------ Config -------------------------------- */

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "1 Aug 2025"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1"
#endif

#ifndef LORA_FREQ
  #define LORA_FREQ   915.0
#endif
#ifndef LORA_BW
  #define LORA_BW     250
#endif
#ifndef LORA_SF
  #define LORA_SF     10
#endif
#ifndef LORA_CR
  #define LORA_CR      5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  20
#endif

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY   300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY     200
#endif

#define FIRMWARE_ROLE "kisstnc"

#define PACKET_LOG_FILE  "/packet_log"

#ifdef ENABLE_BLE
  #define BLE_SCAN_TIME_MS (30 * 10000)
  #define BLE_DEVICE_NAME "MeshTNC"
  #include <NimBLEDevice.h>
#endif

/* ------------------------------ Code -------------------------------- */

#define REQ_TYPE_GET_STATUS          0x01   // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE          0x02
#define REQ_TYPE_GET_TELEMETRY_DATA  0x03

#define RESP_SERVER_LOGIN_OK      0   // response to ANON_REQ


#define CLI_REPLY_DELAY_MILLIS  600


class MyMesh : public mesh::Mesh, public CommonCLICallbacks {

  FILESYSTEM* _fs;
  CommonCLI _cli;
  bool _logging;
  NodePrefs _prefs;

  // status LED state
  uint8_t _led_r, _led_g, _led_b;    // command mode color (not saved: off at boot)
  uint8_t _led_last_mode;
  uint16_t _led_seen_flags;          // Dispatcher error flags already flashed for
  bool _led_booting;
  unsigned long _led_boot_until, _led_crit_until, _led_rx_until, _led_ble_rx_until;
  bool _overtemp;
  unsigned long _temp_check_at;
  uint8_t reply_data[MAX_PACKET_PAYLOAD];
  unsigned long revert_radio_at;
  int _radio_gate;   // RADIO_GATE_*: HOLD until the first config is applied

#ifdef ENABLE_BLE
  NimBLEScan* bleScan;
  bool bleReported;
  uint32_t blePacketRxCount;
#endif

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  int getRadioGate() const override {
    return _radio_gate;
  }

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override {
    _led_rx_until = futureMillis(LED_RX_SHOW_MS);   // status LED: received a packet
    CLIMode cli_mode = _cli.getCLIMode();
    if (cli_mode == CLIMode::CLI) {
      if (!_prefs.log_rx) return;
      CommonCLI* cli = getCLI();
      Serial.printf("%lu", rtc_clock.getCurrentTime());
      Serial.printf(",RXLOG,%.2f,%.2f", rssi, snr);
      Serial.print(",");
      mesh::Utils::printHex(Serial, raw, len);
      Serial.println();
    } else if (cli_mode == CLIMode::KISS) {
      uint8_t kiss_rx[CMD_BUF_LEN_MAX];
      KISSModem* kiss = getCLI()->getKISSModem();
      uint16_t kiss_rx_len = kiss->encodeKISSFrame(
        KISSCmd::Data, raw, len, kiss_rx, sizeof(kiss_rx)
      );
      Serial.write(kiss_rx, kiss_rx_len);
    }
  }

  int calcRxDelay(float score, uint32_t air_time) const override {
    if (_prefs.rx_delay_base <= 0.0f) return 0;
    return (int) ((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
  }

  int getInterferenceThreshold() const override {
    return _prefs.interference_threshold;
  }
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }


public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc)
     : mesh::Mesh(radio, ms, *new StaticPoolPacketManager(32)), _cli(board, rtc, &_prefs, this, this)
  {
    revert_radio_at = 0;
    _radio_gate = RADIO_GATE_HOLD;   // no RX/TX until begin() has applied a radio config
    _logging = false;

#ifdef ENABLE_BLE
    bleReported = false;
#endif
    // defaults
    memset(&_prefs, 0, sizeof(_prefs));
    _prefs.airtime_factor = 1.0;    // one half
    _prefs.rx_delay_base =   0.0f;  // turn off by default, was 10.0;
    _prefs.tx_delay_factor = 0.5f;   // was 0.25f
    StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
    _prefs.node_lat = ADVERT_LAT;
    _prefs.node_lon = ADVERT_LON;
    _prefs.freq = LORA_FREQ;
    _prefs.sf = LORA_SF;
    _prefs.bw = LORA_BW;
    _prefs.cr = LORA_CR;
    _prefs.tx_power_dbm = LORA_TX_POWER;
    _prefs.interference_threshold = 0;  // disabled
    _prefs.sync_word = 0x2B;
    _prefs.log_rx = true;
    _prefs.ble_enabled = false;
    _prefs.ble_filter_dups = true;
    _prefs.ble_active_scan = false;
    _prefs.ble_max_results = 100;
    _prefs.ble_scantime = 10 * 1000;
    _prefs.led_enabled = true;
    _prefs.led_mode = LED_MODE_STATUS;

    _led_r = _led_g = _led_b = 0;
    _led_last_mode = LED_MODE_STATUS;
    _led_seen_flags = 0;
    _led_booting = false;
    _led_boot_until = _led_crit_until = _led_rx_until = _led_ble_rx_until = 0;
    _overtemp = false;
    _temp_check_at = 0;
  }

  // Drives the LED from the saved settings; called every loop (cheap: the LED is only
  // written when its color changes). In status mode, highest priority first:
  //   radio disabled (fatal) > new error (critical) > TX > RX > BLE RX > busy channel > boot
  //   > over temperature > off
  void updateStatusLed() {
#ifdef HAS_STATUS_LED
    uint16_t new_flags = _err_flags & ~_led_seen_flags;   // track even when the LED is off,
    _led_seen_flags = _err_flags;                         // so enabling it doesn't flash old errors
    if (new_flags & LED_CRITICAL_ERR_MASK) _led_crit_until = futureMillis(LED_CRITICAL_SHOW_MS);

    if (_prefs.led_mode != _led_last_mode) {   // entering command mode starts dark
      if (_prefs.led_mode == LED_MODE_COMMAND) _led_r = _led_g = _led_b = 0;
      _led_last_mode = _prefs.led_mode;
    }

    if (!_prefs.led_enabled) {
      status_led.off();
    } else if (_prefs.led_mode == LED_MODE_COMMAND) {
      status_led.set(_led_r, _led_g, _led_b);
    } else if (_radio_gate == RADIO_GATE_CLOSED) {
      if ((_ms->getMillis() / LED_FATAL_BLINK_MS) & 1) status_led.set(LED_COLOR_FATAL); else status_led.off();
    } else if (!millisHasNowPassed(_led_crit_until)) {
      status_led.set(LED_COLOR_CRITICAL);
    } else if (isSending()) {
      status_led.set(LED_COLOR_TX);
    } else if (!millisHasNowPassed(_led_rx_until)) {
      status_led.set(LED_COLOR_RX);
    } else if (!millisHasNowPassed(_led_ble_rx_until)) {
      status_led.set(LED_COLOR_BLE_RX);
    } else if (isChannelBusy()) {
      status_led.set(LED_COLOR_CAD_BUSY);
    } else if (_led_booting || !millisHasNowPassed(_led_boot_until)) {
      status_led.set(LED_COLOR_BOOT);
    } else if (_overtemp) {
      if ((_ms->getMillis() / LED_OVERTEMP_BLINK_MS) & 1) status_led.set(LED_COLOR_OVERTEMP); else status_led.off();
    } else {
      status_led.off();
    }
#endif
  }

  // every TEMP_CHECK_MS: read the ESP32 core temperature, with hysteresis on the alarm
  void checkTemperature() {
#ifdef HAS_STATUS_LED
    if (!millisHasNowPassed(_temp_check_at)) return;
    _temp_check_at = futureMillis(TEMP_CHECK_MS);

    float celsius;
    if (!board.getMCUTemperature(celsius)) return;
    if (!_overtemp && celsius >= OVERTEMP_C) {
      _overtemp = true;
      MESH_DEBUG_PRINTLN("over temperature: %s C", StrHelper::ftoa(celsius));
    } else if (_overtemp && celsius <= OVERTEMP_C - OVERTEMP_HYST_C) {
      _overtemp = false;
      MESH_DEBUG_PRINTLN("temperature back to normal: %s C", StrHelper::ftoa(celsius));
    }
#endif
  }

#ifdef HAS_STATUS_LED
  bool hasLed() override { return true; }
  void applyLedSettings() override { updateStatusLed(); }
  void setLedColor(uint8_t r, uint8_t g, uint8_t b) override {
    _led_r = r; _led_g = g; _led_b = b;
    updateStatusLed();
  }
  void getLedColor(uint8_t& r, uint8_t& g, uint8_t& b) override { r = _led_r; g = _led_g; b = _led_b; }
#endif

#ifndef RADIO_RECONFIG_WAIT_MS
  #define RADIO_RECONFIG_WAIT_MS  5000   // max wait for an in-flight TX before reconfiguring
#endif

  // The only place radio parameters are changed. Nothing may ever transmit on a config
  // other than the one requested, so:
  //  1. the Dispatcher gate is held: no new TX or RX starts from here on
  //  2. any TX already on the air finishes (it was sent on the previous, valid config)
  //  3. the config is applied; BYOMesh checks every step (see radio_apply_params())
  //  4. success: the Dispatcher is bound to the configured radio and the gate opens
  //     failure: both radios sleep, the TX queue is flushed and the gate stays CLOSED
  //     until a later config succeeds
  bool applyRadioConfig(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word) {
    _radio_gate = RADIO_GATE_HOLD;

    unsigned long until = futureMillis(RADIO_RECONFIG_WAIT_MS);
    while (isSending() && !millisHasNowPassed(until)) {
      mesh::Dispatcher::loop();   // completes the TX; the gate stops anything new starting
    }

    bool ok = !isSending();
    if (!ok) {
      MESH_DEBUG_PRINTLN("applyRadioConfig: TX still in progress, refusing to reconfigure");
    }
#ifdef BYOMESH
    if (ok) ok = radio_apply_params(freq, bw, sf, cr, sync_word);
    if (ok) {
      if (getRadio() != active_radio) {
        setRadio(active_radio);
        MESH_DEBUG_PRINTLN("Dispatcher bound to %s radio", active_radio == &radio_driver_2ghz ? "SX1281" : "SX1276");
      }
      active_radio->begin();   // (re)attach its DIO1 IRQ handler and reset its state
      radio_apply_tx_power(_prefs.tx_power_dbm);   // per radio; a rejected value is not fatal
    }
#else
    if (ok) {
      radio_set_params(freq, bw, sf, cr, sync_word);
      radio_set_tx_power(_prefs.tx_power_dbm);
    }
#endif

    if (!ok) {
      disableRadio();
      return false;
    }
    _radio_gate = RADIO_GATE_OPEN;
    return true;
  }

  void disableRadio() {
    _radio_gate = RADIO_GATE_CLOSED;
    flushOutbound();
#ifdef BYOMESH
    radio_disable_all();
#endif
  }

  // for config changes not started by a CLI command (boot, temp-params revert),
  // so the failure isn't silent. Only in CLI mode: in KISS mode it would corrupt the stream.
  void reportRadioFailure(const char* when) {
    if (_cli.getCLIMode() == CLIMode::CLI) {
      Serial.print("ERROR: radio config failed ("); Serial.print(when);
      Serial.println(") - radio disabled (no RX/TX) until 'set radio' or 'tempradio' succeeds");
    }
  }

  void begin(FILESYSTEM* fs) {
    mesh::Mesh::begin();
    _fs = fs;
    _cli.loadPrefs(_fs);

    _led_last_mode = _prefs.led_mode;   // command mode: off at boot (color isn't saved)
    _led_booting = true;
    updateStatusLed();

    if (!applyRadioConfig(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr, _prefs.sync_word)) {
      reportRadioFailure("boot");
    }

    _led_booting = false;
    _led_boot_until = futureMillis(LED_BOOT_SHOW_MS);
    updateStatusLed();

#ifdef ENABLE_BLE
    NimBLEDevice::init(std::__cxx11::string(BLE_DEVICE_NAME));
    bleScan = NimBLEDevice::getScan(); // Create the scan object.

    if(_prefs.ble_enabled){
      applyBLEParams(
        true,
        _prefs.ble_active_scan,
        _prefs.ble_filter_dups,
        _prefs.ble_max_results,
        _prefs.ble_scantime
      );
    }
#endif
  }

  const char* getFirmwareVer() override { return FIRMWARE_VERSION; }
  const char* getBuildDate() override { return FIRMWARE_BUILD_DATE; }
  const char* getNodeName() { return _prefs.node_name; }
  NodePrefs* getNodePrefs() { 
    return &_prefs; 
  }
  CommonCLI* getCLI() {
    return &_cli;
  }

  void savePrefs() override {
    _cli.savePrefs(_fs);
  }

  bool formatFileSystem() override {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    return InternalFS.format();
#elif defined(RP2040_PLATFORM)
    return LittleFS.format();
#elif defined(ESP32)
    return SPIFFS.format();
#else
    #error "need to implement file system erase"
    return false;
#endif
  }


  // Applied immediately (MeshTNC's CLI is serial-only, so there's no remote reply to wait
  // for), which lets the CLI report the real result instead of a premature "OK".
  bool applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word, int timeout_mins) {
    if (!applyRadioConfig(freq, bw, sf, cr, sync_word)) {
      revert_radio_at = 0;
      return false;
    }
    revert_radio_at = futureMillis(timeout_mins*60*1000);   // schedule when to revert radio params
    return true;
  }

  bool applyRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word) {
    revert_radio_at = 0;   // a permanent config replaces any pending temp-params revert
    return applyRadioConfig(freq, bw, sf, cr, sync_word);
  }


  void applyBLEParams(bool enabled, bool active, bool filter_dups, uint16_t max_results, uint32_t scantime) {
#ifdef ENABLE_BLE
    bleScan->setActiveScan( active );
    bleScan->setMaxResults(max_results);
    bleScan->setDuplicateFilter(filter_dups);
    bleScan->start(scantime, false, true);
#endif
  }

  
  void printBLEPackets(){
#ifdef ENABLE_BLE
    if(_prefs.ble_enabled && !bleScan->isScanning() && !bleReported){

      bleReported = true;
      NimBLEScanResults results = bleScan->getResults();


      for(int i=0; i<results.getCount(); i++){
        const NimBLEAdvertisedDevice* advertisedDevice = results.getDevice(i);

        float rssi = (float) advertisedDevice->getRSSI();

        uint8_t raw[300];
        uint16_t rawLength = 0;

        const uint8_t* addrBuf = advertisedDevice->getAddress().getVal();
        raw[0] = addrBuf[5];
        raw[1] = addrBuf[4];
        raw[2] = addrBuf[3];
        raw[3] = addrBuf[2];
        raw[4] = addrBuf[1];
        raw[5] = addrBuf[0];
        rawLength = 6;

        const uint8_t* payloadBuf = advertisedDevice->getPayload().data();
        uint16_t payloadLength = advertisedDevice->getPayload().size();
        memcpy(raw+rawLength, payloadBuf, payloadLength);
        rawLength += payloadLength;

        CLIMode cli_mode = _cli.getCLIMode();
        if (cli_mode == CLIMode::CLI) {

          CommonCLI* cli = getCLI();
          Serial.printf("%lu", rtc_clock.getCurrentTime());
          Serial.printf(",RXBLE,%.2f,%.2f,", rssi, 0.0f);
          mesh::Utils::printHex(
            Serial,
            raw,
            rawLength
          );
          Serial.println();

        } else if (cli_mode == CLIMode::KISS) {

          uint8_t kiss_rx[CMD_BUF_LEN_MAX];
          KISSModem* kiss = getCLI()->getKISSModem();
          uint16_t kiss_rx_len = kiss->encodeKISSFrame(
            KISSCmd::Data, raw, rawLength, kiss_rx, sizeof(kiss_rx), KISSPort::BLE_Port
          );
          Serial.write(kiss_rx, kiss_rx_len);
          
        }

        blePacketRxCount++;
        _led_ble_rx_until = futureMillis(LED_BLE_RX_SHOW_MS);   // status LED: BLE advertisement
        // a full scan dump is hundreds of ms of blocking serial writes:
        // keep servicing the LoRa radio (and the status LED) between results
        mesh::Dispatcher::loop();
        updateStatusLed();
      }

      bleReported = false;
      bleScan->start(_prefs.ble_scantime, false, true);
    }
#endif
  }

  void setLoggingOn(bool enable) { _logging = enable; }

  void eraseLogFile() override {
    _fs->remove(PACKET_LOG_FILE);
  }

  void dumpLogFile() override {
#if defined(RP2040_PLATFORM)
    File f = _fs->open(PACKET_LOG_FILE, "r");
#else
    File f = _fs->open(PACKET_LOG_FILE);
#endif
    if (f) {
      while (f.available()) {
        int c = f.read();
        if (c < 0) break;
        Serial.print((char)c);
      }
      f.close();
    }
  }


  void setTxPower(uint8_t power_dbm) {
    if (_radio_gate == RADIO_GATE_CLOSED) return;   // radios asleep; applied with the next config
    radio_set_tx_power(power_dbm);
  }

  void clearStats() {
    radio_driver.resetStats();
    resetStats();
  }

  void handleSerialData() {
    _cli.handleSerialData();
  }

  void loop() {
    mesh::Dispatcher::loop();
    checkTemperature();
    updateStatusLed();

    if (revert_radio_at && millisHasNowPassed(revert_radio_at)) {   // revert radio params to orig
      revert_radio_at = 0;  // clear timer
      if (applyRadioConfig(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr, _prefs.sync_word)) {
        MESH_DEBUG_PRINTLN("Radio params restored");
      } else {
        reportRadioFailure("tempradio revert");
      }
    }

#ifdef ENABLE_BLE
    printBLEPackets();
#endif
  }
};

StdRNG fast_rng;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock);

void halt() {
  while (1) ;
}


// Host link baud rate. 115200 unless the variant sets it (BYOMesh: 921600).
// The host side (esp-tap, lora-tun-bridge pacing) must use the same rate.
#ifndef MESHTNC_SERIAL_BAUD
  #define MESHTNC_SERIAL_BAUD 115200
#endif
#ifndef MESHTNC_SERIAL_RX_BUFFER
  #define MESHTNC_SERIAL_RX_BUFFER 1024   // about two worst-case (fully escaped) 255-byte KISS frames
#endif

void setup() {
#ifdef ESP32
  Serial.setRxBufferSize(MESHTNC_SERIAL_RX_BUFFER);   // must be before begin()
#endif
  Serial.begin(MESHTNC_SERIAL_BAUD);
  delay(1000);

  board.begin();
#ifdef HAS_STATUS_LED
  status_led.begin();   // off until the saved LED settings are loaded
#endif

  if (!radio_init()) { halt(); }
#ifdef BYOMESH
  the_mesh.setRadio(active_radio);
#else
  the_mesh.setRadio(&radio_driver);
#endif

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
#else
  #error "need to define filesystem"
#endif
  the_mesh.begin(fs);
}

void loop() {
  if (Serial.available())
    the_mesh.handleSerialData();
  the_mesh.loop();
}
