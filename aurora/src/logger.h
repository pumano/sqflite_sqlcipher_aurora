/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC
 * <community@omp.ru> SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <atomic>
#include <iostream>
#include <string>
#include <unistd.h>

class Logger final {
public:
  enum Level {
    None = 0,
    Sql,
    Verbose,
  };

public:
  Logger(Level level, const std::string &tag);
  // std::atomic is not implicitly copyable; a copy takes a relaxed
  // snapshot (a Database copies the plugin logger at construction, on the
  // channel thread, before the worker ever uses it). devnull_ is
  // default-constructed: it is a stateless sink.
  Logger(const Logger &other)
      : level_(other.level_.load(std::memory_order_relaxed)),
        tag_(other.tag_) {}
  Logger &operator=(const Logger &other) {
    if (this != &other) {
      level_.store(other.level_.load(std::memory_order_relaxed),
                   std::memory_order_relaxed);
      tag_ = other.tag_;
    }
    return *this;
  }

  Level logLevel() const;
  void setLogLevel(Level level);

  std::string tag() const;
  // tag_ is a plain string: set the tag before the logger is shared
  // between threads, not while another thread may be logging.
  void setTag(const std::string &tag);

  std::ostream &verb();
  std::ostream &sql();
  // Errors are always reported, regardless of the verbosity level.
  std::ostream &error();

private:
  // A copyable sink: std::ostream is neither copyable (libstdc++ deletes
  // the copy constructor) nor default-constructible, which would make a
  // Logger with an atomic member uncopyable. Writes are discarded; each
  // instance owns its buffer, so no state is shared between copies.
  class NullStream final : public std::ostream {
   public:
    NullStream() : std::ostream(&buf_) {}
    NullStream(const NullStream &) : std::ostream(&buf_) {}
    NullStream &operator=(const NullStream &) { return *this; }

   private:
    class Buf final : public std::streambuf {
     protected:
      // Discard: return the character as stored; eof means "nothing to do".
      int_type overflow(int_type c) override { return c; }
    };
    Buf buf_;
  };

  // Relaxed: read on every log line from the worker thread and from the
  // channel thread (getDatabases / debug), written from the channel thread
  // (the options call). Only the value matters -- no ordering with other
  // state is needed, and a stale level for one line is harmless.
  std::atomic<int> level_{static_cast<int>(Level::None)};
  std::string tag_;
  NullStream devnull_;
};
