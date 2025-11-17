#include "SerialPort.h"

#include <iostream>
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
    std::string err = std::format(
      "error, failed to open serial port '{}': {}",
      _port, _ec.message()
    );
    throw std::runtime_error(err);
  }
}

size_t SerialPort::write(const std::string command) {
  const_buffer wr_buf(command.data(), command.length());
  size_t wr_len;
  try {
    wr_len = _serial.write_some(wr_buf, _ec);
  } catch (std::exception& e) {
    std::string err = std::format(
      "error writing to serial port '{}': {}",
      _port, _ec.message()
    );
    throw std::runtime_error(err);
  }
#ifndef NDEBUG
  std::cerr << std::format(
    "wrote to serial port '{}': {}",
    _port, command
  ) << std::endl;
#endif
  return wr_len;
}

size_t SerialPort::read(mutable_buffer& buf) {
  size_t rd_len;
  while (true) {
    try {
      rd_len = _serial.read_some(buf, _ec);
    } catch(std::exception& e) {
      std::string err = std::format(
        "error reading serial port '{}': {}",
        _port, _ec.message()
      );
      throw std::runtime_error(err);
    }
#ifndef NDEBUG
    /*std::cerr << std::format(
        "recieved {} bytes from serial port '{}': ",
        (int)rd_len, _port
      ) << std::hex << buf.get_debug_check() << std::endl;*/
#endif
    return rd_len;
  }
}