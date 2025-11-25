#pragma once

#include "duini.h"

#include <cstdint>
#include <chrono>
#include <ctime>

extern class DUINI_EXPORTS DateTime {
  std::tm _time_tm;

  public:
  DateTime(uint32_t time) {
    // Convert Unix timestamp to std::tm structure
    auto tp = std::chrono::system_clock::from_time_t(time);
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm* tmp = std::gmtime(&t);
    _time_tm = *tmp;
  }
  int second() { return _time_tm.tm_sec; }
  int minute() { return _time_tm.tm_min; }
  int hour() { return _time_tm.tm_hour; }
  int day() { return _time_tm.tm_mday; }
  int month() { return _time_tm.tm_mon + 1; } // tm_mon is 0-based
  int year() { return _time_tm.tm_year + 1900;  } // tm_year is years since 1900
};