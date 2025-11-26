#pragma once

#include "Dispatcher.h"
#include "KISS.h"

#if defined(ESP32) || defined(RP2040_PLATFORM)
  #include <FS.h>
  #define FILESYSTEM  fs::FS
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <Adafruit_LittleFS.h>
  #define FILESYSTEM  Adafruit_LittleFS

  using namespace Adafruit_LittleFS_Namespace;
#elif !defined(ARDUINO)
  #include <stdint.h>
  #include <FileSystem.h>
#endif

#define CMD_BUF_LEN_MAX 500

struct NodePrefs {  // persisted to file
    float airtime_factor;
    char node_name[32];
    double node_lat, node_lon;
    float freq;
    uint8_t tx_power_dbm;
    float rx_delay_base;
    float tx_delay_factor;
    uint32_t guard;
    uint8_t sf;
    uint8_t cr;
    float bw;
    uint8_t interference_threshold;
    uint8_t agc_reset_interval;   // secs / 4
    uint8_t sync_word;
    bool log_rx;
    // KISS Config
    uint8_t kiss_port;

    // BLE Settings
    bool ble_enabled;         // false
    bool ble_active_scan;     // false
    bool ble_filter_dups;     // true
    uint16_t ble_max_results; // 100
    uint32_t ble_scantime;    // 10s in milliseconds
    uint8_t ble_rxPhyMask;        // BLE_GAP_LE_PHY_ANY_MASK = 0x0F
    uint8_t ble_txPhyMask;        // BLE_GAP_LE_PHY_ANY_MASK = 0x0F
};

class CommonCLICallbacks {
public:
  virtual void savePrefs() = 0;
  virtual const char* getFirmwareVer() = 0;
  virtual const char* getBuildDate() = 0;
  virtual bool formatFileSystem() = 0;
  virtual void setLoggingOn(bool enable) = 0;
  virtual void eraseLogFile() = 0;
  virtual void dumpLogFile() = 0;
  virtual void setTxPower(uint8_t power_dbm) = 0;
  virtual void clearStats() = 0;
  virtual void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word, int timeout_mins) = 0;
  virtual void applyRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word) = 0;
  virtual void applyBLEParams(bool enabled, bool active, bool filter_dups, uint16_t max_results, uint32_t scantime) = 0;
};

class CommonCLI {
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  mesh::Dispatcher* _dispatcher;
  CommonCLICallbacks* _callbacks;
  mesh::MainBoard* _board;
  CLIMode _cli_mode = CLIMode::CLI;
  char _tmp[80];
  char _cmd[CMD_BUF_LEN_MAX];
  KISSModem _kiss;

  mesh::RTCClock* getRTCClock() { return _rtc; }
  void savePrefs();
  void loadPrefsInt(FILESYSTEM* _fs, const char* filename);
  void parseSerialCLI();
  void handleCLICommand(uint32_t sender_timestamp, const char* command, char* resp);

public:
  CommonCLI(mesh::MainBoard& board, mesh::RTCClock& rtc, NodePrefs* prefs, CommonCLICallbacks* callbacks, mesh::Dispatcher* dispatcher)
      : _board(&board), _rtc(&rtc), _prefs(prefs), _callbacks(callbacks), _dispatcher(dispatcher), _kiss(&_cli_mode, dispatcher) {
        _cmd[0] = 0;
      }

  void loadPrefs(FILESYSTEM* _fs);
  void savePrefs(FILESYSTEM* _fs);
  void handleSerialData();
  CLIMode getCLIMode() { return _cli_mode; };
  KISSModem* getKISSModem() { 
    // this isn't supposed to be here but we're refactoring again for multiple radio support soon and it will change again then
    KISSModem* kiss = &_kiss;
    return kiss;
  };
};
