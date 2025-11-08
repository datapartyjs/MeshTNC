#pragma once

#include <Dispatcher.h>
#include <Arduino.h>

class VolatileRTCClock : public mesh::RTCClock {
  long millis_offset;
public:
  VolatileRTCClock() { millis_offset = 1715770351; } // 15 May 2024, 8:50pm
  uint32_t getCurrentTime() override { return (millis()/1000 + millis_offset); }
  void setCurrentTime(uint32_t time) override { millis_offset = time - millis()/1000; }
};

class ArduinoMillis : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override { return millis(); }
};

class StdRNG : public mesh::RNG {
public:
  void begin(long seed) { randomSeed(seed); }
  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = (::random(0, 256) & 0xFF);
    }
  }
};
