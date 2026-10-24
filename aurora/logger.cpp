/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC
 * <community@omp.ru> SPDX-License-Identifier: BSD-3-Clause
 */
#include "src/logger.h"

namespace {

inline std::string ifatty(const std::string& message) {
  return isatty(STDOUT_FILENO) ? message : "";
}

inline std::string green(const std::string& message) {
  return ifatty("\033[1;32m") + message + ifatty("\033[0m");
}

inline std::string yellow(const std::string& message) {
  return ifatty("\033[1;33m") + message + ifatty("\033[0m");
}

inline std::string red(const std::string& message) {
  return ifatty("\033[1;31m") + message + ifatty("\033[0m");
}

inline std::string thin(const std::string& message) {
  return ifatty("\033[90m") + message + ifatty("\033[0m");
}

class cexit {
 public:
  cexit(std::ostream& stream, int exitcode)
      : stream_(stream), exitcode_(exitcode) {}

  ~cexit() { std::exit(exitcode_); }

  template <class T>
  friend const cexit& operator<<(const cexit& self, const T& message) {
    self.stream_ << message;
    return self;
  }

  typedef std::ostream& (*cmanip)(std::ostream&);
  friend const cexit& operator<<(const cexit& self, cmanip manip) {
    manip(self.stream_);
    return self;
  }

 private:
  std::ostream& stream_;
  int exitcode_;
};

/* clang-format off */
#ifdef NDEBUG
#define debuginfo (" ")
#else
#define stringify_impl(x) #x
#define stringify(x) stringify_impl(x)
#define debuginfo (thin(" " __FILE__ ":" stringify(__LINE__) " "))
#endif
/* clang-format on */

#define loginfo (std::cout << green("[info] "))
#define logwarn (std::cout << yellow("[warn]") << debuginfo)
#define logerr (std::cout << red("[error]") << debuginfo)
#define logcrit                        \
  (cexit(std::cout, 1) \
   << red("[crit]") << debuginfo)

}  // namespace

Logger::Logger(Level level, const std::string &tag)
    : level_(level), tag_(tag), devnull_(0) {}

Logger::Level Logger::logLevel() const { return level_; }

void Logger::setLogLevel(Level level) { level_ = level; }

std::string Logger::tag() const { return tag_; }

void Logger::setTag(const std::string &tag) { tag_ = tag; }

std::ostream &Logger::verb() {
  if (level_ >= Level::Verbose)
    return loginfo << (tag_.empty() ? "" : "[" + tag_ + "] ");

  return devnull_;
}

std::ostream &Logger::sql() {
  if (level_ >= Level::Sql)
    return loginfo << (tag_.empty() ? "" : "[" + tag_ + "] ");

  return devnull_;
}
