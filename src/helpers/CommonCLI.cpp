#include <Arduino.h>
#include "CommonCLI.h"
#include "TxtDataHelpers.h"
#include <RTClib.h>

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
#if defined(RP2040_PLATFORM)
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

    // sanitise bad pref values
    _prefs->rx_delay_base = constrain(_prefs->rx_delay_base, 0, 20.0f);
    _prefs->tx_delay_factor = constrain(_prefs->tx_delay_factor, 0, 2.0f);
    _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 9.0f);
    _prefs->freq = constrain(_prefs->freq, 400.0f, 2500.0f);
    _prefs->bw = constrain(_prefs->bw, 62.5f, 500.0f);
    _prefs->sf = constrain(_prefs->sf, 7, 12);
    _prefs->cr = constrain(_prefs->cr, 5, 8);
    _prefs->tx_power_dbm = constrain(_prefs->tx_power_dbm, 1, 30);

    file.close();
  }
}

void CommonCLI::savePrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/com_prefs");
  File file = fs->open("/com_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
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

    file.close();
  }
}

#define MIN_LOCAL_ADVERT_INTERVAL   60

void CommonCLI::savePrefs() {
  _callbacks->savePrefs();
}

void CommonCLI::handleCommand(uint32_t sender_timestamp, const char* command, char* reply) {
    if (memcmp(command, "reboot", 6) == 0) {
      _board->reboot();  // doesn't return
    } else if (memcmp(command, "txraw ", 6) == 0) {
      const char* tx_hex = &command[6];

      mesh::Packet* pkt = _mesh->obtainNewPacket();
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
      _mesh->sendPacket(pkt, 1);
      strcpy(reply, "OK");
    } else if (memcmp(command, "clock sync", 10) == 0) {
      uint32_t curr = getRTCClock()->getCurrentTime();
      if (sender_timestamp > curr) {
        getRTCClock()->setCurrentTime(sender_timestamp + 1);
        uint32_t now = getRTCClock()->getCurrentTime();
        DateTime dt = DateTime(now);
        sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
      } else {
        strcpy(reply, "ERR: clock cannot go backwards");
      }
    } else if (memcmp(command, "start ota", 9) == 0) {
      if (!_board->startOTAUpdate(_prefs->node_name, reply)) {
        strcpy(reply, "Error");
      }
    } else if (memcmp(command, "clock", 5) == 0) {
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(reply, "%02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else if (memcmp(command, "time ", 5) == 0) {  // set time (to epoch seconds)
      uint32_t secs = _atoi(&command[5]);
      uint32_t curr = getRTCClock()->getCurrentTime();
      if (secs > curr) {
        getRTCClock()->setCurrentTime(secs);
        uint32_t now = getRTCClock()->getCurrentTime();
        DateTime dt = DateTime(now);
        sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
      } else {
        strcpy(reply, "(ERR: clock cannot go backwards)");
      }
    } else if (memcmp(command, "tempradio ", 10) == 0) {
      strcpy(tmp, &command[10]);
      const char *parts[5];
      int num = mesh::Utils::parseTextParts(tmp, parts, 6);
      float freq  = num > 0 ? atof(parts[0]) : 0.0f;
      float bw    = num > 1 ? atof(parts[1]) : 0.0f;
      uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
      uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
      uint8_t sync_word  = num > 4 ? atoi(parts[4]) : 0;
      int temp_timeout_mins  = num > 5 ? atoi(parts[5]) : 0;
      if (freq >= 300.0f && freq <= 2500.0f && sf >= 7 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, sync_word, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error, invalid params");
      }
    } else if (memcmp(command, "clear stats", 11) == 0) {
      _callbacks->clearStats();
      strcpy(reply, "(OK - stats reset)");
    } else if (memcmp(command, "get ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af", 2) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->airtime_factor));
      } else if (memcmp(config, "int.thresh", 10) == 0) {
        sprintf(reply, "> %d", (uint32_t) _prefs->interference_threshold);
      } else if (memcmp(config, "agc.reset.interval", 18) == 0) {
        sprintf(reply, "> %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
      } else if (memcmp(config, "name", 4) == 0) {
        sprintf(reply, "> %s", _prefs->node_name);
      } else if (memcmp(config, "lat", 3) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lat));
      } else if (memcmp(config, "lon", 3) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lon));
      } else if (memcmp(config, "radio", 5) == 0) {
        char freq[16], bw[16];
        strcpy(freq, StrHelper::ftoa(_prefs->freq));
        strcpy(bw, StrHelper::ftoa(_prefs->bw));
        sprintf(reply, "> %s,%s,%d,%d,%d", freq, bw, (uint32_t)_prefs->sf, (uint32_t)_prefs->cr, (uint32_t)_prefs->sync_word);
      } else if (memcmp(config, "rxdelay", 7) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->rx_delay_base));
      } else if (memcmp(config, "txdelay", 7) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->tx_delay_factor));
      } else if (memcmp(config, "tx", 2) == 0 && (config[2] == 0 || config[2] == ' ')) {
        sprintf(reply, "> %d", (uint32_t) _prefs->tx_power_dbm);
      } else if (memcmp(config, "freq", 4) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->freq));
      } else if (memcmp(config, "syncword", 8) == 0) {
        sprintf(reply, "> %d", (uint32_t)_prefs->sync_word);
      } else {
        sprintf(reply, "??: %s", config);
      }
    } else if (memcmp(command, "set ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af ", 3) == 0) {
        _prefs->airtime_factor = atof(&config[3]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "int.thresh ", 11) == 0) {
        _prefs->interference_threshold = atoi(&config[11]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "agc.reset.interval ", 19) == 0) {
        _prefs->agc_reset_interval = atoi(&config[19]) / 4;
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "name ", 5) == 0) {
        StrHelper::strncpy(_prefs->node_name, &config[5], sizeof(_prefs->node_name));
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "radio ", 6) == 0) {
        strcpy(tmp, &config[6]);
        const char *parts[5];
        int num = mesh::Utils::parseTextParts(tmp, parts, 5);
        float freq  = num > 0 ? atof(parts[0]) : 0.0f;
        float bw    = num > 1 ? atof(parts[1]) : 0.0f;
        uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
        uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
        uint8_t sync_word  = num > 4 ? atoi(parts[4]) : 0;
        if (freq >= 300.0f && freq <= 2500.0f && sf >= 7 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
          _prefs->sf = sf;
          _prefs->cr = cr;
          _prefs->freq = freq;
          _prefs->bw = bw;
          _prefs->sync_word = sync_word;
          _callbacks->savePrefs();
          strcpy(reply, "OK - reboot to apply");
        } else {
          strcpy(reply, "Error, invalid radio params");
        }
      } else if (memcmp(config, "lat ", 4) == 0) {
        _prefs->node_lat = atof(&config[4]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "lon ", 4) == 0) {
        _prefs->node_lon = atof(&config[4]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "rxdelay ", 8) == 0) {
        float db = atof(&config[8]);
        if (db >= 0) {
          _prefs->rx_delay_base = db;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, "txdelay ", 8) == 0) {
        float f = atof(&config[8]);
        if (f >= 0) {
          _prefs->tx_delay_factor = f;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, "tx ", 3) == 0) {
        _prefs->tx_power_dbm = atoi(&config[3]);
        savePrefs();
        _callbacks->setTxPower(_prefs->tx_power_dbm);
        strcpy(reply, "OK");
      } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
        _prefs->freq = atof(&config[5]);
        savePrefs();
        strcpy(reply, "OK - reboot to apply");
      } else {
        sprintf(reply, "unknown config: %s", config);
      }
    } else if (sender_timestamp == 0 && strcmp(command, "erase") == 0) {
      bool s = _callbacks->formatFileSystem();
      sprintf(reply, "File system erase: %s", s ? "OK" : "Err");
    } else if (memcmp(command, "ver", 3) == 0) {
      sprintf(reply, "%s (Build: %s)", _callbacks->getFirmwareVer(), _callbacks->getBuildDate());
    } else if (memcmp(command, "log start", 9) == 0) {
      _callbacks->setLoggingOn(true);
      strcpy(reply, "   logging on");
    } else if (memcmp(command, "log stop", 8) == 0) {
      _callbacks->setLoggingOn(false);
      strcpy(reply, "   logging off");
    } else if (memcmp(command, "log erase", 9) == 0) {
      _callbacks->eraseLogFile();
      strcpy(reply, "   log erased");
    } else if (memcmp(command, "rxlog on", 8) == 0) {
      _prefs->log_rx = true;
      strcpy(reply, "   rxlog on");
    } else if (memcmp(command, "rxlog off", 9) == 0) {
      _prefs->log_rx = false;
      strcpy(reply, "   rxlog off");
    } else if (sender_timestamp == 0 && memcmp(command, "log", 3) == 0) {
      _callbacks->dumpLogFile();
      strcpy(reply, "   EOF");
    } else {
      strcpy(reply, "Unknown command");
    }
}
