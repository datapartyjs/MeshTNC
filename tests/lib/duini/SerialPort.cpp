#include "SerialPort.h"

#include <stdexcept>

void SerialPort::open() {
  try {
    _serial.open(_port, _ec);
    _serial.set_option(serial_port_base::baud_rate(115200));
    _serial.set_option(serial_port_base::character_size(8));
    _serial.set_option(serial_port_base::parity(serial_port_base::parity::none));
    _serial.set_option(serial_port_base::stop_bits(serial_port_base::stop_bits::one));
    _serial.set_option(serial_port_base::flow_control(serial_port_base::flow_control::none));
  } catch(std::exception& e) {
#ifndef NDEBUG
    fprintf(stderr, "error, failed to open serial port '%s': %.*s\r\n", _port, (int)_ec.message().size(), _ec.message().data());
#endif
    throw std::exception(e);
  }
}

size_t SerialPort::write(const std::string command) {
  const_buffer wr_buf(command.data(), command.length());
  size_t wr_len;
  try {
    wr_len = _serial.write_some(wr_buf, _ec);
  } catch (std::exception& e) {
#ifndef NDEBUG
    fprintf(stderr, "error writing to serial port: %.*s\r\n", (int)_ec.message().size(), _ec.message().data());
#else
    throw std::exception(e);
#endif
  }
#ifndef NDEBUG
  fprintf(stderr, "wrote to serial port: %s\r\n", command.c_str());
#endif
  return wr_len;
}

size_t SerialPort::read(mutable_buffer& buf) {
  size_t rd_len;
  while (true) {
    try {
      rd_len = _serial.read_some(buf, _ec);
    } catch(std::exception& e) {
#ifndef NDEBUG
      fprintf(stderr, "error reading serial port: %.*s\r\n", (int)_ec.message().size(), _ec.message().data());
#else
      throw std::exception(e);
#endif
    }
#ifndef NDEBUG
    fprintf(stderr, "%.*s {%i}\r\n", (int)rd_len, (const char *)buf.data(), (int)rd_len);
#endif
    return rd_len;
  }
}