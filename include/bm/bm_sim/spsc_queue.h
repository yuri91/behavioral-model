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

//! @file spsc_queue.h

#ifndef BM_BM_SIM_SPSC_QUEUE_H_
#define BM_BM_SIM_SPSC_QUEUE_H_

#include <memory>
#include <string>
#include <iostream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <queue>

#include <cmath>
#include <cassert>

namespace {
constexpr int cons_sleep_time{1}; // microseconds
}


namespace bm{

//! XXX A utility class implementing a binary semaphore to synchronize
//! producer and consumer on the queue.
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


/*
 * This class implements a Single-producer Single-consumer lockless queue.
 * Support for batch insertions is through the 'force' argument which,
 * when set, exports the new queue index to the consumer.
 * XXX we also export the info if the queue becomes full
 * Removal can be of a single packet, or of an array.
 *
 * Indexes span the entire index_t range, so actual accesses need
 * to use normalize_index() to reduce to ring_capacity (the physical
 * number of entries). The logical size queue_capacity can be
 * arbitrary, ring_capacity is the next power of 2 >= queue_capacity.
 *
 * The class uses atomic variables for the shared indexes between consumer and producer
 * threads. In order to avoid unnecessary notifications on the semaphores, both
 * the producer and the consumer set an event index corresponding to the point
 * in the queue at which the other thread issues a notification.
 * The consumer sets the event at the next producer index, while the producer
 * can set a point further on in the ring.
 */
template <class T>
class SPSCQueue {
 public:
  using index_t = uint64_t; // XXX could be uint32_t
  using atomic_index_t = std::atomic<index_t>;
  static constexpr size_t max_size = 1ul<<(sizeof(index_t)*8-1);

  /*
   * Multiple queues can share the same semaphore for synchronisation.
   * By default, the consumer and producer semaphores (cs, ps) are
   * null and the class creates private objects.
   * Shared semaphores, if needed, should be passed as arguments
   * to the constructor.
   */
  SPSCQueue(size_t max_capacity = 1024, 
            std::shared_ptr<Semaphore> cs=nullptr,
            std::shared_ptr<Semaphore> ps=nullptr)
    : ring_capacity(1ul<<uint64_t(ceil(log2(max_capacity)))),
      queue_capacity(max_capacity),
      ring(new T[ring_capacity]),
      cons_sem_ptr(cs),prod_sem_ptr(ps) {
    assert(ring_capacity <= max_size);
    if (cons_sem_ptr.get()==nullptr) {
      cons_sem_ptr = std::make_shared<Semaphore>();
    }
    if (prod_sem_ptr.get()==nullptr) {
      prod_sem_ptr = std::make_shared<Semaphore>();
    }
  }

  // Blocking and non-blocking versions of insert functions,
  // only for one item at a time.

  //! Moves \p item to the front of the queue (producer)
  bool push_front(T&& item, bool force = true) {
    return push_front_forward(std::move(item), force);
  }
  //! Copies \p item to the front of the queue (producer)
  bool push_front(const T& item, bool force = true) {
    return push_front_forward(item, force);
  }
  //! Moves \p item to the front of the queue (producer). Non-blocking version
  bool push_front_nb(T&& item, bool force = true) {
    return push_front_forward_nb(std::move(item), force);
  }
  //! Copies \p item to the front of the queue (producer). Non-blocking version
  bool push_front_nb(const T& item, bool force = true) {
    return push_front_forward_nb(item, force);
  }

  // Blocking and non blocking versions of remove functions,
  // for one or a vector of items.

  //! Pops an element from the back of the queue: moves the element to `*pItem`.
  // (consumer)
  bool pop_back(T* pItem) {
    cons_wait_data(1);
    *pItem = std::move(ring[normalize_index(cons_ci)]);
    cons_advance(1);

    return true;
  }
  //! Pops an element from the back of the queue: moves the element to `*pItem`.
  // (consumer)
  bool pop_back(std::vector<T>* container) {
    index_t num = cons_wait_data(1);
    for (index_t i = 0; i < num; i++) {
      container->push_back(std::move(ring[normalize_index(cons_ci+i)]));
    }
    cons_advance(num);

    return true;
  }

  bool pop_back_nb(T* pItem) {
    if (!cons_has_data(1)) {
      return false;
    }

    *pItem = std::move(ring[normalize_index(cons_ci)]);
    cons_advance(1);

    return true;
  }

  //  Used by the consumer to set the cons_event.
  //  Returns true if `wait` elements available, false otherwise
  bool set_cons_event(index_t want) {
    //request wake up when prod_index > cons_event
    cons_event = cons_ci + want - 1;
    cons_notify();
    if (cons_has_data(want)) {
      return true;
    }
    return false;
  }

  // How many items are in the queue. But it makes no sense without
  // indicating which side is requesting the information, because both
  // consumer and producer may move their pointers without notifying
  // the other side.
  // XXX remove this and check how clients break.
  // NOTE: this is an approximation of the real size
  //size_t size() {
  //  return __prod_index - __cons_index;
  //}

  // copy and move constructors and assignment are not supported:
  //! Deleted copy constructor
  SPSCQueue(const SPSCQueue &) = delete;
  //! Deleted copy assignment operator
  SPSCQueue &operator =(const SPSCQueue &) = delete;

