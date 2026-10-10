#include "Packet.h"
#include <string.h>
#include <SHA256.h>

namespace mesh {

Packet::Packet() {
  payload_len = 0;
  _snr = 0;
  tx_tagged = false;
  tx_tag = 0;
}

int Packet::getRawLength() const {
  return 2 + payload_len;
}

uint8_t Packet::writeTo(uint8_t dest[]) const {
  uint8_t i = 0;
  memcpy(&dest[i], payload, payload_len); i += payload_len;
  return i;
}

bool Packet::readFrom(const uint8_t src[], uint8_t len) {
  uint8_t i = 0;
  if (i >= len) return false;   // bad encoding
  payload_len = len - i;
  if (payload_len > sizeof(payload)) return false;  // bad encoding
  memcpy(payload, &src[i], payload_len); //i += payload_len;
  return true;   // success
}

}