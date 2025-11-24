#pragma once

#ifdef ARDUINO
#include <Stream.h>
#else
#include "MeshTNC.h"
#endif

#include "MeshCore.h"
#include <string.h>

namespace mesh {

extern class MESHTNC_EXPORTS RNG {
public:
  virtual void random(uint8_t* dest, size_t sz) = 0;

  /**
   * \returns  random number between _min (inclusive) and _max (exclusive)
   */
  uint32_t nextInt(uint32_t _min, uint32_t _max);
};

extern class MESHTNC_EXPORTS Utils {
public:
  /**
   * \brief  converts 'src' bytes with given length to Hex representation, and null terminates.
  */
  static void toHex(char* dest, const uint8_t* src, size_t len);

  /**
   * \brief  converts 'src_hex' hexadecimal string (should be null term) back to raw bytes, storing in 'dest'.
   * \param dest_size   must be exactly the expected size in bytes.
   * \returns  true if successful
  */
  static bool fromHex(uint8_t* dest, int dest_size, const char *src_hex);

  /**
   * \brief  Prints the hexadecimal representation of 'src' bytes of given length, to Stream 's'.
  */
#ifdef ARDUINO
  static void printHex(Stream& s, const uint8_t* src, size_t len);
#endif

  /**
   * \brief  parse 'text' into parts separated by 'separator' char.
   * \param  text  the text to parse (note is MODIFIED!)
   * \param  parts  destination array to store pointers to starts of parse parts
   * \param  max_num  max elements to store in 'parts' array
   * \param  separator  the separator character
   * \returns  the number of parts parsed (in 'parts')
   */
  static int parseTextParts(char* text, const char* parts[], int max_num, char separator=',');

  static bool isHexChar(char c);
};

}
