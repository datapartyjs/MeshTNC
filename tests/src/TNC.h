#include <Utils.h>
#include <TxtDataHelpers.h>
#include <StaticPoolPacketManager.h>
#include <Dispatcher.h>
#include <KISS.h>
#include <CommonCLI.h>
#include <ArduinoHelpers.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#elif !defined(ARDUINO)
  #include <FileSystem.h>
  #include <SerialPort.h>
  #include <DateTime.h>
  #include <RadioDriver.h>
#endif

#define PACKET_LOG_FILE  "/packet_log"

#define ADVERT_NAME "MeshTNC"
#define ADVERT_LAT  0.0
#define ADVERT_LON  0.0
#define LORA_FREQ   915.0
#define LORA_SF     7
#define LORA_BW     250.0
#define LORA_CR     5
#define LORA_TX_PWR 1

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1"
#endif

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "1 Aug 2025"
#endif

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
      Serial.print(',');
      // TODO: Fix this crap
#ifdef ARDUINO
      mesh::Utils::printHex(Serial, raw, len); // Look at TODO
#endif
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
    _prefs.tx_power_dbm = LORA_TX_PWR;
    _prefs.interference_threshold = 0;  // disabled
    _prefs.sync_word = 0x2B;
    _prefs.log_rx = true;
    _prefs.ble_enabled = false;
    _prefs.ble_filter_dups = true;
    _prefs.ble_active_scan = false;
    _prefs.ble_max_results = 100;
    _prefs.ble_scantime = 10 * 1000;
  }

  void begin(FILESYSTEM* fs);

  mesh::RNG* getRNG() const;
  mesh::RTCClock* getRTCClock() const;
  const char* getFirmwareVer() override;
  const char* getBuildDate() override;
  const char* getNodeName();
  NodePrefs* getNodePrefs();
  CommonCLI* getCLI();
  void savePrefs() override;
  bool formatFileSystem() override;

  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word, int timeout_mins);
  void applyRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word);

  void applyBLEParams(bool enabled, bool active, bool filter_dups, uint16_t max_results, uint32_t scantime);  
  void printBLEPackets();

  void setLoggingOn(bool enable);
  void eraseLogFile() override;
  void dumpLogFile() override;

  void setTxPower(uint8_t power_dbm);

  void clearStats();

  void handleSerialData();
  void loop();
};
