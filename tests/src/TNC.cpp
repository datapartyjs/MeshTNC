#include "TNC.h"


void MyMesh::begin(FILESYSTEM* fs) {
  Dispatcher::begin();
  _fs = fs;
  _cli.loadPrefs(_fs);

  radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr, _prefs.sync_word);
  radio_set_tx_power(_prefs.tx_power_dbm);

#ifdef ENABLE_BLE
  NimBLEDevice::init(std::__cxx11::string(BLE_DEVICE_NAME));
  bleScan = NimBLEDevice::getScan(); // Create the scan object.

  if(_prefs.ble_enabled){
    applyBLEParams(
      true,
      _prefs.ble_active_scan,
      _prefs.ble_filter_dups,
      _prefs.ble_max_results,
      _prefs.ble_scantime
    );
  }
#endif
}

mesh::RNG* MyMesh::getRNG() const { return _rng; }
mesh::RTCClock* MyMesh::getRTCClock() const { return _rtc; }
const char* MyMesh::getFirmwareVer() { return FIRMWARE_VERSION; }
const char* MyMesh::getBuildDate() { return FIRMWARE_BUILD_DATE; }
const char* MyMesh::getNodeName() { return _prefs.node_name; }
NodePrefs* MyMesh::getNodePrefs() { 
  return &_prefs; 
}
CommonCLI* MyMesh::getCLI() {
  return &_cli;
}
void MyMesh::savePrefs() {
  _cli.savePrefs(_fs);
}

bool MyMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return InternalFS.format();
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  return SPIFFS.format();
#elif !defined(ARDUINO)
  return PCFileSystem.format();
#else
  #error "need to implement file system erase"
  return false;
#endif
}

void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word, int timeout_mins) {
  set_radio_at = futureMillis(2000);   // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;
  pending_sync_word = sync_word;

  revert_radio_at = futureMillis(2000 + timeout_mins*60*1000);   // schedule when to revert radio params
}

void MyMesh::applyRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t sync_word) {
  radio_set_params(freq, bw, sf, cr, sync_word);
}

void MyMesh::applyBLEParams(bool enabled, bool active, bool filter_dups, uint16_t max_results, uint32_t scantime) {
#ifdef ENABLE_BLE
  bleScan->setActiveScan( active );
  bleScan->setMaxResults(max_results);
  bleScan->setDuplicateFilter(filter_dups);
  bleScan->start(scantime, false, true);
#endif
}


void MyMesh::printBLEPackets() {
#ifdef ENABLE_BLE
  if(_prefs.ble_enabled && !bleScan->isScanning() && !bleReported){

    bleReported = true;
    NimBLEScanResults results = bleScan->getResults();


    for(int i=0; i<results.getCount(); i++){
      const NimBLEAdvertisedDevice* advertisedDevice = results.getDevice(i);

      float rssi = (float) advertisedDevice->getRSSI();

      uint8_t raw[300];
      uint16_t rawLength = 0;

      const uint8_t* addrBuf = advertisedDevice->getAddress().getVal();
      raw[0] = addrBuf[5];
      raw[1] = addrBuf[4];
      raw[2] = addrBuf[3];
      raw[3] = addrBuf[2];
      raw[4] = addrBuf[1];
      raw[5] = addrBuf[0];
      rawLength = 6;

      const uint8_t* payloadBuf = advertisedDevice->getPayload().data();
      uint16_t payloadLength = advertisedDevice->getPayload().size();
      memcpy(raw+rawLength, payloadBuf, payloadLength);
      rawLength += payloadLength;

      CLIMode cli_mode = _cli.getCLIMode();
      if (cli_mode == CLIMode::CLI) {

        CommonCLI* cli = getCLI();
        Serial.printf("%lu", rtc_clock.getCurrentTime());
        Serial.printf(",RXBLE,%.2f,%.2f,", rssi, 0.0f);
        mesh::Utils::printHex(
          Serial,
          raw,
          rawLength
        );
        Serial.println();

      } else if (cli_mode == CLIMode::KISS) {

        uint8_t kiss_rx[CMD_BUF_LEN_MAX];
        KISSModem* kiss = getCLI()->getKISSModem();
        uint16_t kiss_rx_len = kiss->encodeKISSFrame(
          KISSCmd::Data, raw, rawLength, kiss_rx, sizeof(kiss_rx), KISSPort::BLE_Port
        );
        Serial.write(kiss_rx, kiss_rx_len);
        
      }

      blePacketRxCount++;
    }

    bleReported = false;
    bleScan->start(_prefs.ble_scantime, false, true);
  }
#endif
}

void MyMesh::setLoggingOn(bool enable) { _logging = enable; }

void MyMesh::eraseLogFile() {
  _fs->remove(PACKET_LOG_FILE);
}

void MyMesh::dumpLogFile() {
#if defined(RP2040_PLATFORM) || !defined(ARDUINO)
  File f = _fs->open(PACKET_LOG_FILE, "r");
#else
  File f = _fs->open(PACKET_LOG_FILE);
#endif
  if (f) {
    while (f.available()) {
      int c = f.read();
      if (c < 0) break;
      Serial.print((char)c);
    }
    f.close();
  }
}


void MyMesh::setTxPower(uint8_t power_dbm) {
  radio_set_tx_power(power_dbm);
}

void MyMesh::clearStats() {
  radio_driver.resetStats();
  resetStats();
}

void MyMesh::handleSerialData() {
  _cli.handleSerialData();
}

void MyMesh::loop() {
  mesh::Dispatcher::loop();

  if (set_radio_at && millisHasNowPassed(set_radio_at)) {   // apply pending (temporary) radio params
    set_radio_at = 0;  // clear timer
    radio_set_params(pending_freq, pending_bw, pending_sf, pending_cr, pending_sync_word);
    MESH_DEBUG_PRINTLN("Temp radio params");
  }

  if (revert_radio_at && millisHasNowPassed(revert_radio_at)) {   // revert radio params to orig
    revert_radio_at = 0;  // clear timer
    radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr, _prefs.sync_word);
    MESH_DEBUG_PRINTLN("Radio params restored");
  }

#ifdef ENABLE_BLE
  printBLEPackets();
#endif
}