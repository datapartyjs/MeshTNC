#include "SerialPort.h"

#include <iostream>
#include <stdexcept>

SerialPort Serial = SerialPort();

void SerialPort::close() {
  if (!_open) return;
  try {
    _boost.serial.close(_boost.ec);
  } catch (std::exception& e) {
    std::string err = std::format(
      "error, failed to close serial port '{}': {}",
      _port, _boost.ec.message()
    );
    std::cerr << err << std::endl;
    throw e;
  }
  _open = false;
}

void SerialPort::open(const char* port) {
  if(_open) return;
  strncpy(_port, port, strlen(port));
  try {
    _boost.serial.open(_port, _boost.ec);
    _boost.serial.set_option(serial_port_base::baud_rate(115200));
    _boost.serial.set_option(serial_port_base::character_size(8));
    _boost.serial.set_option(serial_port_base::parity(serial_port_base::parity::none));
    _boost.serial.set_option(serial_port_base::stop_bits(serial_port_base::stop_bits::one));
    _boost.serial.set_option(serial_port_base::flow_control(serial_port_base::flow_control::none));
  } catch(std::exception& e) {
    std::string err = std::format(
      "error, failed to open serial port '{}': {}",
      _port, _boost.ec.message()
    );
    std::cerr << err << std::endl;
    throw e;
  }
  _open = true;
}

size_t SerialPort::println(const char* val) {
  size_t len = strlen(val);
  std::string buffer;
  buffer.reserve(len + 2);
  buffer.append(val, len);
  buffer.append("\r\n", 2);
  return write(buffer.c_str(), buffer.size());
}

size_t SerialPort::write(const uint8_t val) {
  size_t written = write(&val, 1);
  return written;
}

size_t SerialPort::write(const char* str) {
  size_t len = strlen(str);
  if (len == 0) { return 0; }
  size_t written = write(str, len);
  return written;
}

size_t SerialPort::write(const char* buf, size_t len) {
  return write((const uint8_t*)buf, len);
}

size_t SerialPort::write(const uint8_t* buf, size_t len) {
  const_buffer wr_buf(buf, len);
  size_t wr_len;
  try {
    wr_len = _boost.serial.write_some(wr_buf, _boost.ec);
  } catch (std::exception& e) {
    std::string err = std::format(
      "error writing to serial port '{}': {}",
      _port, _boost.ec.message()
    );
    std::cerr << err << std::endl;
    throw e;
  }
#ifndef NDEBUG
  std::string data("");
  for (size_t i = 0; i < len; i++) {
    data.append(std::format("{:x}", buf[i]));
  }
  std::cerr << std::format(
    "wrote to serial port '{}': {}",
    _port, data
  ) << std::endl;
#endif
  return wr_len;
}

int SerialPort::read() {
  uint8_t data;
  mutable_buffer buf(&data, 1);
  size_t rd_len;
  while (true) {
    try {
      rd_len = _boost.serial.read_some(buf, _boost.ec);
    } catch(std::exception& e) {
      std::string err = std::format(
        "error reading serial port '{}': {}",
        _port, _boost.ec.message()
      );
      std::cerr << err << std::endl;
      throw e;
    }
#ifndef NDEBUG
    std::cerr << std::format(
        "recieved {} bytes from serial port '{}': ",
        (int)rd_len, _port
      ) << std::endl;
      // TODO: print bytes here
#endif
    if (rd_len > 0) {
      return data;
    } else {
      return -1; // this arduino stuff is nasty :(
    }
  }
}

// thank you Tanner Sansbury https://stackoverflow.com/a/28268150
unsigned int SerialPort::available() {
  error_code error;
  int bytes_avail = 0;
#if defined(BOOST_ASIO_WINDOWS) || defined(__CYGWIN__)
  COMSTAT status;
  if (
    ::ClearCommError(
      _boost.serial.lowest_layer().native_handle(),
      NULL,
      &status
    ) != 0
  ) { // success
    bytes_avail = status.cbInQue;
  } else {
    error = error_code(
      ::GetLastError(), 
      boost::asio::error::get_system_category()
    );
    _boost.ec = error;
    throw; // TODO: throw proper exception
  }
#else // TODO: define as linux, figure out BSD, Apple, etc
  if (
    ::ioctl(
      _boost._serial.lowest_layer().native_handle(),
      FIONREAD,
      &bytes_avail
    ) == 0
  ) { // failure
    error = error_code(
      errno,
      boost::asio::error::get_system_category()
    );
    _ec = error;
    throw; // TODO: throw proper exception
  }
#endif
  return bytes_avail;
}