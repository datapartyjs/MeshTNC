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
  AckData = 0xC,   // ACKMODE (BPQ): data frame with a 2-byte id, acknowledged once sent
  RxInfoData = 0xD,   // TNC -> host: received frame with RX info in front (set kiss rxinfo on)
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

// ACKMODE acknowledgement, TNC -> host, on the KISS port (cmd 0xC; 0x0C on port 0):
//   sent:    FEND 0x0C <id_hi> <id_lo> FEND             (exactly as BPQ ACKMODE)
//   failed:  FEND 0x0C <id_hi> <id_lo> <status> FEND    (MeshTNC extension, status below)
#define KISS_ACK_TX_FAILED   0x01   // radio failed / timed out, refused (interlock), or disabled
#define KISS_ACK_NO_BUFFER   0x02   // all packet buffers in use: host is sending too fast
#define KISS_ACK_BAD_FRAME   0x03   // empty, too large, or not a valid frame

// RX info frame, TNC -> host, when 'set kiss rxinfo on' (cmd 0xD; 0x0D on port 0):
//   FEND 0x0D <ver=0x01> <rssi:2> <snr:1> <rx_ms:4> <frame...> FEND
//   rssi   int16, big-endian, 0.25 dB units (dBm x 4)
//   snr    int8, 0.25 dB units (dB x 4)
//   rx_ms  uint32, big-endian: TNC millis() at the radio's RX-done interrupt (wraps ~49.7 days)
//   frame  exactly what a plain data (0x00) frame would carry
#define KISS_RXINFO_VER       0x01
#define KISS_RXINFO_HDR_LEN   8

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
        _port = KISSPort::LoRa_Port;
    }
    KISSPort getPort() { return _port; };
    void setPort(KISSPort port) { _port = port; };
    void reset() { _len = 0; _esc = false; };
    void parseSerialKISS();
    void handleKISSCommand(uint32_t sender_timestamp, const char* kiss_data, uint16_t len);
    // ACKMODE: tell the host what happened to tagged frame <tag> (KISS mode only)
    void sendAck(uint16_t tag, bool sent, uint8_t status = 0);
    uint16_t encodeKISSFrame(
      const KISSCmd cmd, 
      const uint8_t* data, const int data_len, 
      uint8_t* kiss_buf, const int kiss_buf_size,
      const KISSPort port = KISSPort::None
    ); // returns the size/length of the encoded data/KISS frame
};
