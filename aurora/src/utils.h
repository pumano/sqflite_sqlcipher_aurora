/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC
 * <community@omp.ru> SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <optional>
#include <string>
#include <utility>

// Database transaction type
namespace DatabaseTransaction {
enum ID {
    None = -2,
    Force = -1,
};
}

namespace utils {

class error final {
public:
  explicit error(std::string message) : message_(std::move(message)) {}
  error(std::string message, int resultCode)
      : message_(std::move(message)), resultCode_(resultCode) {}
  error() = default;

  explicit operator bool() const { return message_.has_value(); }
  const std::string &message() const { return message_.value(); }
  const std::optional<int> &resultCode() const { return resultCode_; }

  static error none() { return {}; }

private:
  std::optional<std::string> message_;
  std::optional<int> resultCode_;
};

} /* namespace utils */
