#include <Arduino.h>   // needed for PlatformIO
#include <Dispatcher.h>
#include <MeshCore.h>

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


class MyMesh : public mesh::Dispatcher, public CommonCLICallbacks {
  mesh::RTCClock* _rtc;
  mesh::RNG* _rng;
  FILESYSTEM* _fs;
  CommonCLI _cli;
  bool _logging;
  NodePrefs _prefs;
  uint8_t reply_data[MAX_PACKET_PAYLOAD];
  unsigned long set_radio_at, revert_radio_at;
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;
  uint8_t pending_sync_word;

#ifdef ENABLE_BLE
  NimBLEScan* bleScan;
  bool bleReported;
  uint32_t blePacketRxCount;
#endif

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override {
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

  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override {
    return 0;
  }

public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc)
    : mesh::Dispatcher(radio, ms, *new StaticPoolPacketManager(32)), _cli(board, rtc, &_prefs, this, this)
  {
    set_radio_at = revert_radio_at = 0;
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
  }

  void begin(FILESYSTEM* fs) {
    Dispatcher::begin();
    _fs = fs;
    _cli.loadPrefs(_fs);

    radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr, _prefs.sync_word);
    radio_set_tx_power(_prefs.tx_power_dbm);

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

  mesh::RNG* getRNG() const { return _rng; }
  mesh::RTCClock* getRTCClock() const { return _rtc; }
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


  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word, int timeout_mins) {
    set_radio_at = futureMillis(2000);   // give CLI reply some time to be sent back, before applying temp radio params
    pending_freq = freq;
    pending_bw = bw;
    pending_sf = sf;
    pending_cr = cr;
    pending_sync_word = sync_word;

    revert_radio_at = futureMillis(2000 + timeout_mins*60*1000);   // schedule when to revert radio params
  }

  void applyRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word) {
    radio_set_params(freq, bw, sf, cr, sync_word);
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

    if (set_radio_at && millisHasNowPassed(set_radio_at)) {   // apply pending (temporary) radio params
      set_radio_at = 0;  // clear timer
      radio_set_params(pending_freq, pending_bw, pending_sf, pending_cr, pending_sync_word);
      MESH_DEBUG_PRINTLN("Temp radio params");
    }

    if (revert_radio_at && millisHasNowPassed(revert_radio_at)) {   // revert radio params to orig
      revert_radio_at = 0;  // clear timer
      radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr, _prefs.sync_word);
      MESH_DEBUG_PRINTLN("Radio params restored");
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


void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

  if (!radio_init()) { halt(); }

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
