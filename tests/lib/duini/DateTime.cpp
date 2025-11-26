#include "DateTime.h"

uint32_t AutoDiscoverRTCClock::getCurrentTime() {
  return std::time(nullptr);
}

int DateTime::second() { return _time_tm.tm_sec; }
int DateTime::minute() { return _time_tm.tm_min; }
int DateTime::hour() { return _time_tm.tm_hour; }
int DateTime::day() { return _time_tm.tm_mday; }
int DateTime::month() { return _time_tm.tm_mon + 1; } // tm_mon is 0-based
int DateTime::year() { return _time_tm.tm_year + 1900;  } // tm_year is years since 1900