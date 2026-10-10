#include "FirmwareUpdater.h"
#include <string.h>
#include <stdio.h>

#ifdef ESP32
  #include <Update.h>
  #include <esp_ota_ops.h>
  #include <Ed25519.h>
#endif

#ifndef OTA_IDLE_TIMEOUT_MS
  #define OTA_IDLE_TIMEOUT_MS  60000   // no 'ota data' for this long: the update is abandoned
#endif
#define OTA_REBOOT_DELAY_MS    300     // after 'ota end': time for the reply to leave the UART

static_assert(sizeof(OTA_PUBLIC_KEY) == 1 || sizeof(OTA_PUBLIC_KEY) == 2 * OTA_PUBLIC_KEY_SIZE + 1,
              "OTA_PUBLIC_KEY must be empty, or an Ed25519 public key as 64 hex characters");

FirmwareUpdater::FirmwareUpdater() {
  _active = _have_sig = false;
  _size = _received = 0;
  _last_data_ms = _reboot_at = 0;
  _last_result = NULL;
}

bool FirmwareUpdater::supported() {
#ifdef ESP32
  return true;
#else
  return false;
#endif
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int FirmwareUpdater::parseHex(const char* hex, uint8_t* out, size_t max) {
  size_t n = 0;
  while (*hex && *hex != ' ' && *hex != '\r' && *hex != '\n') {
    int hi = hexVal(hex[0]);
    int lo = (hi < 0) ? -1 : hexVal(hex[1]);   // hex[1] is at worst the terminator
    if (lo < 0 || n >= max) return -1;
    out[n++] = (uint8_t)((hi << 4) | lo);
    hex += 2;
  }
  return (int) n;
}

#ifdef ESP32

// OTA_PUBLIC_KEY as bytes; false if the compiled-in string isn't hex
static bool publicKey(uint8_t key[OTA_PUBLIC_KEY_SIZE]) {
  return FirmwareUpdater::parseHex(OTA_PUBLIC_KEY, key, OTA_PUBLIC_KEY_SIZE) == OTA_PUBLIC_KEY_SIZE;
}

void FirmwareUpdater::fail(const char* why, char* resp) {
  if (_active) Update.abort();
  _active = false;
  _last_result = why;
  sprintf(resp, "Error, %s", why);
}

bool FirmwareUpdater::begin(uint32_t size, const char* sig_hex, char* resp) {
  if (_active) {
    strcpy(resp, "Error, an update is already in progress ('ota abort' to drop it)");
    return false;
  }
  if (size == 0) {
    strcpy(resp, "Error, use: ota begin <size> [<signature hex>]");
    return false;
  }
  _have_sig = false;
  if (sig_hex && *sig_hex) {
    if (parseHex(sig_hex, _sig, sizeof(_sig)) != sizeof(_sig)) {
      strcpy(resp, "Error, the signature must be 64 bytes (128 hex characters)");
      return false;
    }
    _have_sig = true;
  }
  if (signatureRequired()) {
    uint8_t key[OTA_PUBLIC_KEY_SIZE];
    if (!publicKey(key)) {
      strcpy(resp, "Error, OTA_PUBLIC_KEY in this firmware is not valid hex, updates are impossible");
      return false;
    }
    if (!_have_sig) {
      strcpy(resp, "Error, this firmware only applies signed images: ota begin <size> <signature hex>");
      return false;
    }
  }

  const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
  if (part == NULL) {
    strcpy(resp, "Error, no free OTA app partition (check the partition table)");
    return false;
  }
  if (size > part->size) {
    sprintf(resp, "Error, the image is %lu bytes but OTA partition %s holds at most %lu",
            (unsigned long) size, part->label, (unsigned long) part->size);
    return false;
  }
  if (!Update.begin(size, U_FLASH)) {
    sprintf(resp, "Error, %s", Update.errorString());
    return false;
  }

  _sha.reset();
  _size = size;
  _received = 0;
  _active = true;
  _last_data_ms = millis();
  _reboot_at = 0;
  _last_result = NULL;
  sprintf(resp, "OK - send %lu bytes as 'ota data <offset> <hex>' (up to %d bytes each) then 'ota end'; %s image, partition %s",
          (unsigned long) size, OTA_CHUNK_MAX, _have_sig ? "signed" : "unsigned", part->label);
  return true;
}

bool FirmwareUpdater::write(uint32_t offset, const uint8_t* data, size_t len, char* resp) {
  if (!_active) {
    strcpy(resp, "Error, no update in progress ('ota begin' first)");
    return false;
  }
  _last_data_ms = millis();
  if (offset != _received) {
    // the host sending the previous chunk again (its reply was lost): already written
    if (len > 0 && offset < _received && offset + len == _received) {
      sprintf(resp, "OK %lu", (unsigned long) _received);
      return true;
    }
    sprintf(resp, "Error, expected offset %lu, got %lu", (unsigned long) _received, (unsigned long) offset);
    return false;
  }
  if (len == 0 || _received + len > _size) {
    sprintf(resp, "Error, %u bytes at offset %lu go past the image size of %lu",
            (unsigned) len, (unsigned long) offset, (unsigned long) _size);
    return false;
  }
  if (Update.write((uint8_t*) data, len) != len) {
    fail(Update.errorString(), resp);   // e.g. flash write failed, not an app image
    return false;
  }
  _sha.update(data, len);
  _received += len;
  sprintf(resp, "OK %lu", (unsigned long) _received);
  return true;
}

bool FirmwareUpdater::end(bool reboot, char* resp) {
  if (!_active) {
    strcpy(resp, "Error, no update in progress ('ota begin' first)");
    return false;
  }
  if (_received != _size) {
    sprintf(resp, "Error, %lu of %lu bytes received: send the rest, or 'ota abort'",
            (unsigned long) _received, (unsigned long) _size);
    return false;
  }

  uint8_t digest[32];
  _sha.finalize(digest, sizeof(digest));

  const char* sig_state;
  if (signatureRequired()) {
    uint8_t key[OTA_PUBLIC_KEY_SIZE];
    if (!publicKey(key) || !Ed25519::verify(_sig, key, digest, sizeof(digest))) {
      fail("signature invalid, image discarded", resp);
      return false;
    }
    sig_state = "signature OK";
  } else {
    sig_state = _have_sig ? "signature not checked (no OTA_PUBLIC_KEY in this firmware)" : "unsigned image";
  }

  // writes the magic byte, checks the image (chip, segments, checksum/SHA-256) and sets it to boot
  if (!Update.end()) {
    fail(Update.errorString(), resp);
    return false;
  }
  _active = false;
  _last_result = "applied";
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  if (reboot) _reboot_at = millis() + OTA_REBOOT_DELAY_MS;
  sprintf(resp, "OK - %s, %lu bytes in %s, %s", sig_state, (unsigned long) _size,
          boot ? boot->label : "?", reboot ? "rebooting" : "reboot to run it");
  return true;
}

void FirmwareUpdater::abort(char* resp) {
  if (!_active) {
    strcpy(resp, "OK - no update in progress");
    return;
  }
  Update.abort();
  _active = false;
  _last_result = "aborted";
  strcpy(resp, "OK - update aborted");
}

void FirmwareUpdater::status(char* resp) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  if (_active) {
    sprintf(resp, "> in progress, %lu/%lu bytes", (unsigned long) _received, (unsigned long) _size);
  } else {
    sprintf(resp, "> idle%s%s", _last_result ? ", last update " : "", _last_result ? _last_result : "");
  }
  char buf[120];
  sprintf(buf, ", signature %s, running %s, update slot %s (max image %lu bytes)",
          signatureRequired() ? "required" : "not checked",
          running ? running->label : "?", next ? next->label : "none",
          (unsigned long) (next ? next->size : 0));
  strcat(resp, buf);
}

