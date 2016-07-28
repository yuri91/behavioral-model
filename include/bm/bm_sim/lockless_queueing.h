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
        map_to_worker(map_to_worker), reverse_mapping(nb_workers){
    for (size_t i = 0; i < nb_queues; i++){
      reverse_mapping[map_to_worker(i)].push_back(i);
      if (i/nb_workers==0) {
        worker_sem.push_back(std::make_shared<Semaphore>());
      }
      queues.emplace_back(new SPSCQueue<T>(capacity,worker_sem.back()));
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
      for (auto i : reverse_mapping[worker_id]) {
        if (queues[i]->pop_back_nb(pItem)) {
          *queue_id = i;
          return;
        }
      }
      std::this_thread::sleep_for(std::chrono::microseconds(1));
      for (auto i : reverse_mapping[worker_id]) {
        if (queues[i]->pop_back_nb(pItem)) {
          *queue_id = i;
          return;
        }
      }
      for (auto i : reverse_mapping[worker_id]) {
        if (queues[i]->cons_set_event(1)) {
          queues[i]->pop_back_nb(pItem);
          *queue_id = i;
          return;
        }
      }
      worker_sem[worker_id]->wait();
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

 private:
  size_t nb_queues;
  size_t nb_workers;
  FMap map_to_worker;
  std::vector<std::unique_ptr<SPSCQueue<T>>> queues;
  std::vector<std::vector<size_t>> reverse_mapping;
  std::vector<std::shared_ptr<Semaphore>> worker_sem;
};

}  // namespace bm

#endif  // BM_BM_SIM_LOCKLESS_QUEUEING_H_
