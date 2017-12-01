#ifndef UTIL_HPP
#define UTIL_HPP

#include <sys/time.h>

// comparing nanosecond timestamp
bool operator< (const timespec &A, const timespec &B) {

  if (A.tv_sec < B.tv_sec) return true;
  if (A.tv_sec > B.tv_sec) return false;

  return A.tv_nsec < B.tv_nsec;
}

#endif
