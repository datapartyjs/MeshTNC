#include "KISS.h"

// https://en.wikipedia.org/wiki/KISS_(amateur_radio_protocol)

uint16_t KISSModem::encodeKISSFrame(
  const KISSCmd cmd, 
  const uint8_t* data, const int data_len, 
  uint8_t* kiss_buf, const int kiss_buf_size,
  const KISSPort port
) {
  // begin response
  kiss_buf[0] = KISSFrame::FEND;
  // set KISS port and supplied cmd

  const KISSPort theport = (port == KISSPort::None) ? _port : port;

  uint8_t kiss_cmd =
    ((theport << 4) & KISS_MASK_PORT) |
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
      case KISSFrame::FEND:
        kiss_buf[kiss_buf_len++] = KISSFrame::FESC;
        kiss_buf[kiss_buf_len++] = KISSFrame::TFEND;
        break;
      case KISSFrame::FESC:
        kiss_buf[kiss_buf_len++] = KISSFrame::FESC;
        kiss_buf[kiss_buf_len++] = KISSFrame::TFESC;
        break;
      default:
        kiss_buf[kiss_buf_len++] = data[i];
        break;
    }
  }
  kiss_buf[kiss_buf_len++] = KISSFrame::FEND;    // end response
  return kiss_buf_len;
}

void KISSModem::parseSerialKISS() {
  char* command = _cmd;
  while (Serial.available() && _len < sizeof(_cmd)-1) {
    uint8_t b = Serial.read();
    // handle KISS commands
    switch (b) {
      case KISSFrame::FESC:
        // set escape mode if we encounter a FESC
        if (_esc) { // aborted transmission, double FESC
          _len = 0;
          _esc = false;
        } else { // regular escape
          _esc = true;
        }
        continue;
      case KISSFrame::FEND:
        // a FEND always ends the current frame (and any pending escape);
        // a non-empty frame is handled as a KISS command
        _esc = false;
        if (_len > 0) {
          handleKISSCommand(0, command, _len);
          _len = 0;
        }
        break;
      case KISSFrame::TFESC:
        // literal FESC to cmdbuf if in escape mode, otherwise literal TFESC
        if (_esc) {
          _cmd[_len++] = KISSFrame::FESC;
          _esc = false;
        } else
          _cmd[_len++] = KISSFrame::TFESC;
        break;
      case KISSFrame::TFEND:
        // literal FEND to cmdbuf if in escape mode, otherwise literal TFEND
        if (_esc) {
          _cmd[_len++] = KISSFrame::FEND;
          _esc = false;
        } else
          _cmd[_len++] = KISSFrame::TFEND;
        break;
      default:
        // add byte to command buffer and increment _len,
        // if it is not handled above.
        // eat and discard any unknown escaped byte, and leave escape mode
        if (_esc) _esc = false;
        else _cmd[_len++] = b;
        break;
    }
  }

  // command buffer full without a closing FEND: the frame is far larger than
  // anything the radio can send - drop it instead of transmitting a truncated one
  if (_len >= sizeof(_cmd)-1) {
    _len = 0;
    _esc = false;
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
  const uint16_t kiss_data_len = len - 1;
  kiss_data++; // advance to data

  // this KISS data is from the host to port 0xF
  if (kiss_port == 0xF) {
    switch (kiss_cmd) {
      case KISSCmd::Return:
        _cmd[0] = 0; // reset command buffer
        _len = 0;
        _esc = false;
        *_cli_mode = CLIMode::CLI; // return to CLI mode
        Serial.println("  -> Exiting KISS mode and returning to CLI mode.");
        return;
    }
  }

  // this KISS data is from the host to our KISS port number
  if (kiss_port == _port) {
    switch (kiss_cmd) {
      case KISSCmd::TxDelay:
        // TX delay is ONE BINARY BYTE in 10ms units (not ASCII text)
        if (kiss_data_len > 0) _txdelay = static_cast<uint8_t>(kiss_data[0]) * 10;
        break;
      case KISSCmd::Data: {
        // nothing to send, or more than one LoRa frame can carry: drop
        if (kiss_data_len == 0 || kiss_data_len > MAX_TRANS_UNIT) break;
        const uint8_t* tx_buf = reinterpret_cast<const uint8_t*>(kiss_data);
        // NULL when all packets are queued (host sending faster than the radio
        // can transmit): drop this frame instead of writing through NULL
        mesh::Packet* pkt = _mesh->obtainNewPacket();
        if (pkt == NULL) break;
        if (!pkt->readFrom(tx_buf, static_cast<uint8_t>(kiss_data_len))) {
          _mesh->releasePacket(pkt);   // back to the pool, don't leak it
          break;
        }
        _mesh->sendPacket(pkt, 1/*, _txdelay*/);
        break;
      }
    }
  }
}
