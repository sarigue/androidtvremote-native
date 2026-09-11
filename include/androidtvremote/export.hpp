#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(ANDROIDTVREMOTE_SHARED)
#    if defined(ANDROIDTVREMOTE_BUILDING_LIBRARY)
#      define ANDROIDTVREMOTE_API __declspec(dllexport)
#    else
#      define ANDROIDTVREMOTE_API __declspec(dllimport)
#    endif
#  else
#    define ANDROIDTVREMOTE_API
#  endif
#else
#  if defined(ANDROIDTVREMOTE_SHARED) && defined(__GNUC__)
#    define ANDROIDTVREMOTE_API __attribute__((visibility("default")))
#  else
#    define ANDROIDTVREMOTE_API
#  endif
#endif
