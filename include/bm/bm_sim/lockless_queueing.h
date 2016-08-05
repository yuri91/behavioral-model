#ifndef BM_BM_SIM_LOCKLESS_QUEUEING_H_
#define BM_BM_SIM_LOCKLESS_QUEUEING_H_

#include <bm/bm_sim/spsc_queue.h>
#include <bm/bm_sim/binary_semaphore.h>

#include <deque>
#include <ratio>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <algorithm>  // for std::max

namespace bm {

//! One of the most basic queueing block possible. Lets you choose (at runtime)
//! the desired number of queues and the number of worker threads that
//! will be reading from these queues. //!
//!
//! It is similar to the QueueingLogic class, but uses SPSCQueue internally.
//! For this reason, at most one producer thread can push on a single queue,
//! while different producers can push to different queues.
//! The consumer threads must respect the mapping provided by `map_to_worker` 
//! (see below).
//!
//! As of now, the behavior is blocking for both read (pop_back()) and write
//! (push_front()).
//!
//! Template parameter `T` is the type (has to be movable) of the objects that
//! will be stored in the queues. Template parameter `FMap` is a callable object
//! that has to be able to map every logical queue id to a worker id. The
//! following is a good example of functor that meets the requirements:
//! @code
//! struct WorkerMapper {
//!   WorkerMapper(size_t nb_workers)
//!       : nb_workers(nb_workers) { }
//!
//!   size_t operator()(size_t queue_id) const {
//!     return queue_id % nb_workers;
//!   }
//!
//!   size_t nb_workers;
//! };
//! @endcode
template <typename T, typename FMap>
class QueueingLogicLL {
 public:
  QueueingLogicLL(size_t nb_queues, size_t nb_workers, size_t capacity,
                  FMap map_to_worker)
      : nb_queues(nb_queues), nb_workers(nb_workers),
        map_to_worker(map_to_worker), workers(nb_workers){
    for (size_t i = 0; i < nb_queues; i++){
      workers[map_to_worker(i)].add_queue(i);
      queues.emplace_back(new SPSCQueue<T>(capacity,workers[map_to_worker(i)].sem));
    }
  }

  //! Makes a copy of \p item and pushes it to the front of the logical queue
  //! with id \p queue_id.
  void push_front(size_t queue_id, const T &item) {
    queues.at(queue_id)->push_front_nb(item);
  }

  //! Moves \p item to the front of the logical queue with id \p queue_id.
  void push_front(size_t queue_id, T &&item) {
    queues.at(queue_id)->push_front_nb(std::move(item));
  }

  void pop_back(size_t worker_id, size_t *queue_id, T *pItem) {
    while(true) {
      WorkerInfo& worker = workers[worker_id];
      size_t n = worker.queues.size();
      for (size_t i = 0; i < n; i++) {
        size_t q = worker.next_queue();
        if (queues[q]->pop_back_nb(pItem)) {
          *queue_id = q;
          worker.next_queue();
          return;
        }
      }
      std::this_thread::sleep_for(std::chrono::microseconds(1));
      for (size_t i = 0; i < n; i++) {
        size_t q = worker.next_queue();
        if (queues[q]->pop_back_nb(pItem)) {
          *queue_id = q;
          worker.next_queue();
          return;
        }
      }
      for (size_t i = 0; i < n; i++) {
        size_t q = worker.next_queue();
        if (queues[q]->set_cons_event(1)) {
          queues[q]->pop_back_nb(pItem);
          *queue_id = q;
          worker.next_queue();
          return;
        }
      }
      worker.sem->wait();
    }
  }

  //! Deleted copy constructor
  QueueingLogicLL(const QueueingLogicLL &) = delete;
  //! Deleted copy assignment operator
  QueueingLogicLL &operator =(const QueueingLogicLL &) = delete;

  //! Deleted move constructor
  QueueingLogicLL(QueueingLogicLL &&) = delete;
  //! Deleted move assignment operator
  QueueingLogicLL &&operator =(QueueingLogicLL &&) = delete;


  //! Utility struct to keep track of the last checked queue. Needed to avoid 
  //  starvation (by doing a round robin schedule)
  struct WorkerInfo {
    std::shared_ptr<BinarySemaphore> sem;
    std::vector<size_t> queues;
    size_t cur_idx;

    WorkerInfo() {
      sem = std::make_shared<BinarySemaphore>();
      cur_idx = -1;
    }

    void add_queue(size_t q) {
      queues.push_back(q);
    }

    size_t next_queue() {
      cur_idx++;
      if (cur_idx == queues.size()) {
        cur_idx = 0;
      }
      return queues[cur_idx];
    }
  };

