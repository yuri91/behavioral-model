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

#include <poll.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>
#include <string>

#include "./netmap/netmap_manager.h"

#define POLL_TIMEOUT 1

void NetmapManager::set_packet_handler(packet_handler_t ph, void* c) {
  std::lock_guard<std::mutex> lock(handler_mutex);
  packet_handler = ph;
  packet_cookie = c;
}

bool NetmapManager::start_receive() {
  if (started || stopping)
    return false;

  receive_thread = std::thread(&NetmapManager::receive_loop, this);
  started = true;

  return true;
}

void NetmapManager::stop_receive() {
  stopping = true;
  receive_thread.join();
  stopping = false;
  started = false;
}

bool NetmapManager::send(int port, const char* buf, int len) {
  auto it = ports.find(port);

  if (it == ports.end()) return false;

  // pollfd pfd{it->second->fd(),POLLOUT,0};
  int r = it->second->inject(buf, len);
  while (r <= 0) {
    // poll(&pfd,1,-1);

    // sync_tx is NOT blocking
    it->second->sync_tx();
    r = it->second->inject(buf, len);
  }
  return r == len;
}

void NetmapManager::flush_out() {
  std::lock_guard<std::mutex> lock(ports_mutex);
  for (auto& port : ports) {
    NetmapInterface& intf = *port.second;
    if (intf.pending_tx())
      intf.sync_tx();
  }
}

bool NetmapManager::add_interface(std::string ifname, int port) {
  std::lock_guard<std::mutex> lock(ports_mutex);
  if (ports.find(port) != ports.end()) {
    return false;
  }
  ports.emplace(port, std::unique_ptr<NetmapInterface>(
                new NetmapInterface(ifname)));
  ports_changed = true;
  return true;
}

bool NetmapManager::remove_interface(int port) {
  std::lock_guard<std::mutex> lock(ports_mutex);
  ports_changed = true;
  return ports.erase(port) == 1;
}

void NetmapManager::receive_loop() {
  std::vector<pollfd> pfds;

  while (!stopping) {
    // check if ports have been added/removed and update pfds
    if (ports_changed) {
      std::lock_guard<std::mutex> lock(ports_mutex);
      pfds.clear();
      for (auto& p : ports) {
        pfds.emplace_back(pollfd{p.second->fd(), POLLIN, 0});
      }
      ports_changed = false;
    }

    poll(&pfds[0], pfds.size(), POLL_TIMEOUT);
    int i = 0;
    for (auto& p : ports) {
      if (pfds[i].revents & POLLIN) {
        char* buf = nullptr;
        int len = p.second->nextpkt(&buf);
        while (len > 0) {
          char* buf_next = nullptr;
          int len_next = p.second->nextpkt(&buf_next);
          if (packet_handler) {
            packet_handler(p.first, buf, len, packet_cookie);
          }
          len = len_next;
          buf = buf_next;
        }
      }
      i++;
    }
  }
  stopping = false;
  started = false;
}
