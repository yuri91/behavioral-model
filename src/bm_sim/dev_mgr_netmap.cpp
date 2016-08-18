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

#include <bm/bm_sim/dev_mgr.h>

#include <string>
#include <map>
#include <cassert>
#include <mutex>

#include "netmap/netmap_manager.h"


// These are private implementations

// Implementation that uses netmap to send/receive packets
// from true interfaces

namespace bm {

class NetmapDevMgrImp : public DevMgrIface {
 public:
  NetmapDevMgrImp(int device_id,
                  std::shared_ptr<TransportIface> notifications_transport) {
    p_monitor = PortMonitorIface::make_active(device_id,
                                              notifications_transport);
  }

 private:
  ~NetmapDevMgrImp() override {
  }

  ReturnCode port_add_(const std::string &iface_name, port_t port_num,
                       const char *in_pcap, const char *out_pcap) override {
    (void)in_pcap;
    (void)out_pcap;

    if (!mgr.add_interface(iface_name, port_num))
      return ReturnCode::ERROR;

    PortInfo p_info(port_num, iface_name);

    std::unique_lock<std::mutex> lock(mutex);
    port_info.emplace(port_num, std::move(p_info));

    return ReturnCode::SUCCESS;
  }

  ReturnCode port_remove_(port_t port_num) override {
    if (!mgr.remove_interface(port_num))
      return ReturnCode::ERROR;

    std::unique_lock<std::mutex> lock(mutex);
    port_info.erase(port_num);

    return ReturnCode::SUCCESS;
  }

  void transmit_fn_(int port_num, const char *buffer, int len) override {
    mgr.send(port_num, buffer, len);
  }

  void start_() override {
    assert(mgr.start_receive());
  }

  ReturnCode set_packet_handler_(const PacketHandler &handler, void *cookie)
      override {
    mgr.set_packet_handler(handler, cookie);
    return ReturnCode::SUCCESS;
  }

  bool port_is_up_(port_t port) const override {
    (void)port;
    bool is_up = true;  // TODO(yuri): actual check
    return is_up;
  }

  std::map<port_t, PortInfo> get_port_info_() const override {
    std::map<port_t, PortInfo> info;
    {
      std::unique_lock<std::mutex> lock(mutex);
      info = port_info;
    }
    for (auto &pi : info) {
      pi.second.is_up = port_is_up_(pi.first);
    }
    return info;
  }

 private:
  NetmapManager mgr;
  mutable std::mutex mutex;
  std::map<port_t, DevMgrIface::PortInfo> port_info;
};


void
DevMgr::set_dev_mgr_netmap(
    int device_id, std::shared_ptr<TransportIface> notifications_transport) {
  assert(!pimp);
  pimp = std::unique_ptr<DevMgrIface>(
      new NetmapDevMgrImp(device_id, notifications_transport));
}

}  // namespace bm
