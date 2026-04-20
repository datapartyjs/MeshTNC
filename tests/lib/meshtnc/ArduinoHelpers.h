#pragma once

#include "Dispatcher.h"

extern class MESHTNC_EXPORTS StdRNG : public mesh::RNG {
public:
  void begin(long seed);
  void random(uint8_t* dest, size_t sz) override;
};

extern class MESHTNC_EXPORTS VolatileRTCClock : public mesh::RTCClock {
  long millis_offset;
public:
  VolatileRTCClock();
  unsigned int getCurrentTime() override;
  void setCurrentTime(uint32_t time) override;
};

extern class MESHTNC_EXPORTS ArduinoMillis : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override;
};

// PC compatibilty layer below
#if !defined(ARDUINO)
#include <cstdlib>
#include <iostream>
#include <ctime>

// This is for millis() on PC used in the otherwise portable implementation
// in ArduinoHelpers.cpp
#include <DateTime.h>

class MESHTNC_EXPORTS PCBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  PCBoard();
  void begin();
  uint8_t getStartupReason() const override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;
  void reboot() override;
  bool startOTAUpdate(const char* id, char reply[]) override;
};

class MESHTNC_EXPORTS PCSX1262Wrapper : public mesh::Radio {
public:
  PCSX1262Wrapper(void* radio, void* board);
  virtual int recvRaw(uint8_t* bytes, int sz) override;
  virtual uint32_t getEstAirtimeFor(int len_bytes) override;
  virtual float packetScore(float snr, int packet_len) override;
  virtual bool startSendRaw(const uint8_t* bytes, int len) override;
  virtual bool isSendComplete() override;
  virtual void onSendFinished() override;
  virtual bool isInRecvMode() const override;
  void resetStats();
};

extern MESHTNC_EXPORTS PCSX1262Wrapper radio_driver;
#endif // !ARDUINO
