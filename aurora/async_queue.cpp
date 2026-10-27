/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC
 * <community@omp.ru> SPDX-License-Identifier: BSD-3-Clause
 */
#include "src/async_queue.h"

AsyncQueue::AsyncQueue() = default;

AsyncQueue::~AsyncQueue() {
  state_ = State::Shutdown;
  notifyThread();
  if (thread_) {
    thread_->join();
  }
}

void AsyncQueue::notifyThread() {
  std::lock_guard<std::mutex> lock(notifyMutex_);
  notified_ = true;
  if (workingThreadId_ != std::this_thread::get_id()) {
    m_condition.notify_one();
  }
}

void AsyncQueue::push(const Task &task) {
  std::uint32_t expected = State::Initial;
  if (state_.compare_exchange_strong(expected, State::WaitThread)) {
    std::promise<std::int32_t> promise;
    std::future<std::int32_t> future = promise.get_future();
    thread_ =
        std::make_unique<std::thread>(&AsyncQueue::run, this, std::move(promise));
    future.wait();
  }

  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(taskMutex_);
    tasks_.push_back(task);
    changed = tasks_.size() == 1;
  }

  if (changed) {
    notifyThread();
  }
}

void AsyncQueue::setExceptionHandler(ExceptionHandler handler) {
  std::lock_guard<std::mutex> lock(taskMutex_);
  exceptionHandler_ = std::move(handler);
}

void AsyncQueue::executeTasks() {
  for (;;) {
    std::vector<Task> tasks;
    {
      std::lock_guard<std::mutex> lock(taskMutex_);
      std::swap(tasks, tasks_);
    }

    if (tasks.empty()) {
      return;
    }

    for (const auto &task : tasks) {
      if (!task) {
        continue;
      }

      try {
        task();
      } catch (const std::exception &e) {
        // The task threw before finishing. The queue is a generic utility
        // and cannot answer the channel call itself (it has no access to
        // the task's MethodResult); report the exception so it is at least
        // visible instead of the previous silent swallow. The plugin wraps
        // its tasks (enqueueTask) so the call is answered there.
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (exceptionHandler_)
          exceptionHandler_(e.what() ? e.what() : "unknown exception");
      } catch (...) {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (exceptionHandler_)
          exceptionHandler_("unknown non-std exception");
      }
    }
  }
}

void AsyncQueue::run(std::promise<std::int32_t> promise) {
  promise.set_value(1);
  {
    std::lock_guard<std::mutex> lock(notifyMutex_);
    if (workingThreadId_ != std::thread::id()) {
      return;
    }
    workingThreadId_ = std::this_thread::get_id();
  }

  std::uint32_t expected = State::WaitThread;
  if (!state_.compare_exchange_strong(expected, State::Running)) {
    return;
  }

  while (isRunning()) {
    executeTasks();

    std::unique_lock<std::mutex> lock(notifyMutex_);
    m_condition.wait(lock, [this] { return notified_ || !isRunning(); });
    notified_ = false;
  }
}

bool AsyncQueue::isRunning() const { return state_ == State::Running; }
