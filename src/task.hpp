#ifndef TASK_HPP
#define TASK_HPP

#include <string>
#include <cstdint>
#include <sys/time.h>

class task {

public:

  task() = delete;

  //task(unsigned char s, int32_t f, uint32_t vc, std::string c)
  //  : status(s), fd(f), vote_count(vc), cmd(c) { }

  // an efficient constructor - avoid copying std::string
  task(unsigned char s, int32_t f, uint32_t vc, std::string &&c)
    : status(s), fd(f), vote_count(vc), cmd(c) {
    clock_gettime(CLOCK_REALTIME, &ts);
  }

    // move constructor
  task(task&& old) noexcept
    : status(old.status), fd(old.fd), vote_count(old.vote_count),
      cmd(std::move(old.cmd)) {
    clock_gettime(CLOCK_REALTIME, &ts);
  }

  void update_time() noexcept { clock_gettime(CLOCK_REALTIME, &ts); }

  unsigned char status;
  int32_t fd;
  uint32_t vote_count;
  std::string cmd;

  // storing nanosecond precision timestamp
  // reserved later for crash recovery and synchronization
  struct timespec ts; 
};

#endif