  //! Deleted move constructor (class includes mutex)
  SPSCQueue(SPSCQueue &&) = delete;
  //! Deleted move assignment operator (class includes mutex)
  SPSCQueue &&operator =(SPSCQueue &&) = delete;

 private:
  //! Moves/Copy \p item to the front of the queue (producer)
  // This new template parameter U is necessary so the std::forward<U>()
  // used below can automatically deduce if item is an rvalue reference or
  // a const reference
  template <typename U>
  bool push_front_forward(U &&item, bool force) {
    prod_wait_space(1);
    // sure to have space
    ring[normalize_index(prod_pi)] = std::forward<U>(item);
    prod_advance(1, force);

    return true;
  }

  //! Moves/Copy \p item to the front of the queue (producer)
  // This new template parameter U is necessary so the std::forward<U>()
  // used below can automatically deduce if item is an rvalue reference or
  // a const reference. Non-blocking version
  template <typename U>
  bool push_front_forward_nb(U &&item, bool force) {
    if (!prod_has_space(1)) return false;
    // sure to have space
    ring[normalize_index(prod_pi)] = std::forward<U>(item);
    prod_advance(1, force);

    return true;
  }


  //! Used by the consumer to wait for 'want' elements.
  //  Returns number of available elements
  index_t cons_wait_data(index_t want) {
    while (true) {
      if (cons_has_data(want)) break;
      // sleep a while and retry
      std::this_thread::sleep_for(std::chrono::microseconds(cons_sleep_time));
      if (cons_has_data(want)) break;

      if (set_cons_event(want)) break;

      cons_sem_ptr->wait(); // wait for notification
    }
    return cons_pi - cons_ci;
  }

  // used by consumer to update shared consumer index and signal the producer
  // if producer event is reached
  void cons_notify() {
    index_t old = __cons_index;
    // update shared consumer index
    __cons_index = cons_ci;

    index_t pe = prod_event;

    if (index_t(cons_ci - pe - 1) < index_t(cons_ci - old)) {
      prod_sem_ptr->signal();
      prod_notified++;
    }
  }

  // used by consumer to advance its index and check if a notification is needed
  void cons_advance(index_t have) {
    cons_ci += have;
    if(cons_pi == cons_ci) {
      cons_notify();
    }
  }

  // used by the producer to wait until 'want' slots are available to fill
  // returns the number of available slots
  index_t prod_wait_space(index_t want) { // returns available space
    while (true) {
      if (prod_has_space(want)) {
        break;
      }
      // wake up when prod_c moves past prod_event (75% of current size)
      prod_event = index_t(prod_ci + index_t(prod_pi-prod_ci)/4);
      prod_notify();
      if (prod_has_space(want)) { // double check
        break;
      }
      // not enough space
      prod_sem_ptr->wait();
      prod_ci = __cons_index;
    }
    return prod_ci + queue_capacity - prod_pi;
  }

  // used by producer to update shared producer index and signal the consumer
  // if consumer event is reached
  void prod_notify() {
    index_t old = __prod_index;
    __prod_index = prod_pi;

    index_t ce = cons_event;

    if (index_t(prod_pi - ce - 1) < index_t(prod_pi - old)) {
      cons_sem_ptr->signal();
      cons_notified++;
    }
  }

  // used by consumer to advance its index
  // update shared state and notify only if 'force' is true
  void prod_advance(index_t have, bool force) {
    prod_pi+=have;
    if (force) {
      prod_notify();
    }
  }

  // maps the index position in the ring to the actual array index
  size_t normalize_index(index_t index) {
    return index & (ring_capacity-1);
  }

  // check if 'want' slots are available for the producer
  bool prod_has_space(index_t want) {
    prod_ci = __cons_index;
    return (index_t(prod_pi-prod_ci) <= queue_capacity - want);
  }

  // check if 'want' elements are available for the consumer
  bool cons_has_data(index_t want) {
    cons_pi = __prod_index;
    return (index_t(cons_pi - cons_ci) >= want);
  }

private:
  alignas(64)
  const size_t ring_capacity;
  const size_t queue_capacity;
  const std::unique_ptr<T[]> ring;
  alignas(64)
  std::shared_ptr<Semaphore> cons_sem_ptr;
  std::shared_ptr<Semaphore> prod_sem_ptr;

  alignas(64)
  atomic_index_t __prod_index{0}; // index of the next element to produce
  atomic_index_t prod_event{0}; // wake up when cons_index > prod_event
  index_t prod_ci{0}; // copy of consumer index (used by producer)
  index_t prod_pi{0}; // copy of producer index (used by producer)

  alignas(64)
  atomic_index_t __cons_index{0}; // index of the next element to consume
  atomic_index_t cons_event{0}; // wake up when prod_index > cons_event
  index_t cons_ci{0}; // copy of consumer index (used by consumer)
  index_t cons_pi{0}; // copy of producer index (used by consumer)

 public:
  alignas(64)
  uint64_t prod_notified{0};
  alignas(64)
  uint64_t cons_notified{0};
};

} // namespace bm

#endif // BM_BM_SIM_SPSC_QUEUE_H_
