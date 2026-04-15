#pragma once

#include <RadioLib.h>

// SX128x LoRa IRQ flag bits (SX128x datasheet Table 11-76)
#define SX128X_IRQ_HEADER_VALID       0x0010  // LoRa header received and valid
#define SX128X_IRQ_PREAMBLE_DETECTED  0x4000  // LoRa preamble detected

class CustomSX1281 : public SX1281 {
public:
  CustomSX1281(Module *mod) : SX1281(mod) { }

  bool std_init(SPIClass* spi = NULL) {
#ifdef LORA_CR
    uint8_t cr = LORA_CR;
#else
    uint8_t cr = 7;  // 4/7 coding rate, typical for 2.4GHz LoRa
#endif

#if defined(P_LORA_SCLK)
    if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
#endif

    // SX128x begin(): freq (MHz), bw (kHz), sf, cr, power (dBm), preambleLength
    // Valid BW values: 203.125, 406.25, 812.5, 1625.0 kHz
    int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, LORA_TX_POWER, 12);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print("ERROR: SX1281 init failed: ");
      Serial.println(status);
      return false;
    }

    // Set private LoRa sync word (matches SX127x/SX126x 0x12 convention)
    setLoRaSyncWord(0x12);
    setCRC(2);  // 2-byte CRC

#ifdef SX128X_CURRENT_LIMIT
    setCurrentLimit(SX128X_CURRENT_LIMIT);
#endif

#if defined(SX128X_RXEN) || defined(SX128X_TXEN)
  #ifndef SX128X_RXEN
    #define SX128X_RXEN RADIOLIB_NC
  #endif
  #ifndef SX128X_TXEN
    #define SX128X_TXEN RADIOLIB_NC
  #endif
    setRfSwitchPins(SX128X_RXEN, SX128X_TXEN);
#endif

    return true;
  }

  // Pull IRQ status register — SX128x uses getIrqStatus(uint16_t*)
  uint16_t getIrqFlags() {
    uint16_t irq = 0;
    SX128x::getIrqStatus(&irq);
    return irq;
  }

  bool isReceiving() {
    uint16_t irq = getIrqFlags();
    return (irq & SX128X_IRQ_HEADER_VALID) || (irq & SX128X_IRQ_PREAMBLE_DETECTED);
  }

  // Bridge setSyncWord() → setLoRaSyncWord() so generic target.cpp works unchanged
  int16_t setSyncWord(uint8_t syncWord) {
    return setLoRaSyncWord(syncWord);
  }
};
