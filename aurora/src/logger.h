/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC
 * <community@omp.ru> SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

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

  Level logLevel() const;
  void setLogLevel(Level level);

  std::string tag() const;
  void setTag(const std::string &tag);

  std::ostream &verb();
  std::ostream &sql();

private:
  Level level_;
  std::string tag_;
  std::ostream devnull_;
};
