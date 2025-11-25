#include "CommonCLI.h"

#ifdef ARDUINO
  #include <Arduino.h>
  #include "TxtDataHelpers.h"
  #include <RTClib.h>
#else
  #include <DateTime.h>
  #include <SerialPort.h>
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

void CommonCLI::loadPrefs(FILESYSTEM* fs) {
  if (fs->exists("/com_prefs")) {
    loadPrefsInt(fs, "/com_prefs");   // new filename
  } else if (fs->exists("/node_prefs")) {
    loadPrefsInt(fs, "/node_prefs");
    savePrefs(fs);  // save to new filename
    fs->remove("/node_prefs");  // remove old
  }
}

void CommonCLI::loadPrefsInt(FILESYSTEM* fs, const char* filename) {
#if defined(RP2040_PLATFORM) || !defined(ARDUINO)
  File file = fs->open(filename, "r");
#else
  File file = fs->open(filename);
#endif
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *) &_prefs->airtime_factor, sizeof(_prefs->airtime_factor));
    file.read((uint8_t *) &_prefs->node_name, sizeof(_prefs->node_name));
    file.read(pad, 4);
    file.read((uint8_t *) &_prefs->node_lat, sizeof(_prefs->node_lat));
    file.read((uint8_t *) &_prefs->node_lon, sizeof(_prefs->node_lon));
    file.read((uint8_t *) &_prefs->freq, sizeof(_prefs->freq));
    file.read((uint8_t *) &_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));
    file.read((uint8_t *) pad, 1);
    file.read((uint8_t *) &_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));
    file.read((uint8_t *) &_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));
    file.read(pad, 4);
    file.read((uint8_t *) &_prefs->sf, sizeof(_prefs->sf));
    file.read((uint8_t *) &_prefs->cr, sizeof(_prefs->cr));
    file.read((uint8_t *) &_prefs->bw, sizeof(_prefs->bw));
    file.read((uint8_t *) &_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));
    file.read(pad, 3);
    file.read((uint8_t *) &_prefs->interference_threshold, sizeof(_prefs->interference_threshold));
    file.read((uint8_t *) &_prefs->sync_word, sizeof(_prefs->sync_word));
    file.read(pad, 1);
    file.read((uint8_t *) &_prefs->kiss_port, sizeof(_prefs->kiss_port));
    file.read((uint8_t *) &_prefs->ble_enabled, sizeof(_prefs->ble_enabled));
    file.read((uint8_t *) &_prefs->ble_filter_dups, sizeof(_prefs->ble_filter_dups));
    file.read((uint8_t *) &_prefs->ble_active_scan, sizeof(_prefs->ble_active_scan));
    file.read((uint8_t *) &_prefs->ble_max_results, sizeof(_prefs->ble_max_results));
    file.read((uint8_t *) &_prefs->ble_scantime, sizeof(_prefs->ble_scantime));

    // sanitise bad pref values
    _prefs->rx_delay_base = constrain(_prefs->rx_delay_base, 0, 20.0f);
    _prefs->tx_delay_factor = constrain(_prefs->tx_delay_factor, 0, 2.0f);
    _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 9.0f);
    _prefs->freq = constrain(_prefs->freq, 400.0f, 2500.0f);
    _prefs->bw = constrain(_prefs->bw, 62.5f, 500.0f);
    _prefs->sf = constrain(_prefs->sf, 5, 12);
    _prefs->cr = constrain(_prefs->cr, 5, 8);
    _prefs->tx_power_dbm = constrain(_prefs->tx_power_dbm, 1, 30);
    _prefs->kiss_port = constrain(_prefs->kiss_port, 0, 15);

    file.close();
  }
}

