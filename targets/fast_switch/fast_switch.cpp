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

#include <cstdio>

using q_elem_t = std::unique_ptr<bm::Packet>;

/*
 * We can test different queue types by setting QUEUE_TYPE accordingly.
 */
#define LOCKING 1	/* original mutex-based queue */
#define SPSC 2		/* single producer, single consumer lockless queue */
#define MULTI 3		/* multiple queues, single producer multiple consumers */

#define QUEUE_TYPE MULTI


#if QUEUE_TYPE == LOCKING

#include <bm/bm_sim/queue.h>
using Queue = bm::Queue<q_elem_t>;

#elif QUEUE_TYPE == SPSC

#include <bm/bm_sim/spsc_queue.h>
using Queue = bm::SPSCQueue<q_elem_t>;

#elif QUEUE_TYPE == MULTI

#include <bm/bm_sim/lockless_queueing.h>
#define num_queues 1
#define num_workers 1

struct WorkerMapper {
  WorkerMapper(size_t nb_workers)
      : nb_workers(nb_workers) { }

  size_t operator()(size_t queue_id) const {
    return queue_id % nb_workers;
  }

  size_t nb_workers;
};
using Queue = bm::QueueingLogicLL<q_elem_t, WorkerMapper>;

#else

#error --- QUEUE_TYPE must be defined ---

#endif

using bm::Switch;
using bm::Packet;
using bm::PHV;
using bm::Parser;
using bm::Deparser;
using bm::Pipeline;


#define SINGLE_STAGE 0

constexpr static int queue_size = 512;

class FastSwitch : public Switch {
 public:
  FastSwitch()
    : Switch(true),  // enable_swap = false  
#if QUEUE_TYPE == LOCKING || QUEUE_TYPE == SPSC
    input_buffer(queue_size),
    process_buffer(queue_size),
    output_buffer(queue_size)
#elif QUEUE_TYPE == MULTI
    input_buffer(num_queues,num_workers,queue_size,WorkerMapper(num_workers)),
    process_buffer(num_queues,num_workers,queue_size,WorkerMapper(num_workers)),
    output_buffer(num_queues,num_workers,queue_size,WorkerMapper(num_workers))
#endif
  {}

  int receive(int port_num, const char *buffer, int len, uint64_t flags) {
#if SINGLE_STAGE
    (void)flags;
    (void)buffer;
    (void)len;
    (void)port_num;

    q_elem_t packet = 0;
#else
    static int pkt_id = 0;

    //if (this->do_swap() == 0)  // a swap took place
    //  swap_happened = true;

    auto packet = new_packet_ptr(port_num, pkt_id++, len,
                                 bm::PacketBuffer(len+512, buffer, len));

    BMELOG(packet_in, *packet);

    Parser *parser = this->get_parser("parser");
    parser->parse(packet.get());
#endif

#if QUEUE_TYPE == LOCKING
    (void) flags;
    input_buffer.push_front(std::move(packet));
#elif QUEUE_TYPE == SPSC
    input_buffer.push_front(std::move(packet), flags==0);
#elif QUEUE_TYPE == MULTI
    (void) flags;
    input_buffer.push_front(packet_count_in%num_queues, std::move(packet));
#endif

    packet_count_in++;

    return 0;
  }

  void start_and_return() {
    std::thread ti(&FastSwitch::ingress_thread, this);
    ti.detach();
    for (size_t i = 0; i < nb_egress_threads; i++) {
      std::thread te(&FastSwitch::egress_thread, this, i);
      te.detach();
    }
    std::thread tt(&FastSwitch::transmit_thread, this);
    tt.detach();
    std::thread ts(&FastSwitch::stats_thread, this);
    ts.detach();
  }

 private:
  void ingress_thread();
  void egress_thread(size_t i);
  void transmit_thread();

  void stats_thread();

 private:
  Queue input_buffer;
  Queue process_buffer;
  Queue output_buffer;
  //bool swap_happened{false};

  static constexpr size_t nb_egress_threads = 1u;

  // XXX variables for stat printing
  uint64_t packet_count_in{0};
  uint64_t packet_count_out{0};
  uint64_t avg_latency{0};
  uint64_t max_latency{0};

};

