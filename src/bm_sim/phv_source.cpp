
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

#include <bm/bm_sim/phv_source.h>

#include <vector>
#include <mutex>
#include <iostream>
#include <atomic>
#include <map>

namespace bm {

class PHVSourceContextPools : public PHVSourceIface {
 public:
  explicit PHVSourceContextPools(size_t size)
      : phv_pools(size) {
      for (auto& phv_pool : phv_pools) {
        tls.emplace(&phv_pool, 0);
      }
  }

 private:
  class PHVPool {
   public:
    void set_phv_factory(const PHVFactory *factory) {
      std::unique_lock<std::mutex> lock(mutex);
      assert(count == 0);
      phv_factory = factory;
      global_phvs.clear(); // XXX should do explicit delete
      std::vector<PHV*>& local_phvs = tls[this];
      local_phvs.clear();
    }

    std::unique_ptr<PHV> get() {
      count++;
      std::vector<PHV*>& local_phvs = tls[this];
      if (local_phvs.size() == 0) {
        std::unique_lock<std::mutex> lock(mutex);
        if (global_phvs.size() == 0) {
          lock.unlock();
          return phv_factory->create();
        }
        local_phvs.insert(local_phvs.end(),global_phvs.begin(), global_phvs.end());
        global_phvs.clear();
        lock.unlock();
      }
      std::unique_ptr<PHV> phv = std::unique_ptr<PHV>(local_phvs.back());
      local_phvs.pop_back();
      return phv;
    }

    void release(std::unique_ptr<PHV> phv) {
      count--;
      std::vector<PHV*>& local_phvs = tls[this];
      local_phvs.push_back(phv.release());
      if (local_phvs.size() >= max_local_size) {
        std::unique_lock<std::mutex> lock(mutex);
        global_phvs.insert(global_phvs.end(), local_phvs.begin(), local_phvs.end());
        local_phvs.clear();
      }
    }

    size_t phvs_in_use() {
      std::unique_lock<std::mutex> lock(mutex);
      return count;
    }

   private:
    mutable std::mutex mutex{};
    std::vector<PHV*> global_phvs{};
    const PHVFactory *phv_factory{nullptr};
    std::atomic<size_t> count{0};
    size_t max_local_size{1024};

  };

  std::unique_ptr<PHV> get_(size_t cxt) override {
    return phv_pools.at(cxt).get();
  }

  void release_(size_t cxt, std::unique_ptr<PHV> phv) override {
    return phv_pools.at(cxt).release(std::move(phv));
  }

  void set_phv_factory_(size_t cxt, const PHVFactory *factory) override {
    phv_pools.at(cxt).set_phv_factory(factory);
  }

  size_t phvs_in_use_(size_t cxt) override {
    return phv_pools.at(cxt).phvs_in_use();
  }

  std::vector<PHVPool> phv_pools;
  /* This map is used to store the thread_local vector of available phvs
   * Because thread_local variables can only be static, we need the map to link
   * every PHVPool instance to a local phv vector.
   * The goal is to limit the use of locks in order to achieve better
   * performance.
   */
  static thread_local std::map<PHVPool*, std::vector<PHV*>> tls;
};

thread_local std::map<PHVSourceContextPools::PHVPool*, std::vector<PHV*>> PHVSourceContextPools::tls;

std::unique_ptr<PHVSourceIface>
PHVSourceIface::make_phv_source(size_t size) {
  return std::unique_ptr<PHVSourceContextPools>(
      new PHVSourceContextPools(size));
}

}  // namespace bm
