#include <string>
#include <iostream>
#include <cassert>
#include <SerialPort.h>

#ifdef __linux__
#define SERIAL_PORT "/dev/ttyACM0"
#elif _WIN32
#define SERIAL_PORT "\\\\.\\COM4"
#else
#define SERIAL_PORT ""
#endif

int main () {
  std::vector<uint8_t> input(4096);

  #if defined(NDEBUG) && defined(BOOST_ASIO_HAS_SERIAL_PORT)
  printf("Boost has serial port!\r\n");
  #endif

  Serial.open(SERIAL_PORT);
  
  std::string command("get radio");

  std::cout << "Sending command: " << command << std::endl;
  
  command.append("\r\n");
  Serial.write(command.data(), command.length());

  std::cout << "Response:" << std::endl;
  while (Serial.available()) {
    std::cout << std::hex << Serial.read();
  }
  std::cout << std::endl;

  Serial.close();
}