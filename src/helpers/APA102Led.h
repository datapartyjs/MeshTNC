#pragma once

#include <Arduino.h>

// Single APA102 / APA102-2020 RGB LED, bit-banged on two GPIOs (data + clock), so it
// needs no SPI peripheral. Frame: 32-bit start (zeros), LED frame (0b111 + 5-bit global
// brightness, then blue, green, red), 32-bit end frame.
class APA102Led {
  uint8_t _data, _clk, _brightness;
  uint8_t _r, _g, _b;
  bool _valid;   // false until the first write, so the first set() always reaches the LED

  void writeByte(uint8_t v) {
    for (int i = 0; i < 8; i++) {
      digitalWrite(_data, (v & 0x80) ? HIGH : LOW);
      digitalWrite(_clk, HIGH);
      digitalWrite(_clk, LOW);
      v <<= 1;
    }
  }

public:
  // brightness: APA102 global brightness, 0..31
  APA102Led(uint8_t data_pin, uint8_t clk_pin, uint8_t brightness = 31)
    : _data(data_pin), _clk(clk_pin), _brightness(brightness > 31 ? 31 : brightness),
      _r(0), _g(0), _b(0), _valid(false) { }

  void begin() {
    pinMode(_data, OUTPUT);
    pinMode(_clk, OUTPUT);
    digitalWrite(_data, LOW);
    digitalWrite(_clk, LOW);
    _valid = false;
    set(0, 0, 0);
  }

  // cheap to call repeatedly: only writes to the LED when the color changes
  void set(uint8_t r, uint8_t g, uint8_t b) {
    if (_valid && r == _r && g == _g && b == _b) return;
    _r = r; _g = g; _b = b; _valid = true;

    for (int i = 0; i < 4; i++) writeByte(0x00);   // start frame
    writeByte(0xE0 | _brightness);
    writeByte(b);
    writeByte(g);
    writeByte(r);
    for (int i = 0; i < 4; i++) writeByte(0x00);   // end frame (zeros also suit SK9822 clones)
  }

  void off() { set(0, 0, 0); }
};
