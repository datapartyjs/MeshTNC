#pragma once

#include <RadioLib.h>

// SX128x LoRa IRQ flag bits, from RadioLib. (The old hand-written PREAMBLE_DETECTED
// value 0x4000 is actually RX_TX_TIMEOUT, so a TX/RX timeout read as "channel busy".)
#define SX128X_IRQ_TX_DONE            RADIOLIB_SX128X_IRQ_TX_DONE
#define SX128X_IRQ_HEADER_VALID       RADIOLIB_SX128X_IRQ_HEADER_VALID
#define SX128X_IRQ_PREAMBLE_DETECTED  RADIOLIB_SX128X_IRQ_PREAMBLE_DETECTED

// Max time to wait for BUSY to drop after waking from sleep. With config retention the
// SX128x is normally ready in ~1-2 ms; the generous default covers slow crystal start-up.
#ifndef SX1281_WAKE_TIMEOUT_MS
  #define SX1281_WAKE_TIMEOUT_MS  100
#endif

// SX128x GetStatus: bits [7:5] = circuit mode, 0x6 = TX (datasheet table 11-5)
#define SX128X_STATUS_CIRCUIT_MODE(s) (((s) >> 5) & 0x07)
#define SX128X_CIRCUIT_MODE_TX        0x06

// SX1280 and SX1281 share the same die. Physical parts report "SX1280 V3B" in the
// version string regardless of package markings. SX1281 class requires "SX1281" match
// and will fail. SX1280 is the correct base class.
class CustomSX1281 : public SX1280 {
public:
  CustomSX1281(Module *mod) : SX1280(mod) { }

  // Parameters explicit — no build-flag dependency for frequency/BW/SF/CR/power.
  // Defaults: 2400 MHz, 812.5 kHz BW, SF9, CR4/7, -18 dBm.
  // power is the SX1281's own output (-18..13 dBm). With an external PA (BYOMesh: AT2401C)
  // it is the PA's input drive, so the default is the minimum, not an antenna power.
  bool std_init(float freq = 2400.0, float bw = 812.5, uint8_t sf = 9,
                uint8_t cr = 7, int8_t power = -18, SPIClass* spi = NULL) {

    Serial.println("SX1281::stdinit spi->begin");
    if (spi) spi->begin(/*P_SX1281_SCLK, P_SX1281_MISO, P_SX1281_MOSI*/);

    // Wait for BUSY LOW before RadioLib begin(). At power-on, BUSY is HIGH ~3ms (OTP load).
    // After a serial/RTS reset (CHIP_PU stays high), the chip may still be in sleep from the
    // previous run — crystal startup can take up to ~200ms in this case. Never send any command
    // while BUSY is HIGH: the SX128x datasheet forbids it and will corrupt the state machine.
    {
      unsigned long t0 = millis();
      while (digitalRead(P_SX1281_BUSY) == HIGH) {
        if (millis() - t0 > 1500) {
          Serial.println("ERROR: SX1281 BUSY stuck HIGH after 500ms — hardware fault");
          //return false;
          break;
        }
      }
      Serial.println("SX1281 BUSY LOW — chip ready");
    }

    // Snap bw to nearest valid SX128x LoRa BW: 203.125, 406.25, 812.5, 1625.0 kHz
    {
      static const float valid_bw[] = { 203.125f, 406.25f, 812.5f, 1625.0f };
      float snapped = valid_bw[0];
      float best = fabsf(bw - valid_bw[0]);
      for (int i = 1; i < 4; i++) {
        float d = fabsf(bw - valid_bw[i]);
        if (d < best) { best = d; snapped = valid_bw[i]; }
      }
      if (snapped != bw) {
        Serial.print("WARN: SX1281 BW ");  Serial.print(bw);
        Serial.print(" -> ");              Serial.print(snapped);
        Serial.println(" kHz");
        bw = snapped;
      }
    }

    // SX128x begin(): freq (MHz), bw (kHz), sf, cr, syncWord, power (dBm), preambleLength.
    // (Previously power was passed as the sync word and 12 as the power, so the chip ran at
    // +12 dBm whatever was requested: far above the AT2401C's +5 dBm absolute max input.)
    // Hand the AT2401C enables to RadioLib before begin(), so they are driven from the very
    // first mode change: RX -> RXEN high, TX -> TXEN high, standby/sleep -> both low.
    // (They used to be registered after begin(), leaving them undriven during init.)
    setRfSwitchPins(P_SX1281_RXEN, P_SX1281_TXEN);

    Serial.println("SX1281::stdinit begin");
    int status = begin(freq, bw, sf, cr, RADIOLIB_SX128X_SYNC_WORD_PRIVATE, power, 12);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print("ERROR: SX1281 init failed: ");
      Serial.print(status);
      switch (status) {
        case RADIOLIB_ERR_CHIP_NOT_FOUND:        Serial.println(" (CHIP_NOT_FOUND — SPI or power fault)"); break;
        case RADIOLIB_ERR_INVALID_BANDWIDTH:     Serial.println(" (INVALID_BANDWIDTH)"); break;
        case RADIOLIB_ERR_INVALID_SPREADING_FACTOR: Serial.println(" (INVALID_SPREADING_FACTOR)"); break;
        case RADIOLIB_ERR_INVALID_CODING_RATE:   Serial.println(" (INVALID_CODING_RATE)"); break;
        case RADIOLIB_ERR_INVALID_FREQUENCY:     Serial.println(" (INVALID_FREQUENCY)"); break;
        case RADIOLIB_ERR_INVALID_OUTPUT_POWER:  Serial.println(" (INVALID_OUTPUT_POWER)"); break;
        case RADIOLIB_ERR_SPI_CMD_TIMEOUT:       Serial.println(" (SPI_CMD_TIMEOUT)"); break;
        default:                                 Serial.println();
      }
      return false;
    }

