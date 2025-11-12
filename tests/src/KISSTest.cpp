#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <cassert>
#include <boost/asio.hpp>

#define DEBUG true

#ifdef __linux__
#define SERIAL_PORT "/dev/ttyACM0"
#elif _WIN32
#define SERIAL_PORT "\\\\.\\COM4"
#else
#define SERIAL_PORT ""
#endif

using boost::system::error_code;
using boost::asio::io_context;
using boost::asio::serial_port;
using boost::asio::serial_port_base;
using boost::asio::buffer;
using boost::asio::const_buffer;
using boost::asio::mutable_buffer;


serial_port openSerialPort(io_context &ctx, char* port) {
  serial_port serial(ctx);
  try {
    serial.open(port);
    if (!serial.is_open()) {
      printf("FATAL: Failed to open serial port '%s'\r\n", port);
      std::exit(1);
    }
    serial.set_option(serial_port_base::baud_rate(115200));
    serial.set_option(serial_port_base::character_size(8));
    serial.set_option(serial_port_base::parity(serial_port_base::parity::none));
    serial.set_option(serial_port_base::stop_bits(serial_port_base::stop_bits::one));
    serial.set_option(serial_port_base::flow_control(serial_port_base::flow_control::none));
  } catch (std::exception e) {
    printf("FATAL: Failed to initialize serial port "
            "'%s':\r\n%s\r\n", port, e.what());
    std::exit(1);
  }
  return serial;
}

int main () {
  std::vector<uint8_t> input(4096);

  #if defined(DEBUG) && defined(BOOST_ASIO_HAS_SERIAL_PORT)
  printf("Boost has serial port!\r\n");
  #endif

  io_context ctx;
  serial_port serial = openSerialPort(ctx, SERIAL_PORT);
  
  error_code ec;

  std::string  command("get radio\r\n");
  const_buffer wr_buf(command.data(), command.length());
  size_t wr_len;
  try {
    wr_len = serial.write_some(wr_buf, ec);
  } catch(std::exception e) {
    printf("FATAL: writing serial port: %s\r\n", ec.message());
  }
  printf("wrote to serial port: %s\r\n", command.c_str());

  mutable_buffer rd_buf(buffer(input));
  size_t rd_len;
  while (true) {
    try {
      rd_len = serial.read_some(rd_buf, ec);
    } catch(std::exception e) {
      printf("error reading serial port: %s\r\n", ec.message());
    }
    printf("%s {%i}\r\n", rd_buf.data(), rd_len);
  }
  serial.close();

}