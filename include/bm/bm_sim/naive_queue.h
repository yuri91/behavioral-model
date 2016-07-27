/* Copyright 2013-present Barefoot Networks, Inc.
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
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

//! @file queue.h

#ifndef BM_BM_SIM_NAIVE_QUEUE_H_
#define BM_BM_SIM_NAIVE_QUEUE_H_

#include <deque>
#include <mutex>
#include <condition_variable>

namespace {
class Semaphore {
 public:
  Semaphore():flag(false){}
  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [this](){ return flag; });
    flag = false;
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
}

namespace bm {

/* TODO(antonin): implement non blocking read behavior */

//! A utility queueing class made available to all targets. It could be used
//! -for example- to queue packets between an ingress thread and an egress
//! thread. This is a very simple class, which does not implement anything fancy
//! (e.g. rate limiting, priority queueing, fair scheduling, ...) but can be
//! used as a base class to build something more advanced.
//! Queue includes a mutex and is thread-safe.
template <class T>
class NaiveQueue {
 public:
  NaiveQueue()
    : capacity(1024), ring(new T[capacity]) { }

  //! Constructs a queue with specified \p capacity and read / write behaviors
  NaiveQueue(size_t capacity)
    : capacity(capacity), ring(new T[capacity]) {}

  //! Moves \p item to the front of the queue
  void push_front(T &&item) {
    while (!is_not_full()) {
      prod_sem.wait();
    }
    ring[prod_index] = std::move(item);
    prod_index = wrap(prod_index+1);
    cons_sem.signal();
    prod_not++;
  }

  //! Pops an element from the back of the queue: moves the element to `*pItem`.
  void pop_back(T* pItem) {
    while (!is_not_empty()) {
      cons_sem.wait();
    }
    *pItem = std::move(ring[cons_index]);
    cons_index = wrap(cons_index+1);
    prod_sem.signal();
    cons_not++;
  }

 private:
  bool is_not_empty() const { return prod_index != cons_index; }
  bool is_not_full() const { return wrap(prod_index+1)!=cons_index; }

  int wrap(int idx) const { return (idx % capacity + capacity) % capacity; }

  size_t capacity;
  std::unique_ptr<T[]> ring;
  std::atomic<int> prod_index;
  std::atomic<int> cons_index;

  Semaphore prod_sem;
  Semaphore cons_sem;
 public:
  uint64_t cons_not{0};
  uint64_t prod_not{0};

};

}  // namespace bm

#endif  // BM_BM_SIM_NAIVE_QUEUE_H_
