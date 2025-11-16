#include <string>
#include <cassert>
#include <boost/asio.hpp>
#include <SerialPort.h>

#define BUILDING_DUINI

#ifdef __linux__
#define SERIAL_PORT "/dev/ttyACM0"
#elif _WIN32
#define SERIAL_PORT "\\\\.\\COM4"
#else
#define SERIAL_PORT ""
#endif

using boost::asio::io_context;
using boost::asio::buffer;
using boost::asio::mutable_buffer;

int main () {
  std::vector<uint8_t> input(4096);

  #if defined(NDEBUG) && defined(BOOST_ASIO_HAS_SERIAL_PORT)
  printf("Boost has serial port!\r\n");
  #endif

  io_context ctx;
  SerialPort serial(ctx, SERIAL_PORT);
  serial.open();
  
  std::string  command("get radio\r\n");
  serial.write(command);

  mutable_buffer rd_buf(buffer(input));
  serial.read(rd_buf);

  serial.close();

}