void CommonCLI::savePrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/com_prefs");
  File file = fs->open("/com_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM) || !defined(ARDUINO)
  File file = fs->open("/com_prefs", "w");
#else
  File file = fs->open("/com_prefs", "w", true);
#endif
  if (file) {
    uint8_t pad[8];
    memset(pad, 0, sizeof(pad));

    file.write((uint8_t *) &_prefs->airtime_factor, sizeof(_prefs->airtime_factor));
    file.write((uint8_t *) &_prefs->node_name, sizeof(_prefs->node_name));
    file.write(pad, 4);
    file.write((uint8_t *) &_prefs->node_lat, sizeof(_prefs->node_lat));
    file.write((uint8_t *) &_prefs->node_lon, sizeof(_prefs->node_lon));
    file.write((uint8_t *) &_prefs->freq, sizeof(_prefs->freq));
    file.write((uint8_t *) &_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));
    file.write((uint8_t *) pad, 1);
    file.write((uint8_t *) &_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));
    file.write((uint8_t *) &_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));
    file.write(pad, 4);
    file.write((uint8_t *) &_prefs->sf, sizeof(_prefs->sf));
    file.write((uint8_t *) &_prefs->cr, sizeof(_prefs->cr));
    file.write((uint8_t *) &_prefs->bw, sizeof(_prefs->bw));
    file.write((uint8_t *) &_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));
    file.write(pad, 3);
    file.write((uint8_t *) &_prefs->interference_threshold, sizeof(_prefs->interference_threshold));
    file.write((uint8_t *) &_prefs->sync_word, sizeof(_prefs->sync_word));
    file.write(pad, 1);
    file.write((uint8_t *) &_prefs->kiss_port, sizeof(_prefs->kiss_port));
    file.write((uint8_t *) &_prefs->ble_enabled, sizeof(_prefs->ble_enabled));
    file.write((uint8_t *) &_prefs->ble_filter_dups, sizeof(_prefs->ble_filter_dups));
    file.write((uint8_t *) &_prefs->ble_active_scan, sizeof(_prefs->ble_active_scan));
    file.write((uint8_t *) &_prefs->ble_max_results, sizeof(_prefs->ble_max_results));
    file.write((uint8_t *) &_prefs->ble_scantime, sizeof(_prefs->ble_scantime));

    file.close();
  }
}

#define MIN_LOCAL_ADVERT_INTERVAL   60

void CommonCLI::savePrefs() {
  _callbacks->savePrefs();
}

void CommonCLI::handleSerialData() {
  if (_cli_mode == CLIMode::CLI) {
    parseSerialCLI();
  } else if (_cli_mode == CLIMode::KISS) {
    _kiss.parseSerialKISS();
  }
}

void CommonCLI::parseSerialCLI() {
  int len = strlen(_cmd);

  while (Serial.available() && len < sizeof(_cmd)-1) {
    char c = Serial.read();
    if (c != '\n') {
      _cmd[len++] = c;
      _cmd[len] = 0;
    }
    Serial.print(c);  // echo read characters back to the serial console
  }
  if (len == sizeof(_cmd)-1) {  // command buffer full
    _cmd[sizeof(_cmd)-1] = '\r';
  }

  if (len > 0 && _cmd[len - 1] == '\r') {  // received complete line
    _cmd[len - 1] = 0;  // replace newline with C string null terminator
    
    // construct reply buffer and refer to cmdbuf and replybuf by ptr
    char reply[CMD_BUF_LEN_MAX];
    char* command = _cmd;
    char* resp = reply;

    while (*command == ' ') command++;   // skip leading spaces
    handleCLICommand(0, command, resp);  // NOTE: there is no sender_timestamp via serial!
    if (_cli_mode == CLIMode::CLI && resp[0]) {
      Serial.print("  -> "); Serial.println(resp);
    }

    _cmd[0] = 0;  // reset command buffer
  }
}

