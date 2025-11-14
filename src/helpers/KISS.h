#pragma once

#include <Arduino.h>
#include <Mesh.h>

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

enum KISSPort: uint8_t {
  LoRa_Port = 0x0,
  GPS_Port = 0x1,
  BLE_Port = 0x2,
  WiFi_Port = 0x3,
  Global_Port = 0xf,
  None = 0xff
};

class KISSModem {
  uint16_t _len;
  bool _esc;
  uint32_t _txdelay;
  KISSPort _port;
  char _cmd[CMD_BUF_LEN_MAX];

  mesh::Mesh* _mesh;
  CLIMode* _cli_mode;

  public:
    KISSModem(CLIMode* cli_mode, mesh::Mesh* mesh) : _cli_mode(cli_mode), _mesh(mesh) {
        _len = 0;
        _esc = false;
        _txdelay = 0;
    }
    KISSPort getPort() { return _port; };
    void setPort(KISSPort port) { _port = port; };
    void reset() {_len = 0; };
    void parseSerialKISS();
    void handleKISSCommand(uint32_t sender_timestamp, const char* kiss_data, uint16_t len);
    uint16_t encodeKISSFrame(
      const KISSCmd cmd, 
      const uint8_t* data, const int data_len, 
      uint8_t* kiss_buf, const int kiss_buf_size,
      const KISSPort port = KISSPort::None
    ); // returns the size/length of the encoded data/KISS frame
};
