#pragma once

#include "duini.h"

#include <cstdint>
#include <chrono>
#include <ctime>

using namespace std::chrono;

// millis() definition
extern DUINI_EXPORTS unsigned long millis();

// AutoDiscoverRTCClock definition
extern class DUINI_EXPORTS AutoDiscoverRTCClock {
  // TODO: understand and fix this thing
  public:
  AutoDiscoverRTCClock() {}
  uint32_t getCurrentTime();
};
extern AutoDiscoverRTCClock DUINI_EXPORTS rtc_clock;

// DateTime definition
extern class DUINI_EXPORTS DateTime {
  std::tm _time_tm;

  public:
  DateTime(uint32_t time) {
    // Convert Unix timestamp to std::tm structure
    auto tp = system_clock::from_time_t(time);
    std::time_t t = system_clock::to_time_t(tp);
    std::tm* tmp = std::gmtime(&t);
    _time_tm = *tmp;
  }
  int second();
  int minute();
  int hour();
  int day();
  int month();
  int year();
};