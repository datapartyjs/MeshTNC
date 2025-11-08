#pragma once

#include <Arduino.h>
#include <Dispatcher.h>

enum CLIMode { CLI, KISS };

#define CMD_BUF_LEN_MAX 500

// KISS Definitions
#define KISS_MASK_PORT   0xF0
#define KISS_MASK_CMD    0x0F

enum KISSFrame: uint16_t {
  FEND = 0xC0,
  FESC = 0xDB,
  TFEND = 0xDC,
  TFESC = 0xDD
};

enum KISSCmd: uint8_t {
  Data = 0x0,
  TxDelay = 0x1,
  Persist = 0x2,
  SlotTime = 0x3,
  TxTail = 0x4,
  FullDuplex = 0x5,
  Vendor = 0x6,
  Return = 0xF
};

enum KISSVendorCmd: uint16_t {
  GetRadioStats = 0x01,     // retrieve radio stats, takes no additional arguments
  SignalReporting = 0x80,   // enable SNR and RSSI reporting by setting the following byte to anything other than 0
};

struct RadioStats {
  uint8_t noise;
  uint32_t rx_count;
  uint32_t tx_count;
  unsigned long tx_airtime;
  uint32_t uptime;
};

struct SignalReport {
  unsigned long long timestamp;
  uint8_t rssi;
  int8_t snr;
  uint16_t pkt_len;
};

class KISSModem {
  uint16_t _len;
  bool _esc;
  uint32_t _txdelay;
  uint8_t _port;
  char _cmd[CMD_BUF_LEN_MAX];

  mesh::Dispatcher* _dispatcher;
  CLIMode* _cli_mode;

  void handleVendorCommand(uint32_t sender_timestamp, const char* vendor_data, uint16_t len);

  public:
    KISSModem(CLIMode* cli_mode, mesh::Dispatcher* dispatcher) 
      : _cli_mode(cli_mode), _dispatcher(dispatcher)
    {
      _len = 0;
      _esc = false;
      _txdelay = 0;
    }
    uint8_t getPort() { return _port; };
    void setPort(uint8_t port) { _port = port; };
    void reset();
    void parseSerialKISS();
    void handleKISSCommand(uint32_t sender_timestamp, const char* kiss_data, const uint16_t len);
    uint16_t encodeKISSFrame(
      const KISSCmd cmd, 
      const uint8_t* data, const int data_len, 
      uint8_t* kiss_buf, const int kiss_buf_size
    ); // returns the size/length of the encoded data/KISS frame
};
