// Copyright [2016] <y.iozzelli@gmail.com>

#include <string>
#include <functional>

typedef std::function<void(int, const char*, int, void*)>
        packet_handler_t;

class NetmapManager {
 public:
  NetmapManager();
  ~NetmapManager();

  void set_packet_handler(packet_handler_t ph, void* cookie);
  bool start_receive();
  void stop_receive();
  bool send(int port, const char* buf, int len);
  void flush_out();
  bool add_interface(std::string ifname, int port);
  bool remove_interface(int port_num);
};

NetmapManager::NetmapManager() {}
NetmapManager::~NetmapManager() {}

void NetmapManager::set_packet_handler(packet_handler_t ph, void* cookie) {
  (void) ph;
  (void) cookie;
}

bool NetmapManager::start_receive() {return false;}
void NetmapManager::stop_receive() {}

bool NetmapManager::send(int port, const char* buf, int len) {
  (void) port;
  (void) buf;
  (void) len;
  return false;
}

void NetmapManager::flush_out() {}

bool NetmapManager::add_interface(std::string ifname, int port) {
  (void) ifname;
  (void) port;
  return false;
}
bool NetmapManager::remove_interface(int port_num) {
  (void) port_num;
  return false;
}
