#ifndef BM_BM_SIM_LOCKLESS_QUEUEING_H_
#define BM_BM_SIM_LOCKLESS_QUEUEING_H_

#include <bm/bm_sim/spsc_queue.h>
#include <deque>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <algorithm>  // for std::max

namespace bm {

//! One of the most basic queueing block possible. Lets you choose (at runtime)
//! the desired number of logical queues and the number of worker threads that
//! will be reading from these queues. I write "logical queues" because the
//! implementation actually uses as many physical queues as there are worker
//! threads. However, each logical queue still has its own maximum capacity.
//! As of now, the behavior is blocking for both read (pop_back()) and write
//! (push_front()), but we may offer additional options if there is interest
//! expressed in the future.
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
    queues.at(queue_id)->push_front(item);
  }

  //! Moves \p item to the front of the logical queue with id \p queue_id.
  void push_front(size_t queue_id, T &&item) {
    queues.at(queue_id)->push_front(std::move(item));
  }

  void pop_back(size_t worker_id, size_t *queue_id, T *pItem) {
    while(true) {
      WorkerInfo& worker = workers[worker_id];
      //size_t n = worker.queues.size();
      constexpr size_t n = 1;
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
        if (queues[q]->cons_set_event(1)) {
          queues[q]->pop_back_nb(pItem);
          *queue_id = q;
          worker.next_queue();
          return;
        }
      }
      worker.sem->wait();
    }
  }

  //! Get the occupancy of the logical queue with id \p queue_id.
  size_t size(size_t queue_id) const {
    return queues.at(queue_id).size();
  }

  //! Deleted copy constructor
  QueueingLogicLL(const QueueingLogicLL &) = delete;
  //! Deleted copy assignment operator
  QueueingLogicLL &operator =(const QueueingLogicLL &) = delete;

  //! Deleted move constructor
  QueueingLogicLL(QueueingLogicLL &&) = delete;
  //! Deleted move assignment operator
  QueueingLogicLL &&operator =(QueueingLogicLL &&) = delete;


  struct WorkerInfo {
    std::shared_ptr<Semaphore> sem;
    std::vector<size_t> queues;
    size_t cur_idx;

    WorkerInfo() {
      sem = std::make_shared<Semaphore>();
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

}  // namespace bm

#endif  // BM_BM_SIM_LOCKLESS_QUEUEING_H_
