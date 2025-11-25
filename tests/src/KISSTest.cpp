#include <string>
#include <iostream>
#include <cctype>
#include <chrono>
#include <thread>
#include <cassert>

#include <SerialPort.h>

#include <Dispatcher.h>
#include <KISS.h>

#ifdef __linux__
#define SERIAL_PORT "/dev/ttyACM0"
#elif _WIN32
#define SERIAL_PORT "\\\\.\\COM4"
#else
#define SERIAL_PORT ""
#endif

int main () {
  using namespace std::chrono_literals;

  #if defined(NDEBUG) && defined(BOOST_ASIO_HAS_SERIAL_PORT)
  printf("Boost has serial port!\r\n");
  #endif

  Serial.open(SERIAL_PORT);

  //CLIMode cli_mode = CLIMode::KISS;
  //mesh::Dispatcher disp = mesh::Dispatcher();
  
  //KISSModem kiss = KISSModem(&cli_mode, &dispatcher);
  
  std::string command("get radio");

  std::cout << "Sending command: " << command << std::endl;
  
  command.append("\r\n");
  Serial.write(command.data(), command.length());

  // wait some time for a response
  std::this_thread::sleep_for(25ms);

  std::string rx("");
  std::cout << "Response:" << std::endl;
  while (Serial.available()) {
    int val = Serial.read();
    if (0 <= val && val <= 255) {
      if (std::isprint(val)) {
        char character = static_cast<char>(val);
        rx.push_back(character);
      } else {
        rx.append(std::format(" 0x{:02X} ", val));
      }
    }
  }
  std::cout << rx << std::endl;

  Serial.close();

  return 0;
}