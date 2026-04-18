#pragma once

#include <RadioLib.h>

// SX128x LoRa IRQ flag bits (SX128x datasheet Table 11-76)
#define SX128X_IRQ_HEADER_VALID       0x0010  // LoRa header received and valid
#define SX128X_IRQ_PREAMBLE_DETECTED  0x4000  // LoRa preamble detected

// SX1280 and SX1281 share the same die. Physical parts report "SX1280 V3B" in the
// version string regardless of package markings. SX1281 class requires "SX1281" match
// and will fail. SX1280 is the correct base class.
class CustomSX1281 : public SX1280 {
public:
  CustomSX1281(Module *mod) : SX1280(mod) { }

  // Parameters explicit — no build-flag dependency for frequency/BW/SF/CR/power.
  // Defaults: 2400 MHz, 812.5 kHz BW, SF9, CR4/7, 20 dBm — standard 2.4GHz LoRa mesh.
  bool std_init(float freq = 2400.0, float bw = 812.5, uint8_t sf = 9,
                uint8_t cr = 7, int8_t power = 20, SPIClass* spi = NULL) {

    Serial.println("SX1281::stdinit spi->begin");
    if (spi) spi->begin(P_SX1281_SCLK, P_SX1281_MISO, P_SX1281_MOSI);

    // SX128x begin(): freq (MHz), bw (kHz), sf, cr, power (dBm), preambleLength
    // Valid BW: 203.125, 406.25, 812.5, 1625.0 kHz
    Serial.println("SX1281::stdinit begin");
    int status = begin(freq, bw, sf, cr, power, 12);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print("ERROR: SX1281 init failed: ");
      Serial.println(status);
      return false;
    }

    Serial.println("SX1281::stdinit twiddle settings");

    // Private LoRa sync word (matches SX127x/SX126x 0x12 convention)
    setSyncWord(0x12);
    setCRC(2);  // 2-byte CRC

    setRfSwitchPins(P_SX1281_RXEN, P_SX1281_TXEN);

    return true;
  }

  // Pull IRQ status register — RadioLib 7.x: getIrqStatus() returns value directly
  uint32_t getIrqFlags() override {
    return (uint32_t)SX128x::getIrqStatus();
  }

  bool isReceiving() {
    uint32_t irq = getIrqFlags();
    return (irq & SX128X_IRQ_HEADER_VALID) || (irq & SX128X_IRQ_PREAMBLE_DETECTED);
  }

};
