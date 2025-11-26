#include "FileSystem.h"
#include <stdexcept>
#include <cstring>

// This is heavily refactored AI cybertrash output from
// Qwen3 Coder 30B A3B Instruct 1M Q8_0 run locally in LM Studio

FILESYSTEM PCFileSystem = FILESYSTEM();

bool FILESYSTEM::begin(std::string base_dir) {
  _base_dir = base_dir;
  try {
    // Create the directory if it doesn't exist
    if (!fs::exists(_base_dir)) {
      if (fs::exists(_base_dir.parent_path())) {
        fs::create_directories(_base_dir);
      } else {
        throw std::runtime_error("FATAL: Specified parent directory FS root directory does not exist!");
      }
    }
    _open = true;
  } catch (const std::exception& e) {
    _open = false;
  }
  return _open;
}

bool FILESYSTEM::checkBaseDir(char const* filename) {
  if (_base_dir.empty()) return false;
  try {
    // Convert to filesystem path and resolve it relative to base directory
    fs::path file_path(filename);
    
    // If the path is absolute, we need to check if it's within our base_dir
    if (file_path.is_absolute()) {
      return fs::canonical(file_path).string().find(_base_dir.string()) == 0;
    } else {
      // For relative paths, construct full path and check containment
      fs::path full_path = _base_dir / file_path;
      return fs::canonical(full_path).string().find(_base_dir.string()) == 0;
    }
  } catch (const std::filesystem::filesystem_error&) {
      // If canonicalization fails, the path is likely invalid or outside base dir
      return false;
  }
}

bool FILESYSTEM::exists(const char* filename) {
  if (!checkBaseDir(filename)) return false;
  
  try {
    fs::path full_path = _base_dir / filename;
    return fs::exists(full_path);
  } catch (const std::exception&) {
    return false;
  }
}

bool FILESYSTEM::remove(char const *filepath) {
  if (!checkBaseDir(filepath)) return false;
  
  try {
    fs::path full_path = _base_dir / filepath;
    
    if (fs::exists(full_path)) {
      fs::remove(full_path);
      return true;
    }
    return false;
  } catch (const std::exception&) {
    return false;
  }
}

bool FILESYSTEM::format() {
  if (_base_dir.empty()) return false;
  try {
    if (fs::exists(_base_dir)) {
      fs::remove_all(_base_dir);
      fs::create_directories(_base_dir);
    } else {
      fs::create_directories(_base_dir);
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

File FILESYSTEM::open(const char* filename, const char* mode) {
  return File(filename, mode, *this);
}

bool File::open(const char* filename, const char* mode) {
  if (!_fs.checkBaseDir(filename)) return false;
  if (isOpen()) return true;
  
  _filename = std::string(filename);
  _mode = std::string(mode);
  
  // Create the full path
  fs::path full_path = _fs.getBaseDir() / filename;
  
  // Ensure parent directory exists for writing operations
  if (std::string(mode).find(FILE_O_WRITE) != std::string::npos) {
    try {
      // TODO: probably don't do this
      fs::create_directories(full_path.parent_path());
    } catch (const std::exception&) {
      return false;
    }
  }
  
  // Open the actual file stream
  std::ios_base::openmode open_mode = std::ios_base::binary;
  if (std::string(mode).find(FILE_O_READ)) {
    open_mode |= std::ios::in;
  } 
  if (std::string(mode).find(FILE_O_WRITE)) {
    open_mode |= std::ios::out;
  }
  _file_stream.open(full_path.string(), open_mode);
  
  return _file_stream.is_open();
}

int File::read(void* buf, uint16_t nbyte) {
  if (!isOpen()) return -1; // Error: file not open
  
  _file_stream.read(static_cast<char*>(buf), nbyte);
  int bytes_read = static_cast<int>(_file_stream.gcount());
  
  // Update position in the virtual filesystem
  _pos += bytes_read;
  
  return bytes_read;
}

int File::read() {
  int ret = -1;
  uint8_t ch;
  if (read(&ch, 1) > 0)
  {
    ret = static_cast<int>(ch);
  }
  return ret;
}

int File::available() {
  if (!isOpen()) return 0;  // Error: file not open
  
  // Save current position
  auto current_pos = _file_stream.tellg();
  
  // Seek to end of file to get total size
  _file_stream.seekg(0, std::ios::end);
  auto total_size = _file_stream.tellg();
  
  // Restore original position
  _file_stream.seekg(current_pos);
  
  // Return number of bytes from current position to the end
  return static_cast<int>(total_size - current_pos);
}

size_t File::write(const uint8_t* buffer, size_t size) {
  if (!isOpen()) return 0;  // Error: file not open
  
  _file_stream.write(reinterpret_cast<const char*>(buffer), size);
  _pos += size;
  
  return size;
}

size_t File::write(const char* str) {
  if (!isOpen()) return 0;  // Error: file not open
  return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
}

void File::close() {
  if (isOpen()) {
    _file_stream.close();
  }
}