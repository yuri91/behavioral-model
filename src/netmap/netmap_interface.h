// Copyright [2016] <y.iozzelli@gmail.com>

#ifndef NETMAP_NETMAP_INTERFACE_H_
#define NETMAP_NETMAP_INTERFACE_H_

extern "C" {
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>
}

#include <sys/ioctl.h>

#include <iostream>
#include <string>

// This class is a simple wrapper around the use of a network interface in
// netmap mode
class NetmapInterface {
 public:
  explicit NetmapInterface(std::string name) {
    if (name.substr(4) != "vale") {
      name = "netmap:"+name;
    }
    d = nm_open(name.c_str(), NULL, 0, 0);
    if (d == NULL) {
        std::cerr << "can't open interface "
                  << name <<" with netmap" << std::endl;
        exit(1);  // TODO(yuri): exception?
    }
  }
  ~NetmapInterface() {
    nm_close(d);
  }
  int fd() {
    return d->fd;
  }

  void sync_tx() {
    ioctl(d->fd, NIOCTXSYNC);
  }
  void sync_rx() {
    ioctl(d->fd, NIOCRXSYNC);
  }

  bool pending_tx() {
    for (int i = d->first_tx_ring; i <= d->last_tx_ring; i++) {
      if  (nm_tx_pending(NETMAP_TXRING(d->nifp, i))) {
        return true;
      }
    }
    return false;
  }

  int inject(const char* buf, int len) {
    return nm_inject(d, buf, len);
  }
  int nextpkt(char** buf) {
    struct nm_pkthdr h{};
    *buf = reinterpret_cast<char*>(nm_nextpkt(d, &h));
    return h.len;
  }

 private:
  struct nm_desc *d;
};

#endif  // NETMAP_NETMAP_INTERFACE_H_