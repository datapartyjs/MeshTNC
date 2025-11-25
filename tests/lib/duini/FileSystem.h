#pragma once

#include "duini.h"

#include <string>
#include <filesystem>
#include <fstream>
#include <stdint.h>

namespace fs = std::filesystem;

#define FILE_O_READ "r"
#define FILE_O_WRITE "w"

extern class DUINI_EXPORTS File {
  FILESYSTEM& _fs;
  std::string _filename;
  std::string _mode;
  std::fstream _file_stream;
  size_t _pos;

  public:
  File (FILESYSTEM& fs) : _fs(fs), _pos(0) {};
  File (
    const char* filename,
    const char* mode,
    FILESYSTEM& fs
  ) : _fs(fs), _pos(0) {
    _filename = std::string(filename);
    _mode = std::string(mode);
  };

  bool open (const char* filename, const char* mode); // file must exist within the FILESYSTEM base directory
  int read(void* buf, uint16_t nbyte); // read a number of bytes from the file, updates _pos with the current position in the file
  int read();
  int available(); // returns the number of bytes in the file that have not yet been read
  size_t write(const char* str);
  size_t write(const uint8_t* buffer, size_t size);
  size_t write(const uint8_t* buffer, unsigned long long size);
  void close();
  bool isOpen() { return _file_stream.is_open(); };
  operator bool () const { return _file_stream.is_open(); };
};

extern class DUINI_EXPORTS FILESYSTEM {
  fs::path _base_dir;
  bool _open = false;

  public:
  FILESYSTEM();
  bool begin(std::string base_dir);
  bool exists(const char* filename);
  File open (const char* filename, const char* mode);
  bool remove (char const *filepath);
  virtual bool format();
  fs::path getBaseDir() { return _base_dir; };
  bool checkBaseDir(char const* filename); // returns true if path exists within FILESYSTEM's _base_dir
  operator bool () const { return _open; };
};

extern FILESYSTEM DUINI_EXPORTS PCFileSystem = FILESYSTEM();