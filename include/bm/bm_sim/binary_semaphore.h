/* Copyright 2016-present Universita` di Pisa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Yuri Iozzelli (y.iozzelli@gmail.com)
 *
 */

//! @file binary_semaphore.h

#ifndef BM_BM_SIM_BINARY_SEMAPHORE_H_
#define BM_BM_SIM_BINARY_SEMAPHORE_H_

#include <mutex>
#include <condition_variable>
#include <thread>

namespace bm{

//! A utility class implementing a binary semaphore.
//  Used in spsc_queue.h and lockless_queueing.h to synchronize producer and
//  consumer threads
class BinarySemaphore {
 public:
  BinarySemaphore():flag(false){}
  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [this](){ return flag; });
    flag = false;
  }
  template<class Rep, class Period>
  bool wait_for(const std::chrono::duration<Rep, Period>& rel_time) {
    std::unique_lock<std::mutex> lock(mutex);
    bool ret = condition.wait_for(lock, rel_time, [this](){ return flag; });
    flag = false;

    return  ret;
  }
  template<class Rep, class Period>
  bool wait_until(const std::chrono::time_point<Rep, Period>& timeout_time) {
    std::unique_lock<std::mutex> lock(mutex);
    bool ret = condition.wait_until(lock, timeout_time, [this](){return flag;});
    flag = false;

    return  ret;
  }
  void signal() {
    std::unique_lock<std::mutex> lock(mutex);
    flag = true;
    condition.notify_one();
  }
 private:
  mutable std::mutex mutex;
  mutable std::condition_variable condition;
  bool flag;
};

} // namespace bm

#endif  // BM_BM_SIM_BINARY_SEMAPHORE_H_
