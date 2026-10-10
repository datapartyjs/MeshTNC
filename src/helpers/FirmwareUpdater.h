#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <SHA256.h>

// Firmware update over the serial CLI (README, "Firmware updates over serial"):
//
//   ota begin <size> [<signature hex>]   start: image size in bytes, Ed25519 signature
//   ota data <offset> <hex>              the image, in order, up to OTA_CHUNK_MAX bytes a time
//   ota end [noreboot]                   check, apply, reboot (bin/ota/upload_firmware.py does all this)
//   ota abort | ota status | ota rollback
//
// The same commands work over the text CLI and as data frames on KISS port 1, so a host can
// update a TNC without leaving KISS mode. Every chunk is answered, so the host paces itself to
// the flash writes and no serial RX buffer can overflow; a chunk whose reply was lost can be
// sent again (same offset) and is answered OK without being written twice.
//
// ESP32 only. The image goes to the inactive OTA app partition through the Arduino core's
// Update library (the partition updater behind ArduinoOTA), which also checks the image
// header, chip type and image checksum/SHA-256 before the boot partition is switched. Until
// 'ota end' succeeds the new image is not bootable (its first bytes are written last), so a
// failed or rejected upload leaves the running firmware untouched.
//
// Signatures: Ed25519 (RFC 8032) over the SHA-256 digest of the .bin, made with
// bin/ota/sign_firmware.py. The public key is compiled in with OTA_PUBLIC_KEY (64 hex chars,
// see variants/byomesh/platformio.ini): with it set, only an image with a valid signature is
// applied. With it empty any image is applied, and a signature given is not checked.

#ifndef OTA_PUBLIC_KEY
  #define OTA_PUBLIC_KEY ""   // -D OTA_PUBLIC_KEY='"<64 hex chars>"' in the variant's platformio.ini
#endif

// largest 'ota data' chunk, in bytes: "ota data <offset> " + 2 hex chars per byte must fit one
// CLI line / KISS frame (CMD_BUF_LEN_MAX, checked in CommonCLI.cpp)
#ifndef OTA_CHUNK_MAX
  #define OTA_CHUNK_MAX  236
#endif

#define OTA_SIGNATURE_SIZE   64
#define OTA_PUBLIC_KEY_SIZE  32

class FirmwareUpdater {
  bool _active;                 // between a successful 'ota begin' and 'ota end' / abort
  bool _have_sig;
  uint32_t _size, _received;
  uint8_t _sig[OTA_SIGNATURE_SIZE];
  unsigned long _last_data_ms;  // for the idle timeout
  unsigned long _reboot_at;     // 0, or when loop() should report a reboot is due
  const char* _last_result;     // shown by 'ota status', NULL = none yet
  SHA256 _sha;                  // digest of the bytes written so far (what the signature covers)

  void fail(const char* why, char* resp);   // abort the update and write an error reply

public:
  FirmwareUpdater();

  static bool supported();      // this board can be updated this way
  static bool signatureRequired() { return OTA_PUBLIC_KEY[0] != 0; }
  bool inProgress() const { return _active; }

  // decodes hex (stops at the end or a space) into out; returns the byte count, or -1 if it
  // isn't valid hex, has an odd number of digits, or is longer than max bytes
  static int parseHex(const char* hex, uint8_t* out, size_t max);

  // The CLI commands. Each writes its reply to resp (CMD_BUF_LEN_MAX bytes), true = OK.
  bool begin(uint32_t size, const char* sig_hex, char* resp);
  bool write(uint32_t offset, const uint8_t* data, size_t len, char* resp);
  bool end(bool reboot, char* resp);   // signature check, image check, set boot partition
  void abort(char* resp);
  void status(char* resp);
  bool rollback(char* resp);   // boot the firmware in the other OTA slot, if it holds a valid one

  // call from the main loop: aborts an update the host abandoned; true when a reboot is due
  // ('ota end' / 'ota rollback' delay it so their reply gets out first)
  bool loop();
};
