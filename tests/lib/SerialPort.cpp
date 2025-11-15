#include "SerialPort.h"

void SerialPort::open() {
  try {
    _serial.open(_port);
    if (!_serial.is_open()) {
      printf("FATAL: Failed to open serial port '%s'\r\n", _port);
      throw std::exception("Serial port cannot be opened!");
    }
    _serial.set_option(serial_port_base::baud_rate(115200));
    _serial.set_option(serial_port_base::character_size(8));
    _serial.set_option(serial_port_base::parity(serial_port_base::parity::none));
    _serial.set_option(serial_port_base::stop_bits(serial_port_base::stop_bits::one));
    _serial.set_option(serial_port_base::flow_control(serial_port_base::flow_control::none));
  } catch(std::exception e) {
#ifdef DEBUG
    printf("error, failed to open serial port: %s\r\n", __addgsbyte
#endif
    throw e;
  }
}

size_t SerialPort::write(const std::string command) {
  const_buffer wr_buf(command.data(), command.length());
  size_t wr_len;
  try {
    wr_len = _serial.write_some(wr_buf, _ec);
  } catch (std::exception e) {
#ifdef DEBUG
    printf("error writing to serial port: %s\r\n", _ec.message().c_str());
#else
    throw e;
#endif
  }
#ifdef DEBUG
  printf("wrote to serial port: %s\r\n", command.c_str());
#endif
  return wr_len;
}

size_t SerialPort::read(mutable_buffer& buf) {
  size_t rd_len;
  while (true) {
    try {
      rd_len = _serial.read_some(buf, _ec);
    } catch(std::exception e) {
#ifdef DEBUG
      printf("error reading serial port: %s\r\n", _ec.message().c_str());
#else
      throw e;
#endif
    }
#ifdef DEBUG
    printf("%s {%i}\r\n", rd_buf.data(), rd_len);
#endif
    return rd_len;
  }
}