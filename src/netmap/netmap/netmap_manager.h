/* Copyright 2016 University of Pisa
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

#ifndef NETMAP_NETMAP_NETMAP_MANAGER_H_
#define NETMAP_NETMAP_NETMAP_MANAGER_H_

#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>
#include <memory>
#include <map>

#include "./netmap_interface.h"

typedef std::function<void(int, const char*, int, void*)>
        packet_handler_t;

class NetmapManager {
 public:
  NetmapManager() {}
  ~NetmapManager() { stop_receive(); }

  void set_packet_handler(packet_handler_t ph, void* cookie);

  bool start_receive();
  void stop_receive();

  bool send(int port, const char* buf, int len);

  void flush_out();

  bool add_interface(std::string ifname, int port);
  bool remove_interface(int port_num);

 private:
  void receive_loop();

  packet_handler_t packet_handler{};
  void* packet_cookie{nullptr};
  std::map<int, std::unique_ptr<NetmapInterface>> ports{};
  std::atomic_bool started{false};
  std::atomic_bool stopping{false};
  std::atomic_bool ports_changed {false};
  std::mutex ports_mutex{};
  std::mutex handler_mutex{};
  std::thread receive_thread{};
};

#endif  // NETMAP_NETMAP_NETMAP_MANAGER_H_
