#pragma once

#include <Mesh.h>
#include "KISS.h"

#if defined(ESP32) || defined(RP2040_PLATFORM)
  #include <FS.h>
  #define FILESYSTEM  fs::FS
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <Adafruit_LittleFS.h>
  #define FILESYSTEM  Adafruit_LittleFS

  using namespace Adafruit_LittleFS_Namespace;
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
    uint8_t kiss_port;            // unused (the old "set kiss port"); kept so the prefs file layout stays the same

    // BLE Settings
    bool ble_enabled;         // false
    bool ble_active_scan;     // false
    bool ble_filter_dups;     // true
    uint16_t ble_max_results; // 100
    uint32_t ble_scantime;    // 10s in milliseconds
    uint8_t ble_rxPhyMask;        // BLE_GAP_LE_PHY_ANY_MASK = 0x0F
    uint8_t ble_txPhyMask;        // BLE_GAP_LE_PHY_ANY_MASK = 0x0F

    // Status LED (boards that have one)
    bool led_enabled;
    uint8_t led_mode;             // LED_MODE_*

    // KISS: send received frames as RX info frames (cmd 0x0D) with RSSI, SNR, RX time
    bool kiss_rxinfo;

    // light-sleep the MCU whenever it's idle in KISS mode (boards that support it)
    bool powersave;
};

#define LED_MODE_COMMAND  0   // LED shows the color set with 'set ledrgb' (off at boot)
#define LED_MODE_STATUS   1   // LED shows node status (boot, errors, busy channel, TX)

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

  // MCU light sleep. sleepFor(): seconds < 0 = until 'wake'. The defaults are for boards
  // that don't support it.
  virtual bool supportsSleep() { return false; }
  virtual void sleepFor(long seconds, char* resp) { strcpy(resp, "Error, not supported on this board"); }
  virtual void wakeUp(char* resp) { strcpy(resp, "Error, not supported on this board"); }
  virtual void getSleepInfo(char* resp) { resp[0] = 0; }

  // poweroff: which = 0 for all LoRa radios, else 1.. by frequency. The defaults are for
  // boards that don't support it.
  virtual void powerOffRadios(int which, char* resp) { strcpy(resp, "Error, not supported on this board"); }
  virtual void powerOffBoard(char* resp) { strcpy(resp, "Error, not supported on this board"); }

  // status LED; the defaults are for boards without one
  virtual bool hasLed() { return false; }
  virtual void applyLedSettings() { }   // led_enabled / led_mode changed
  virtual void setLedColor(uint8_t r, uint8_t g, uint8_t b) { }
  virtual void getLedColor(uint8_t& r, uint8_t& g, uint8_t& b) { r = g = b = 0; }
  // both return false if the radio could not be configured as requested; the radio is
  // then disabled (no RX/TX) rather than left on any other configuration
  virtual bool applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word, int timeout_mins) = 0;
  virtual bool applyRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word) = 0;
  virtual void applyBLEParams(bool enabled, bool active, bool filter_dups, uint16_t max_results, uint32_t scantime) = 0;
};

class CommonCLI {
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  mesh::Mesh* _mesh;
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
  static void kissCLI(void* ctx, const char* command, char* resp) {
    ((CommonCLI*)ctx)->handleCLICommand(0, command, resp);
  }

public:
  CommonCLI(mesh::MainBoard& board, mesh::RTCClock& rtc, NodePrefs* prefs, CommonCLICallbacks* callbacks, mesh::Mesh* mesh)
      : _board(&board), _rtc(&rtc), _prefs(prefs), _callbacks(callbacks), _mesh(mesh), _kiss(&_cli_mode, mesh) {
        _cmd[0] = 0;
        _kiss.setCLIHandler(&CommonCLI::kissCLI, this);   // CLI over KISS port 1
      }

  void loadPrefs(FILESYSTEM* _fs);
  void savePrefs(FILESYSTEM* _fs);
  void handleSerialData();
  // no partly received CLI line or KISS frame (safe to light-sleep the MCU)
  bool isIdle() const { return _cmd[0] == 0 && _kiss.isIdle(); }
  CLIMode getCLIMode() { return _cli_mode; };
  KISSModem* getKISSModem() { 
    // this isn't supposed to be here but we're refactoring again for multiple radio support soon and it will change again then
    KISSModem* kiss = &_kiss;
    return kiss;
  };
};
