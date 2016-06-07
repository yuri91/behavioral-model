// Copyright [2016] <y.iozzelli@gmail.com>

#ifndef NETMAP_NETMAP_MANAGER_H_
#define NETMAP_NETMAP_MANAGER_H_

#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>
#include <memory>
#include <map>

#include "./netmap_interface.h"

typedef std::function<void(int, const char*, int, void*)> packet_handler_t;

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
  bool ports_changed {false};
  std::mutex ports_mutex{};
  std::mutex handler_mutex{};
  std::thread receive_thread{};
};

#endif  // NETMAP_NETMAP_MANAGER_H_
