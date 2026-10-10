#include "BuildInfo.h"

#if defined(__has_include)
  #if __has_include("build_info_gen.h")
    #include "build_info_gen.h"
  #endif
#endif

#ifndef BUILD_GIT_HASH
  #define BUILD_GIT_HASH  "unknown"
#endif
#ifndef BUILD_DATE_UTC
  #define BUILD_DATE_UTC  "unknown"
#endif
#ifndef BUILD_VARIANT
  #define BUILD_VARIANT   "unknown"
#endif
#ifndef BUILD_ENV
  #define BUILD_ENV       "unknown"
#endif
#ifndef BUILD_BOARD
  #define BUILD_BOARD     "unknown"
#endif

namespace BuildInfo {
  const char* gitHash() { return BUILD_GIT_HASH; }
  const char* date()    { return BUILD_DATE_UTC; }
  const char* variant() { return BUILD_VARIANT; }
  const char* env()     { return BUILD_ENV; }
  const char* board()   { return BUILD_BOARD; }
}