void FastSwitch::stats_thread() {
  uint64_t old_packet_count_in=0;
  uint64_t old_packet_count_out=0;

  uint64_t old_prod_notified=0;
  uint64_t old_cons_notified=0;

  int period = 200;
  while(true) {
    float delta_t_in = period*1000000.0/(packet_count_in-old_packet_count_in);
    float delta_t_out = period*1000000.0/(packet_count_out-old_packet_count_out);

#if QUEUE_TYPE == LOCKING
    float prod_notified = 0;
    float cons_notified = 0;
#elif QUEUE_TYPE == SPSC
    float prod_notified = input_buffer.prod_notified;
    float cons_notified = input_buffer.cons_notified;
#elif QUEUE_TYPE == MULTI
    float prod_notified = input_buffer.queues[0]->prod_notified;
    float cons_notified = input_buffer.queues[0]->cons_notified;
#endif

    printf("-- IN  ns_pkt  %5.1f pkt_s %1.3e prod_notified %6.0f\n"
           "   OUT ns_pkt  %5.1f pkt_s %1.3e cons_notified %6.0f\n"
           //"       ms_avg_lat %1.3e ms_max_lat %1.3e\n"
           ,delta_t_in, 1000000000.0/delta_t_in
           ,(prod_notified-old_prod_notified)*1000.0/period
           ,delta_t_out, 1000000000.0/delta_t_out
           ,(cons_notified-old_cons_notified)*1000.0/period
        );//(0.000001*avg_latency)/packet_count_out, 0.000001*max_latency);

    old_packet_count_in=packet_count_in;
    old_packet_count_out=packet_count_out;
#if QUEUE_TYPE == SPSC
    old_prod_notified = input_buffer.prod_notified;
    old_cons_notified = input_buffer.cons_notified;
#elif QUEUE_TYPE == MULTI
    old_prod_notified = input_buffer.queues[0]->prod_notified;
    old_cons_notified = input_buffer.queues[0]->cons_notified;
#endif

    std::this_thread::sleep_for(std::chrono::milliseconds(period));
  }
}

void FastSwitch::transmit_thread() {
  Deparser *deparser = this->get_deparser("deparser");
  while (1) {
    std::unique_ptr<Packet> packet;
#if QUEUE_TYPE == LOCKING || QUEUE_TYPE == SPSC
    output_buffer.pop_back(&packet);
#elif QUEUE_TYPE == MULTI
    size_t port;
    output_buffer.pop_back(0, &port, &packet);
#endif
    deparser->deparse(packet.get());
    BMELOG(packet_out, *packet);
    BMLOG_DEBUG_PKT(*packet, "Transmitting packet of size {} out of port {}",
                    packet->get_data_size(), packet->get_egress_port());
    auto ingress_ts = packet->get_ingress_ts();
    std::chrono::nanoseconds delta = std::chrono::system_clock::now() - ingress_ts;
    if (uint64_t(delta.count()) > max_latency) max_latency = delta.count();
    avg_latency+= delta.count();
    packet_count_out++;

    int egress_port = packet->get_egress_port();
    if (egress_port == 511) {
      BMLOG_DEBUG_PKT(*packet, "Dropping packet");
    } else {
      transmit_fn(egress_port, packet->data(), packet->get_data_size());
    }
  }
}

void FastSwitch::ingress_thread() {
#if !SINGLE_STAGE
  Pipeline *ingress_mau = this->get_pipeline("ingress");
#endif
  while (1) {
    std::unique_ptr<Packet> packet;
#if QUEUE_TYPE == LOCKING || QUEUE_TYPE == SPSC
    input_buffer.pop_back(&packet);
#elif QUEUE_TYPE == MULTI
    size_t port;
    input_buffer.pop_back(0, &port, &packet);
#endif

#if SINGLE_STAGE
    packet_count_out++;
#else
    int ingress_port = packet->get_ingress_port();
    (void) ingress_port;
    BMLOG_DEBUG_PKT(*packet, "Processing packet received on port {}",
                    ingress_port);

    ingress_mau->apply(packet.get());

#if QUEUE_TYPE == LOCKING || QUEUE_TYPE == SPSC
    process_buffer.push_front(std::move(packet));
#elif QUEUE_TYPE == MULTI
    process_buffer.push_front(0,std::move(packet));
#endif
#endif
  }
}

void FastSwitch::egress_thread(size_t i) {
  (void)i;

  Pipeline *egress_mau = this->get_pipeline("egress");
  PHV *phv;

  while (1) {
    q_elem_t packet;
#if QUEUE_TYPE == LOCKING || QUEUE_TYPE == SPSC
    process_buffer.pop_back(&packet);
#elif QUEUE_TYPE == MULTI
    size_t port;
    process_buffer.pop_back(i, &port, &packet);
#endif
    phv = packet->get_phv();
    int egress_port = phv->get_field("standard_metadata.egress_spec").get_int();
    BMLOG_DEBUG_PKT(*packet, "Egress port is {}", egress_port);

    packet->set_egress_port(egress_port);
    egress_mau->apply(packet.get());
#if QUEUE_TYPE == LOCKING || QUEUE_TYPE == SPSC
    output_buffer.push_front(std::move(packet));
#elif QUEUE_TYPE == MULTI
    output_buffer.push_front(0, std::move(packet));
#endif
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