    Serial.println("SX1281::stdinit twiddle settings");

    // Private LoRa sync word (matches SX127x/SX126x 0x12 convention)
    setSyncWord(0x12);
    setCRC(2);  // 2-byte CRC

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

  // Sleep with configuration retained, so wake() needs no re-init.
  bool sleepRetain() {
    return SX128x::sleep(true) == RADIOLIB_ERR_NONE;
  }

  // Wake from sleep and return to STDBY_RC. false if the chip never became ready.
  bool wake(uint32_t timeout_ms = SX1281_WAKE_TIMEOUT_MS) {
  #ifdef P_SX1281_RESET
    // NRESET is wired: use RadioLib's own wake-up (NSS pulse inside standby())
    (void)timeout_ms;
    return SX128x::standby(RADIOLIB_SX128X_STANDBY_RC, true) == RADIOLIB_ERR_NONE;
  #else
    // no NRESET line: wake with our own NSS routine, then enter standby normally
    if (!nssWake(timeout_ms)) return false;
    return SX128x::standby() == RADIOLIB_ERR_NONE;
  #endif
  }

  // SX128x wakes from sleep on an NSS falling edge and holds BUSY high until it is ready.
  // Commands must never be sent while BUSY is high, so this pulses NSS by hand and waits
  // for BUSY instead of letting RadioLib's SPI layer issue a command into a sleeping chip.
  static bool nssWake(uint32_t timeout_ms = SX1281_WAKE_TIMEOUT_MS) {
    if (digitalRead(P_SX1281_BUSY) == LOW) return true;   // not asleep, nothing to do

    digitalWrite(P_SX1281_NSS, LOW);
    delayMicroseconds(100);
    digitalWrite(P_SX1281_NSS, HIGH);

    unsigned long t0 = millis();
    while (digitalRead(P_SX1281_BUSY) == HIGH) {
      if (millis() - t0 > timeout_ms) return false;   // chip missing, unpowered or stuck
      delayMicroseconds(50);
    }
    return true;
  }

  bool isTxDone() {
    return (getIrqFlags() & SX128X_IRQ_TX_DONE) != 0;
  }

  bool isTransmitting() {
    return SX128X_STATUS_CIRCUIT_MODE(SX128x::getStatus()) == SX128X_CIRCUIT_MODE_TX;
  }

};
