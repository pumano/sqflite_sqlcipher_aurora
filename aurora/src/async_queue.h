/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC
 * <community@omp.ru> SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class AsyncQueue final {
public:
  using Task = std::function<void()>;
  // Invoked when a task throws; the message describes the exception. The
  // queue is a generic utility, so it only reports the exception — deciding
  // what to do with it (log, reply to the channel) is up to the owner.
  using ExceptionHandler = std::function<void(const std::string &)>;

  AsyncQueue();
  ~AsyncQueue();

  AsyncQueue(const AsyncQueue&) = delete;
  AsyncQueue(AsyncQueue&&) = delete;

  AsyncQueue& operator=(const AsyncQueue&) = delete;
  AsyncQueue& operator=(AsyncQueue&&) = delete;

  void push(const Task &task);
  bool isRunning() const;
  void setExceptionHandler(ExceptionHandler handler);

private:
  void run(std::promise<std::int32_t> promise);
  void notifyThread();
  void executeTasks();

private:
  enum State : std::uint32_t {
    Initial = 0,
    WaitThread,
    Running,
    Shutdown,
  };

  mutable std::mutex notifyMutex_;
  std::thread::id workingThreadId_;
  bool notified_ = false;
  std::condition_variable m_condition;
  std::atomic<std::uint32_t> state_ = State::Initial;
  std::unique_ptr<std::thread> thread_;
  std::vector<Task> tasks_;
  std::mutex taskMutex_;
  ExceptionHandler exceptionHandler_;
};
