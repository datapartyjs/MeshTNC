#pragma once

#include "duini.h"

#include <cstdlib>
#include <string>

#include <boost/asio.hpp>

using boost::system::error_code;
using boost::asio::io_context;
using boost::asio::serial_port;
using boost::asio::serial_port_base;
using boost::asio::buffer;
using boost::asio::const_buffer;
using boost::asio::mutable_buffer;

struct BoostPrivate {
  error_code ec = error_code();
  io_context ctx = io_context();
  serial_port serial = serial_port(ctx);
};

class DUINI_EXPORTS SerialPort {
  char _port[64];
  bool _open;
  BoostPrivate _boost;

  public:
    SerialPort()
    {
      _open = false;
    }
    void open(const char* port);
    void close();
    bool isOpen() { return _open; };
    // Arduino compatible
    size_t write(uint8_t val);
    size_t write(const char* str);
    size_t write(const char* buf, size_t len);
    size_t write(const uint8_t* buf, size_t len);
    size_t println(const char* val);
    size_t print(char val);

    // TODO: Implement!
    size_t println();
    size_t print(int val);
    size_t print(float val);
    size_t print(char* val);
    size_t printf(const char* fmt, ...);

    int read();
    unsigned int available();
};

extern DUINI_EXPORTS SerialPort Serial;
