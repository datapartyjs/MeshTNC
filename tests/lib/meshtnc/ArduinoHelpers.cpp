#include "ArduinoHelpers.h"

VolatileRTCClock::VolatileRTCClock() {
    millis_offset = 1715770351; // 15 May 2024, 8:50pm
}
uint32_t VolatileRTCClock::getCurrentTime() {
    return (millis()/1000 + millis_offset);
}
void VolatileRTCClock::setCurrentTime(uint32_t time) {
    millis_offset = time - millis()/1000;
}

unsigned long ArduinoMillis::getMillis() { return millis(); }

#ifdef ARDUINO
// Arduino StdRNG implementation
void StdRNG::begin(long seed) { randomSeed(seed); }
void StdRNG::random(uint8_t* dest, size_t sz) override {
  for (int i = 0; i < sz; i++) {
    dest[i] = (::random(0, 256) & 0xFF);
  }
}
#else
#include <cstdlib>
#include <iostream>
#include <ctime>

// PC StdRNG implementation
void StdRNG::begin(long seed) { std::srand(std::time({})); }
void StdRNG::random(uint8_t* dest, size_t sz) {
  for (int i = 0; i < sz; i++) {
    dest[i] = (uint8_t)(std::rand() & 0xFF);
  }
}

// PCSX1262Wrapper implementation
PCSX1262Wrapper::PCSX1262Wrapper(void* radio, void* board) { };

int PCSX1262Wrapper::recvRaw(uint8_t* bytes, int sz) {
  //std::cerr << "PCSX1260Wrapper::recvRaw(xxx," << sz << ") called, returning 0" << std::endl;
  return 0;
};
uint32_t PCSX1262Wrapper::getEstAirtimeFor(int len_bytes) {
  std::cerr << "PCSX1260Wrapper::getEstAirtimeFor(" << len_bytes << ") called, returning 0" << std::endl;
  return 0;
};
float PCSX1262Wrapper::packetScore(float snr, int packet_len) {
  std::cerr << "PCSX1260Wrapper::packetScore(" << snr << "," << packet_len << ") called, returning 0.0" << std::endl;
  return 0.0;
};
bool PCSX1262Wrapper::startSendRaw(const uint8_t* bytes, int len) {
  std::cerr << "PCSX1260Wrapper::startSendRaw(xxx," << len << ") called, returning true" << std::endl;
  return true;
};
bool PCSX1262Wrapper::isSendComplete() {
  std::cerr << "PCSX1260Wrapper::isSendComplete() called, returning true" << std::endl;
  return true;
};
void PCSX1262Wrapper::onSendFinished() {};
bool PCSX1262Wrapper::isInRecvMode() const { return true; };
void PCSX1262Wrapper::resetStats() {};

extern PCSX1262Wrapper radio_driver(0, 0);

// PCBoard implementation
PCBoard::PCBoard() {};

void PCBoard::begin() { };
uint8_t PCBoard::getStartupReason() const { return 0; };
uint16_t PCBoard::getBattMilliVolts() {
  return 65535;
};
const char* PCBoard::getManufacturerName() const  {
#if defined(WIN32) || defined(WIN64)
  return "Windows PC";
#elif defined(__linux__)
  return "Linux PC";
#else 
  return "Unknown PC";
#endif
};
void PCBoard::reboot() { };
bool PCBoard::startOTAUpdate(const char* id, char reply[])  { return false; };
#endif