void CommonCLI::handleCLICommand(
  uint32_t sender_timestamp,
  const char* command,
  char* resp
){
  if (memcmp(command, "reboot", 6) == 0) {
    _board->reboot();  // doesn't return
  } else if (memcmp(command, "serial mode ", 12) == 0) {
    const char* mode = &command[12];
    if (memcmp(mode, "kiss", 4) == 0) {
      Serial.println("  -> Entering KISS mode!");
      _kiss.reset();  // reset kiss length
      _cli_mode = CLIMode::KISS;
      return;
    }
  } else if (memcmp(command, "txraw ", 6) == 0) {
    const char* tx_hex = &command[6];

    mesh::Packet* pkt = _dispatcher->obtainNewPacket();
    uint8_t tx_buf[MAX_PACKET_PAYLOAD];
    uint8_t len_buf = 0;
    char tmp[3];
    for (int i = 0; i < strlen(tx_hex); i+= 2) {
      if (tx_hex[i] == '\n' || tx_hex[i] == ' ') {
        break;
      }
      tmp[0] = tx_hex[i];
      tmp[1] = tx_hex[i+1];
      tx_buf[len_buf] = strtol(tmp,NULL,16);
      len_buf++;
    }
    pkt->readFrom(tx_buf, len_buf);
    mesh::Utils::printHex(Serial, tx_buf, len_buf);
    _dispatcher->sendPacket(pkt, 1);
    strcpy(resp, "OK");
  } else if (memcmp(command, "clock sync", 10) == 0) {
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (sender_timestamp > curr) {
      getRTCClock()->setCurrentTime(sender_timestamp + 1);
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(resp,
              "OK - clock set: %02d:%02d - %d/%d/%d UTC",
              dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else {
      strcpy(resp, "ERR: clock cannot go backwards");
    }
  } else if (memcmp(command, "start ota", 9) == 0) {
    if (!_board->startOTAUpdate(_prefs->node_name, resp)) {
      strcpy(resp, "Error");
    }
  } else if (memcmp(command, "clock", 5) == 0) {
    uint32_t now = getRTCClock()->getCurrentTime();
    DateTime dt = DateTime(now);
    sprintf(resp,
            "%02d:%02d - %d/%d/%d UTC",
            dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
  } else if (memcmp(command, "time ", 5) == 0) {  // set time (to epoch seconds)
    uint32_t secs = _atoi(&command[5]);
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (secs > curr) {
      getRTCClock()->setCurrentTime(secs);
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(resp,
              "OK - clock set: %02d:%02d - %d/%d/%d UTC",
              dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else {
      strcpy(resp, "(ERR: clock cannot go backwards)");
    }
  } else if (memcmp(command, "tempradio ", 10) == 0) {
    strcpy(_tmp, &command[10]);
    const char *parts[6];
    int num = mesh::Utils::parseTextParts(_tmp, parts, 6);
    float freq  = num > 0 ? atof(parts[0]) : 0.0f;
    float bw    = num > 1 ? atof(parts[1]) : 0.0f;
    uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
    uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
    uint8_t sync_word  = num > 4 ? strtol(parts[4], nullptr, 16) : 0;
    int temp_timeout_mins  = num > 5 ? atoi(parts[5]) : 0;
    if (freq >= 300.0f && freq <= 2500.0f &&
        sf >= 7 && sf <= 12 &&
        cr >= 5 && cr <= 8 &&
        bw >= 7.0f && bw <= 500.0f &&
        temp_timeout_mins > 0)
    {
      _callbacks->applyTempRadioParams(freq, bw, sf, cr,
                                       sync_word, temp_timeout_mins);
      sprintf(resp, "OK - temp params for %d mins", temp_timeout_mins);
    } else {
      strcpy(resp, "Error, invalid params");
    }
  } else if (memcmp(command, "clear stats", 11) == 0) {
    _callbacks->clearStats();
    strcpy(resp, "(OK - stats reset)");
  } else if (memcmp(command, "get ", 4) == 0) {
    const char* config = &command[4];
    if (memcmp(config, "af", 2) == 0) {
      sprintf(resp, "> %s", StrHelper::ftoa(_prefs->airtime_factor));
    } else if (memcmp(config, "int.thresh", 10) == 0) {
      sprintf(resp, "> %d", (uint32_t) _prefs->interference_threshold);
    } else if (memcmp(config, "agc.reset.interval", 18) == 0) {
      sprintf(resp, "> %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
    } else if (memcmp(config, "name", 4) == 0) {
      sprintf(resp, "> %s", _prefs->node_name);
    } else if (memcmp(config, "lat", 3) == 0) {
      sprintf(resp, "> %s", StrHelper::ftoa(_prefs->node_lat));
    } else if (memcmp(config, "lon", 3) == 0) {
      sprintf(resp, "> %s", StrHelper::ftoa(_prefs->node_lon));
    } else if (memcmp(config, "radio", 5) == 0) {
      char freq[16], bw[16];
      strcpy(freq, StrHelper::ftoa(_prefs->freq));
      strcpy(bw, StrHelper::ftoa(_prefs->bw));
      sprintf(resp,
              "> %s,%s,%d,%d,0x%x",
              freq, bw, (uint32_t)_prefs->sf, (uint32_t)_prefs->cr,
              (uint32_t)_prefs->sync_word);
    } else if (memcmp(config, "rxdelay", 7) == 0) {
      sprintf(resp, "> %s", StrHelper::ftoa(_prefs->rx_delay_base));
    } else if (memcmp(config, "txdelay", 7) == 0) {
      sprintf(resp, "> %s", StrHelper::ftoa(_prefs->tx_delay_factor));
    } else if (memcmp(config, "txpower", 2) == 0 &&
               (config[2] == 0 || config[2] == ' '))
    {
      sprintf(resp, "> %d", (uint32_t) _prefs->tx_power_dbm);
    } else if (memcmp(config, "freq", 4) == 0) {
      sprintf(resp, "> %s", StrHelper::ftoa(_prefs->freq));
    } else if (memcmp(config, "syncword", 8) == 0) {
      sprintf(resp, "> 0x%x", (uint32_t)_prefs->sync_word);
    } else if (memcmp(config, "ble", 3) == 0) {
      sprintf(resp, "> %s,%s,%d,%d", 
        _prefs->ble_active_scan == 1 ? "on" : "off",
        _prefs->ble_filter_dups == 1 ? "on" : "off",
        _prefs->ble_max_results, 
        _prefs->ble_scantime);
    } else {
      sprintf(resp, "??: %s", config);
    }
  } else if (memcmp(command, "set ", 4) == 0) {
    const char* config = &command[4];
    if (memcmp(config, "af ", 3) == 0) {
      _prefs->airtime_factor = atof(&config[3]);
      savePrefs();
      strcpy(resp, "OK");
    } else if (memcmp(config, "int.thresh ", 11) == 0) {
      _prefs->interference_threshold = atoi(&config[11]);
      savePrefs();
      strcpy(resp, "OK");
    } else if (memcmp(config, "agc.reset.interval ", 19) == 0) {
      _prefs->agc_reset_interval = atoi(&config[19]) / 4;
      savePrefs();
      strcpy(resp, "OK");
    } else if (memcmp(config, "name ", 5) == 0) {
      StrHelper::strncpy(_prefs->node_name,
                         &config[5],
                         sizeof(_prefs->node_name));
      savePrefs();
      strcpy(resp, "OK");
    } else if (memcmp(config, "ble ", 4) == 0) {
      strcpy(_tmp, &config[6]);
      const char *parts[4];
      int num = mesh::Utils::parseTextParts(_tmp, parts, 4);

      _prefs->ble_active_scan = num > 0 ? (memcmp(parts[0], "on", 2) == 0) : false;
      _prefs->ble_filter_dups = num > 1 ? (memcmp(parts[1], "on", 2) == 0) : true;
      _prefs->ble_max_results  = num > 2 ?  atoi(parts[2]) : 0;
      _prefs->ble_scantime = num > 3 ? atoi(parts[3]) : 0;
      _callbacks->savePrefs();
      _callbacks->applyBLEParams(
        true,
        _prefs->ble_active_scan,
        _prefs->ble_filter_dups,
        _prefs->ble_max_results,
        _prefs->ble_scantime
      );
      strcpy(resp, "OK - reboot to apply");

    } else if (memcmp(config, "radio ", 6) == 0) {
      strcpy(_tmp, &config[6]);
      const char *parts[5];
      int num = mesh::Utils::parseTextParts(_tmp, parts, 5);
      float freq  = num > 0 ? atof(parts[0]) : 0.0f;
      float bw    = num > 1 ? atof(parts[1]) : 0.0f;
      uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
      uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
      uint8_t sync_word  = num > 4 ? strtol(parts[4], nullptr, 16) : 0;
      if (freq >= 300.0f && freq <= 2500.0f &&
          sf >= 5 && sf <= 12 &&
          cr >= 5 && cr <= 8 &&
          bw >= 7.0f && bw <= 500.0f
      ){
        _prefs->sf = sf;
        _prefs->cr = cr;
        _prefs->freq = freq;
        _prefs->bw = bw;
        _prefs->sync_word = sync_word;
        _callbacks->savePrefs();
        _callbacks->applyRadioParams(freq, bw, sf, cr, sync_word);
        strcpy(resp, "OK");
      } else {
        strcpy(resp, "Error, invalid radio params");
      }
    } else if (memcmp(config, "lat ", 4) == 0) {
      _prefs->node_lat = atof(&config[4]);
      savePrefs();
      strcpy(resp, "OK");
    } else if (memcmp(config, "lon ", 4) == 0) {
      _prefs->node_lon = atof(&config[4]);
      savePrefs();
      strcpy(resp, "OK");
    } else if (memcmp(config, "rxdelay ", 8) == 0) {
      float db = atof(&config[8]);
      if (db >= 0) {
        _prefs->rx_delay_base = db;
        savePrefs();
        strcpy(resp, "OK");
      } else {
        strcpy(resp, "Error, cannot be negative");
      }
    } else if (memcmp(config, "txdelay ", 8) == 0) {
      float f = atof(&config[8]);
      if (f >= 0) {
        _prefs->tx_delay_factor = f;
        savePrefs();
        strcpy(resp, "OK");
      } else {
        strcpy(resp, "Error, cannot be negative");
      }
    } else if (memcmp(config, "txpower ", 3) == 0) {
      _prefs->tx_power_dbm = atoi(&config[3]);
      savePrefs();
      _callbacks->setTxPower(_prefs->tx_power_dbm);
      strcpy(resp, "OK");
    } else if (memcmp(config, "kiss ", 5) == 0) {
      const char* kiss_config = &config[5];
      if (memcmp(kiss_config, "port ", 5) == 0) {
        uint8_t kiss_port = atoi(&kiss_config[5]);
        if (kiss_port < 16) {
          _prefs->kiss_port = kiss_port;
          savePrefs();
          strcpy(resp, "OK");
        } else {
          sprintf(resp,
                  "KISS port must be between 0 and 15, invalid value: %d",
                  kiss_port);
        }
      } else {
        sprintf(resp, "unknown kiss config: %s", kiss_config);
      }
    } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
      _prefs->freq = atof(&config[5]);
      savePrefs();
      strcpy(resp, "OK - reboot to apply");
    } else {
      sprintf(resp, "unknown config: %s", config);
    }
  } else if (sender_timestamp == 0 && strcmp(command, "erase") == 0) {
    bool s = _callbacks->formatFileSystem();
    sprintf(resp, "File system erase: %s", s ? "OK" : "Err");
  } else if (memcmp(command, "ver", 3) == 0) {
    sprintf(resp,
            "%s (Build: %s)",
            _callbacks->getFirmwareVer(),
            _callbacks->getBuildDate());
  } else if (memcmp(command, "log start", 9) == 0) {
    _callbacks->setLoggingOn(true);
    strcpy(resp, "   logging on");
  } else if (memcmp(command, "log stop", 8) == 0) {
    _callbacks->setLoggingOn(false);
    strcpy(resp, "   logging off");
  } else if (memcmp(command, "log erase", 9) == 0) {
    _callbacks->eraseLogFile();
    strcpy(resp, "   log erased");
  } else if (memcmp(command, "rxlog on", 8) == 0) {
    _prefs->log_rx = true;
    strcpy(resp, "   rxlog on");
  } else if (memcmp(command, "rxlog off", 9) == 0) {
    _prefs->log_rx = false;
    strcpy(resp, "   rxlog off");
  }  else if (memcmp(command, "rxlog ble on", 12) == 0) {
    _prefs->ble_enabled = true;
    _callbacks->savePrefs();
    _callbacks->applyBLEParams(
      true,
      _prefs->ble_active_scan,
      _prefs->ble_filter_dups,
      _prefs->ble_max_results,
      _prefs->ble_scantime
    );
    strcpy(resp, "OK - reboot to apply");
  } else if (memcmp(command, "rxlog ble off", 13) == 0) {
    _prefs->ble_enabled = false;
    _callbacks->savePrefs();
    _callbacks->applyBLEParams(
      true,
      _prefs->ble_active_scan,
      _prefs->ble_filter_dups,
      _prefs->ble_max_results,
      _prefs->ble_scantime
    );
    strcpy(resp, "OK   rxlog ble off");
  } else if (sender_timestamp == 0 && memcmp(command, "log", 3) == 0) {
    _callbacks->dumpLogFile();
    strcpy(resp, "   EOF");
  } else {
    strcpy(resp, "Unknown command");
  }
}