 private:
  size_t nb_queues;
  size_t nb_workers;
  FMap map_to_worker;
 public:
  std::vector<std::unique_ptr<SPSCQueue<T>>> queues;
  std::vector<WorkerInfo> workers;
};

//! This class is slightly more advanced than QueueingLogicLL. The difference
//! between the 2 is that this one offers the ability to rate-limit every
//! logical queue, by providing a maximum number of elements consumed per
//! second. If the rate is too small compared to the incoming packet rate, or if
//! the worker thread cannot sustain the desired rate, elements are buffered in
//! the queue. However, the write behavior (push_front()) for this class is
//! different than the one for QueueingLogic. It is not blocking: if the queue
//! is full, the function will return immediately and the element will not be
//! queued. Look at the documentation for QueueingLogic for more information
//! about the template parameters (they are the same).
template <typename T, typename FMap>
class QueueingLogicLLRL {
 public:
  using clock = std::chrono::high_resolution_clock;

  QueueingLogicLLRL(size_t nb_queues, size_t nb_workers, size_t capacity,
                  FMap map_to_worker)
      : nb_queues(nb_queues), nb_workers(nb_workers),
        map_to_worker(map_to_worker), workers(nb_workers){
    for (size_t i = 0; i < nb_queues; i++){
      workers[map_to_worker(i)].add_queue(i);
      queues.emplace_back(new SPSCQueue<T>(capacity,workers[map_to_worker(i)].sem));
    }
  }

  //! Makes a copy of \p item and pushes it to the front of the logical queue
  //! with id \p queue_id.
  void push_front(size_t queue_id, const T &item) {
    queues.at(queue_id)->push_front_nb(item);
  }

  //! Moves \p item to the front of the logical queue with id \p queue_id.
  void push_front(size_t queue_id, T &&item) {
    queues.at(queue_id)->push_front_nb(std::move(item));
  }

  void pop_back(size_t worker_id, size_t *queue_id, T *pItem) {
    WorkerInfo& worker = workers[worker_id];
    size_t n = worker.queues.size();
    while(true) {
      clock::time_point next_time = clock::time_point::max();

      for (size_t i = 0; i < n; i++) {
        QueueInfo& q = worker.next_queue();
        if (q.next_pop <= clock::now() && queues[q.index]->pop_back_nb(pItem)) {
          *queue_id = q.index;
          q.sent();
          worker.next_queue();
          return;
        } else if (q.next_pop > clock::now()) {
          next_time = std::min(next_time, q.next_pop);
        }
      }
      for (size_t i = 0; i < n; i++) {
        QueueInfo& q = worker.next_queue();
        if (q.next_pop <= clock::now() && queues[q.index]->set_cons_event(1)) {
          queues[q.index]->pop_back_nb(pItem);
          *queue_id = q.index;
          q.sent();
          worker.next_queue();
          return;
        } else if (q.next_pop > clock::now()) {
          next_time = std::min(next_time, q.next_pop);
        }
      }
      worker.sem->wait_until(next_time);
    }
  }

  void set_rate(size_t queue_id, uint64_t pps) {
    workers[map_to_worker(queue_id)].queues[queue_id].pps = pps;
  }

  //! Deleted copy constructor
  QueueingLogicLLRL(const QueueingLogicLLRL &) = delete;
  //! Deleted copy assignment operator
  QueueingLogicLLRL &operator =(const QueueingLogicLLRL &) = delete;

  //! Deleted move constructor
  QueueingLogicLLRL(QueueingLogicLLRL &&) = delete;
  //! Deleted move assignment operator
  QueueingLogicLLRL &&operator =(QueueingLogicLLRL &&) = delete;


 private:
  struct QueueInfo {
    size_t index;
    std::atomic<uint64_t> pps;
    clock::time_point last_pop;
    clock::time_point next_pop;
    QueueInfo(size_t index): index(index), pps(0){
      next_pop = clock::now();
    }
    QueueInfo(const QueueInfo& q): index(q.index){
      pps = q.pps.load();
      next_pop = clock::now();
    }
    void sent() {
      if (pps == 0ul) {
        return;
      } else {
        std::chrono::nanoseconds interval(1000000000 / pps);
        next_pop += interval;
      }
    }
    void set_rate(uint64_t pps) {
      this->pps.store(pps);
      next_pop = clock::now();
    }
  };

  //! Utility struct to keep track of the last checked queue. Needed to avoid 
  //  starvation (by doing a round robin schedule)
  struct WorkerInfo {
    std::shared_ptr<BinarySemaphore> sem;
    std::vector<QueueInfo> queues;
    size_t cur_idx;

    WorkerInfo() {
      sem = std::make_shared<BinarySemaphore>();
      cur_idx = -1;
    }

    void add_queue(size_t q) {
      queues.emplace_back(q);
    }

    QueueInfo& next_queue() {
      cur_idx++;
      if (cur_idx == queues.size()) {
        cur_idx = 0;
      }
      return queues[cur_idx];
    }

  };

  size_t nb_queues;
  size_t nb_workers;
  FMap map_to_worker;
 public:
  std::vector<std::unique_ptr<SPSCQueue<T>>> queues;
  std::vector<WorkerInfo> workers;
};
}  // namespace bm

#endif  // BM_BM_SIM_LOCKLESS_QUEUEING_H_
