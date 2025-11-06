#include "KISS.h"

// https://en.wikipedia.org/wiki/KISS_(amateur_radio_protocol)

uint16_t KISSModem::encodeKISSFrame(
  const KISSCmd cmd, 
  const uint8_t* data, const int data_len, 
  uint8_t* kiss_buf, const int kiss_buf_size
) {
  // begin response
  kiss_buf[0] = KISS_FEND;
  // set KISS port and supplied cmd
  uint8_t kiss_cmd =
    ((_port << 4) & KISS_MASK_PORT) |
    (cmd & KISS_MASK_CMD);
  kiss_buf[1] = kiss_cmd;
  // start after FEND and KISS CMD byte
  uint16_t kiss_buf_len = 2;
  // escape bytes that need escaping
  for (int i = 0; i < data_len; i++) {
    if (kiss_buf_len + 2 > kiss_buf_size) {
      // handle buffer oversize, just truncate packet for now
      // + 2 because 1 byte can become 2 due to the following switch statement
      //     and I'd really like to keep that switch statement simple
      // TODO: error handling?
      break;
    }
    switch (data[i]) {
      case KISS_FEND:
        kiss_buf[kiss_buf_len++] = KISS_FESC;
        kiss_buf[kiss_buf_len++] = KISS_TFEND;
        break;
      case KISS_FESC:
        kiss_buf[kiss_buf_len++] = KISS_FESC;
        kiss_buf[kiss_buf_len++] = KISS_TFESC;
        break;
      default:
        kiss_buf[kiss_buf_len++] = data[i];
        break;
    }
  }
  kiss_buf[kiss_buf_len++] = KISS_FEND;    // end response
  return kiss_buf_len;
}

void KISSModem::parseSerialKISS() {
  char* command = _cmd;
  while (Serial.available() && _len < sizeof(_cmd)-1) {
    uint8_t b = Serial.read();
    // handle KISS commands
    switch (b) {
      case KISS_FESC:
        // set escape mode if we encounter a FESC
        if (_esc) { // aborted transmission, double FESC
          _len = 0;
          _esc = false;
        } else { // regular escape
          _esc = true;
        }
        continue;
      case KISS_FEND:
        // if current command length is greater than 0 and we encounter a FEND,
        // handle the whole command buffer as a KISS command, send length, and
        // then reset length to zero to wait for the next KISS command
        if (_len > 0) {
          // encountered literal FEND while in escape mode. reset escape mode
          if (_esc) _esc = false;
          // handle the command and reset kiss cmdbuf length to 0
          handleKISSCommand(0, command, _len);
          _len = 0;
        }
        break;
      case KISS_TFESC:
        // literal FESC to cmdbuf if in escape mode, otherwise literal TFESC
        if (_esc) {
          _cmd[_len++] = KISS_FESC;
          _esc = false;
        } else
          _cmd[_len++] = KISS_TFESC;
        break;
      case KISS_TFEND:
        // literal FEND to cmdbuf if in escape mode, otherwise literal TFEND
        if (_esc) {
          _cmd[_len++] = KISS_FEND;
          _esc = false;
        } else
          _cmd[_len++] = KISS_TFEND;
        break;
      default:
        // add byte to command buffer and increment _len,
        // if it is not handled above.
        // eat and discard any unknown escaped bytes
        if (!_esc) _cmd[_len++] = b;
        break;
    }
  }

  // check if command buffer is full after reading and processing last byte
  if (_len == sizeof(_cmd)-1) {
    // just send the truncated transmission for now
    // TODO: handle error condition?
    handleKISSCommand(0, command, _len);
    _len = 0;
  }
}

// https://www.ax25.net/kiss.aspx
void KISSModem::handleKISSCommand(
  uint32_t sender_timestamp,
  const char* kiss_data,
  const uint16_t len
){
  if (len == 0) return; // we shouldn't hit this but just in case

  const uint8_t instr_byte = static_cast<uint8_t>(kiss_data[0]);
  
  const uint8_t kiss_port = (instr_byte & 0xF0) >> 4;
  const uint8_t kiss_cmd = instr_byte & 0x0F;

  // kiss port&command are 1 byte, indicate remaining data length
  const uint16_t kiss_data_len = len-1;
  kiss_data++; // advance to data

  // this KISS data is from the host to port 0xF
  if (kiss_port == 0xF) {
    switch (kiss_cmd) {
      case KISS_CMD_RETURN:
        _cmd[0] = 0; // reset command buffer
        *_cli_mode = CLIMode::CLI; // return to CLI mode
        Serial.println("  -> Exiting KISS mode and returning to CLI mode.");
        return;
    }
  }

  // this KISS data is from the host to our KISS port number
  if (kiss_port == _port) {
    switch (kiss_cmd) {
      case KISS_CMD_TXDELAY:
        // TX delay is specified in 10ms units
        if (kiss_data_len > 0) _txdelay = atoi(&kiss_data[0]) * 10;
        break;
      case KISS_CMD_DATA:
        if (kiss_data_len == 0) break;
        const uint8_t* tx_buf = reinterpret_cast<const uint8_t*>(kiss_data);
        mesh::Packet* pkt = _mesh->obtainNewPacket();
        pkt->readFrom(tx_buf, kiss_data_len);
        _mesh->sendPacket(pkt, 1, _txdelay);
        break;
    }
  }
}