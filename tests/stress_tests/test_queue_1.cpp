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
 * Yuri Iozzelli (y.iozzelli@gmail.com)
 *
 */

#include <vector>
#include <string>
#include <iostream>
#include <memory>

#include <boost/filesystem.hpp>

#include "stress_utils.h"

#include <bm/bm_sim/queue.h>
#include <bm/bm_sim/spsc_queue.h>

using LockQueue = bm::Queue<int>;
using SPSCQueue = bm::SPSCQueue<int>;

using Queue = SPSCQueue;

using ::stress_tests_utils::SwitchTest;
using ::stress_tests_utils::TestChrono;

namespace fs = boost::filesystem;

void stage1(Queue& queue,
              size_t num_repeats) {
  for (int i = 0; i < num_repeats; i++) {
    queue.push_front(1);
    //std::this_thread::sleep_for(std::chrono::microseconds(1000));
  }
}

void stage2(Queue& queue, size_t num_repeats) {
  int pkt;
  for (int i = 0; i < num_repeats; i++) {
    queue.pop_back(&pkt);
    //std::this_thread::sleep_for(std::chrono::microseconds(1000));
  }
}

int main(int argc, char* argv[]) {
  size_t num_repeats = 1000;
  size_t queue_size = 1024;
  if (argc > 1) num_repeats = std::stoul(argv[1]);
  if (argc > 2) queue_size = std::stoul(argv[2]);

  Queue queue(queue_size);

  TestChrono chrono(num_repeats);
  chrono.start();
  std::thread s1(stage1,std::ref(queue),num_repeats);
  std::thread s2(stage2,std::ref(queue),num_repeats);

  s1.join();
  s2.join();

  chrono.end();
  chrono.print_summary();
}
