#include "DateTime.h"

class PCMillis {
  time_point<high_resolution_clock> _start;
public:
  PCMillis() {
    _start = high_resolution_clock::now();
  };
  unsigned long millis() {
    auto ns = high_resolution_clock::now() - _start;
    unsigned long ms = ns.count() / 1000;
    return ms;
  }
};

PCMillis _millis = PCMillis();
AutoDiscoverRTCClock rtc_clock = AutoDiscoverRTCClock();

unsigned long millis() {
  return _millis.millis();
}

uint32_t AutoDiscoverRTCClock::getCurrentTime() {
  return std::time(nullptr);
}

int DateTime::second() { return _time_tm.tm_sec; }
int DateTime::minute() { return _time_tm.tm_min; }
int DateTime::hour() { return _time_tm.tm_hour; }
int DateTime::day() { return _time_tm.tm_mday; }
int DateTime::month() { return _time_tm.tm_mon + 1; } // tm_mon is 0-based
int DateTime::year() { return _time_tm.tm_year + 1900;  } // tm_year is years since 1900