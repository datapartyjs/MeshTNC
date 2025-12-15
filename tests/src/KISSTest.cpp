#include <string>
#include <iostream>
#include <cctype>
#include <chrono>
#include <thread>
#include <cassert>

#include <SerialPort.h>
#include <FileSystem.h>

#include <Dispatcher.h>
#include <ArduinoHelpers.h>
#include <KISS.h>
#include "TNC.h"

#ifdef __linux__
// this is a link to a pty/tty in my development environment
#define SERIAL_PORT "serial.port"
#elif _WIN32
#define SERIAL_PORT "\\\\.\\COM5"
#else
#define SERIAL_PORT ""
#endif

PCBoard board;
ArduinoMillis ms;
VolatileRTCClock rtc;
StdRNG fast_rng;

MyMesh disp(board, radio_driver, ms, fast_rng, rtc);

int main () {
  using namespace std::chrono_literals;

  #if defined(NDEBUG) && defined(BOOST_ASIO_HAS_SERIAL_PORT)
  printf("Boost has serial port!\r\n");
  #endif

  PCFileSystem.begin("MeshTNC.data/");
  Serial.open(SERIAL_PORT);

  CLIMode cli_mode = CLIMode::KISS;
  
  KISSModem kiss = KISSModem(&cli_mode, &disp);

  board.begin();

  if (!radio_init()) { exit(1); }

  fast_rng.begin(radio_get_rng_seed());

  disp.begin(&PCFileSystem);

  while (true) {
    if (Serial.available())
      disp.handleSerialData();
    disp.loop();
  }

// going to reuse this later for the actual test harness
/*
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
*/

  Serial.close();

  return 0;
}