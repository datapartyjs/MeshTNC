#pragma once

#include <MeshCore.h>

namespace mesh {

/**
 * \brief  The fundamental transmission unit.
*/
class Packet {
public:
  Packet();

  uint16_t payload_len;
  uint8_t payload[MAX_PACKET_PAYLOAD];
  int8_t _snr;

  // KISS ACKMODE: set when the host asked to be told once this packet has been sent
  bool tx_tagged;
  uint16_t tx_tag;

  float getSNR() const { return ((float)_snr) / 4.0f; }

  /**
   * \returns  the encoded/wire format length of this packet
   */
  int getRawLength() const;

  /**
   * \brief  save entire packet as a blob
   * \param dest  (OUT) destination buffer (assumed to be MAX_MTU_SIZE)
   * \returns  the packet length
   */
  uint8_t writeTo(uint8_t dest[]) const;

  /**
   * \brief  restore this packet from a blob (as created using writeTo())
   * \param  src  (IN) buffer containing blob
   * \param  len  the packet length (as returned by writeTo())
   */
  bool readFrom(const uint8_t src[], uint8_t len);
};

}