bool FirmwareUpdater::rollback(char* resp) {
  if (_active) {
    strcpy(resp, "Error, an update is in progress ('ota abort' first)");
    return false;
  }
  if (!Update.canRollBack()) {
    strcpy(resp, "Error, no firmware in the other OTA slot");
    return false;
  }
  if (!Update.rollBack()) {   // checks that image, then sets it to boot
    strcpy(resp, "Error, the firmware in the other OTA slot is not valid");
    return false;
  }
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  _reboot_at = millis() + OTA_REBOOT_DELAY_MS;
  sprintf(resp, "OK - booting the firmware in %s, rebooting", boot ? boot->label : "?");
  return true;
}

bool FirmwareUpdater::loop() {
  if (_active && (long)(millis() - _last_data_ms) > (long) OTA_IDLE_TIMEOUT_MS) {
    Update.abort();
    _active = false;
    _last_result = "aborted, the host stopped sending";
  }
  return _reboot_at != 0 && (long)(millis() - _reboot_at) >= 0;
}

#else   // not ESP32: the commands exist but refuse

#define NOT_SUPPORTED  "Error, not supported on this board"

void FirmwareUpdater::fail(const char* why, char* resp) { sprintf(resp, "Error, %s", why); }
bool FirmwareUpdater::begin(uint32_t size, const char* sig_hex, char* resp) { strcpy(resp, NOT_SUPPORTED); return false; }
bool FirmwareUpdater::write(uint32_t offset, const uint8_t* data, size_t len, char* resp) { strcpy(resp, NOT_SUPPORTED); return false; }
bool FirmwareUpdater::end(bool reboot, char* resp) { strcpy(resp, NOT_SUPPORTED); return false; }
void FirmwareUpdater::abort(char* resp) { strcpy(resp, NOT_SUPPORTED); }
void FirmwareUpdater::status(char* resp) { strcpy(resp, NOT_SUPPORTED); }
bool FirmwareUpdater::rollback(char* resp) { strcpy(resp, NOT_SUPPORTED); return false; }
bool FirmwareUpdater::loop() { return false; }

#endif
