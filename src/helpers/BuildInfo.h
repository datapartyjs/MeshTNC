#pragma once

// Build information, generated at build time by build_info.py ("unknown" if it didn't run)
namespace BuildInfo {
  const char* gitHash();    // short git hash, "-dirty" if there were uncommitted changes
  const char* date();       // UTC build time, ISO 8601: 2026-10-04T12:34:56Z
  const char* variant();    // variants/<name> directory, e.g. "byomesh"
  const char* env();        // PlatformIO environment, e.g. "BYOMesh_Repeater"
  const char* board();      // PlatformIO board, e.g. "esp32-s3-devkitc-1"
}
