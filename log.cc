#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>
#include <string>

#include "log.h"

namespace trek {
namespace {
static std::string timestamp() {
  char buf[32] = "";
  timeval tv;
  if (gettimeofday(&tv, nullptr) == 0) {
    tm *tmp = localtime(&tv.tv_sec);
    if (tmp != nullptr) {
      if (strftime(buf, sizeof(buf), "%T", tmp) != 0) {
        return std::string(buf) + "." + std::to_string(tv.tv_usec);
      }
    }
  }
  return std::string("");
}
}

void log(const char *tag, const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "[%s %s] ", timestamp().c_str(), tag);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}
}
