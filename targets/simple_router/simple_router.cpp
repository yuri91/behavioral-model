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

#include <bm/bm_sim/queue.h>
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

using bm::Switch;
//using bm::Queue;
#include <bm/bm_sim/spsc_queue.h>
template<typename T>
using Queue=bm::SPSCQueue<T>;
using bm::Packet;
using bm::PHV;
using bm::Parser;
using bm::Deparser;
using bm::Pipeline;

class SimpleSwitch : public Switch {
 public:
  SimpleSwitch()
    : Switch(true),  // enable_switch = true
      input_buffer(1024), output_buffer(128) { }

  int receive(int port_num, const char *buffer, int len, uint64_t flags) {
    (void) flags;
    static int pkt_id = 0;
    packet_count_in++;
    if (this->do_swap() == 0)  // a swap took place
      swap_happened = true;

    auto packet = new_packet_ptr(port_num, pkt_id++, len,
                                 bm::PacketBuffer(2048, buffer, len));

    BMELOG(packet_in, *packet);

    input_buffer.push_front(std::move(packet), flags==0);
    return 0;
  }

  void start_and_return() {
    std::thread t1(&SimpleSwitch::pipeline_thread, this);
    t1.detach();
    std::thread t2(&SimpleSwitch::transmit_thread, this);
    t2.detach();
    std::thread t3(&SimpleSwitch::stats_thread, this);
    t3.detach();
  }

 private:
  void pipeline_thread();
  void transmit_thread();
  void stats_thread();

 private:
  Queue<std::unique_ptr<Packet> > input_buffer;
  Queue<std::unique_ptr<Packet> > output_buffer;
  bool swap_happened{false};

  // XXX variables for stat printing
  uint64_t packet_count_in{0};
  uint64_t packet_count_out{0};
  uint64_t avg_latency{0};
  uint64_t max_latency{0};
};

void SimpleSwitch::stats_thread() {
  uint64_t old_packet_count=0;
  int period = 200;
  while(true) {
    float delta_t = period*1000000.0/(packet_count_in-old_packet_count);

    std::cout<<"cycle time: "<<delta_t<<" ns/pkt"<<" / "
             <<"throughput:  "<<1000000000.0/delta_t<<" pkt/s"<<" / "
             <<"avg latency: "<<(0.000001*avg_latency)/packet_count_out<<" ms"<<" / "
             <<"max latency: "<<0.000001*max_latency<<" ms"
             <<std::endl;

    old_packet_count=packet_count_in;

    std::this_thread::sleep_for(std::chrono::milliseconds(period));
  }
}

void SimpleSwitch::transmit_thread() {
  while (1) {
    std::unique_ptr<Packet> packet;
    output_buffer.pop_back(&packet);
    BMELOG(packet_out, *packet);
    BMLOG_DEBUG_PKT(*packet, "Transmitting packet of size {} out of port {}",
                    packet->get_data_size(), packet->get_egress_port());
    auto ingress_ts = packet->get_ingress_ts();
    std::chrono::nanoseconds delta = std::chrono::system_clock::now() - ingress_ts;
    if (uint64_t(delta.count()) > max_latency) max_latency = delta.count();
    avg_latency+= delta.count();
    packet_count_out++;
    transmit_fn(packet->get_egress_port(),
                packet->data(), packet->get_data_size());
  }
}

void SimpleSwitch::pipeline_thread() {
  Pipeline *ingress_mau = this->get_pipeline("ingress");
  Pipeline *egress_mau = this->get_pipeline("egress");
  Parser *parser = this->get_parser("parser");
  Deparser *deparser = this->get_deparser("deparser");
  PHV *phv;

  while (1) {
    std::unique_ptr<Packet> packet;
    input_buffer.pop_back(&packet);
    phv = packet->get_phv();

    int ingress_port = packet->get_ingress_port();
    (void) ingress_port;
    BMLOG_DEBUG_PKT(*packet, "Processing packet received on port {}",
                    ingress_port);

    // update pointers if needed
    if (swap_happened) {  // a swap took place
      ingress_mau = this->get_pipeline("ingress");
      egress_mau = this->get_pipeline("egress");
      parser = this->get_parser("parser");
      deparser = this->get_deparser("deparser");
      swap_happened = false;
    }

    parser->parse(packet.get());
    ingress_mau->apply(packet.get());

    int egress_port = phv->get_field("standard_metadata.egress_spec").get_int();
    BMLOG_DEBUG_PKT(*packet, "Egress port is {}", egress_port);

    if (egress_port == 511) {
      BMLOG_DEBUG_PKT(*packet, "Dropping packet");
    } else {
      packet->set_egress_port(egress_port);
      egress_mau->apply(packet.get());
      deparser->deparse(packet.get());
      output_buffer.push_front(std::move(packet));
    }
  }
}

/* Switch instance */

static SimpleSwitch *simple_switch;


int
main(int argc, char* argv[]) {
  simple_switch = new SimpleSwitch();
  int status = simple_switch->init_from_command_line_options(argc, argv);
  if (status != 0) std::exit(status);

  // should this be done by the call to init_from_command_line_options
  int thrift_port = simple_switch->get_runtime_port();
  bm_runtime::start_server(simple_switch, thrift_port);

  simple_switch->start_and_return();

  while (true) std::this_thread::sleep_for(std::chrono::seconds(100));

  return 0;
}
