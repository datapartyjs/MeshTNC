#pragma once

#ifdef BUILDING_LIBMESHTNC
#define MESHTNC_API __declspec(dllexport)
#else
#define MESHTNC_API __declspec(dllimport)
#endif

#include "libtnc_exports.h"

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

extern "C" class LIBTNC_EXPORTS SerialPort {
  // Constructor set
  io_context& _ctx;
  const char* _port;

  // Ephemeral
  error_code _ec;

  // Structural
  serial_port _serial;

  public:
    SerialPort(io_context &ctx, const char* port)
      : _ctx(ctx), _port(port), _serial(_ctx)
    {

    }
    void open();
    void close() { _serial.close(); };
    size_t write(const std::string command);
    size_t read(mutable_buffer& buf);
};
