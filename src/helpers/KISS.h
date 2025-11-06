#pragma once

#include <Arduino.h>
#include <Mesh.h>

enum CLIMode { CLI, KISS };

#define CMD_BUF_LEN_MAX 500

// KISS Definitions
#define KISS_FEND  0xC0
#define KISS_FESC  0xDB
#define KISS_TFEND 0xDC
#define KISS_TFESC 0xDD

#define KISS_MASK_PORT   0xF0
#define KISS_MASK_CMD    0x0F

#define KISS_CMD_DATA    0x0
#define KISS_CMD_TXDELAY 0x1
#define KISS_CMD_PERSIST 0x2
#define KISS_CMD_SLTTIME 0x3
#define KISS_CMD_TXTAIL  0x4
#define KISS_CMD_FULLDUP 0x5
#define KISS_CMD_VENDOR  0x6
#define KISS_CMD_RETURN  0xF

enum KISSCmd {
  Data = 0x0,
  TxDelay = 0x1,
  Persist = 0x2,
  SlotTime = 0x3,
  TxTail = 0x4,
  FullDuplex = 0x5,
  Vendor = 0x6,
  Return = 0xF
};

class KISSModem {
  uint16_t _len;
  bool _esc;
  uint32_t _txdelay;
  uint8_t _port;
  char _cmd[CMD_BUF_LEN_MAX];

  mesh::Mesh* _mesh;
  CLIMode* _cli_mode;

  public:
    KISSModem(CLIMode* cli_mode, mesh::Mesh* mesh) : _cli_mode(cli_mode), _mesh(mesh) {
        _len = 0;
        _esc = false;
        _txdelay = 0;
    }
    uint8_t getPort() { return _port; };
    void setPort(uint8_t port) { _port = port; };
    void reset() {_len = 0; };
    void parseSerialKISS();
    void handleKISSCommand(uint32_t sender_timestamp, const char* kiss_data, uint16_t len);
    uint16_t encodeKISSFrame(
      const KISSCmd cmd, 
      const uint8_t* data, const int data_len, 
      uint8_t* kiss_buf, const int kiss_buf_size
    ); // returns the size/length of the encoded data/KISS frame
};
