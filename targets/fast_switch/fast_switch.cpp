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

#include <bm/bm_sim/spsc_queue.h>
#include <bm/bm_sim/packet.h>
#include <bm/bm_sim/parser.h>
#include <bm/bm_sim/tables.h>
#include <bm/bm_sim/switch.h>
#include <bm/bm_sim/event_logger.h>

#include <bm/bm_runtime/bm_runtime.h>

#include <unistd.h>

#include <iostream>
#include <memory>
#include <thread>
#include <fstream>
#include <string>
#include <chrono>

#include <iostream>
#include <iomanip>


struct cmp {
  bool operator()(const std::unique_ptr<bm::Packet> &lhs, const std::unique_ptr<bm::Packet> &rhs) const {
    return lhs->get_packet_id() < rhs->get_packet_id();
  }
};

template<typename T>
//using Queue = bm::SPSCQueue<T,std::priority_queue<T,std::vector<T>,cmp>>;
using Queue = bm::SPSCQueue<T>;

using bm::Switch;
using bm::Packet;
using bm::PHV;
using bm::Parser;
using bm::Deparser;
using bm::Pipeline;

class FastSwitch : public Switch {
 public:
  FastSwitch()
    : Switch(true),  // enable_swap = false
      input_buffer(1024), process_buffer(1024), output_buffer(1024) { }

  int receive(int port_num, const char *buffer, int len, uint64_t flags) {
    static int pkt_id = 0;

    (void)flags;
    //if (this->do_swap() == 0)  // a swap took place
    //  swap_happened = true;

    auto packet = new_packet_ptr(port_num, pkt_id++, len,
                                 bm::PacketBuffer(len+512, buffer, len));

    BMELOG(packet_in, *packet);

    packet_count++;
    Parser *parser = this->get_parser("parser");
    parser->parse(packet.get());

    input_buffer.push_front(std::move(packet), flags==0);
    return 0;
  }

  void start_and_return() {
    std::thread t1(&FastSwitch::ingress_thread, this);
    t1.detach();
    std::thread t2(&FastSwitch::egress_thread, this);
    t2.detach();
    std::thread t3(&FastSwitch::transmit_thread, this);
    t3.detach();
    std::thread t4(&FastSwitch::stats_thread, this);
    t4.detach();
  }

 private:
  void ingress_thread();
  void egress_thread();
  void transmit_thread();

  void stats_thread();

 private:
  Queue<std::unique_ptr<Packet>> input_buffer;
  Queue<std::unique_ptr<Packet>> process_buffer;
  Queue<std::unique_ptr<Packet>> output_buffer;
  bool swap_happened{false};

  // XXX variables for stat printing
  uint64_t packet_count{0};  // one entry per port

};

void FastSwitch::stats_thread() {
  uint64_t old_packet_count=0;

  int period = 200;
  while(true) {
    std::cout<<period*1000000.0/(packet_count-old_packet_count)<<" ns/pkt "
             <<" "<<1000.0*(packet_count-old_packet_count)/period<<" pkt/s"
             <<std::endl;

    old_packet_count=packet_count;

    std::this_thread::sleep_for(std::chrono::milliseconds(period));
  }
}

void FastSwitch::transmit_thread() {
  Deparser *deparser = this->get_deparser("deparser");
  while (1) {
    std::unique_ptr<Packet> packet;
    output_buffer.pop_back(&packet);
    deparser->deparse(packet.get());
    BMELOG(packet_out, *packet);
    BMLOG_DEBUG_PKT(*packet, "Transmitting packet of size {} out of port {}",
                    packet->get_data_size(), packet->get_egress_port());
    transmit_fn(packet->get_egress_port(),
                packet->data(), packet->get_data_size());
  }
}

void FastSwitch::ingress_thread() {
  Pipeline *ingress_mau = this->get_pipeline("ingress");
  while (1) {
    std::unique_ptr<Packet> packet;
    input_buffer.pop_back(&packet);
    //continue;
    int ingress_port = packet->get_ingress_port();
    (void) ingress_port;
    BMLOG_DEBUG_PKT(*packet, "Processing packet received on port {}",
                    ingress_port);


    ingress_mau->apply(packet.get());

    process_buffer.push_front(std::move(packet));
  }
}

void FastSwitch::egress_thread() {
  Pipeline *egress_mau = this->get_pipeline("egress");
  PHV *phv;

  while (1) {
    std::unique_ptr<Packet> packet;
    process_buffer.pop_back(&packet);
    //continue;
    phv = packet->get_phv();


    int egress_port = phv->get_field("standard_metadata.egress_spec").get_int();
    BMLOG_DEBUG_PKT(*packet, "Egress port is {}", egress_port);

    if (egress_port == 511) {
      BMLOG_DEBUG_PKT(*packet, "Dropping packet");
    } else {
      packet->set_egress_port(egress_port);
      egress_mau->apply(packet.get());
      output_buffer.push_front(std::move(packet));
    }
  }
}

/* Switch instance */

static FastSwitch *fast_switch;

int
main(int argc, char* argv[]) {
  fast_switch = new FastSwitch();
  int status = fast_switch->init_from_command_line_options(argc, argv);
  if (status != 0) std::exit(status);

  // should this be done by the call to init_from_command_line_options
  int thrift_port = fast_switch->get_runtime_port();
  bm_runtime::start_server(fast_switch, thrift_port);

  fast_switch->start_and_return();

  while (true) std::this_thread::sleep_for(std::chrono::seconds(100));

  return 0;
}