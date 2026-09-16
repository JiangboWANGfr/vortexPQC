#include <cstdlib>
#include <iostream>
#include "cache.h"
#include "mem_block_pool.h"

using namespace vortex;

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void tick(uint32_t count = 1) {
  for (uint32_t i = 0; i < count; ++i) {
    SimPlatform::instance().tick();
  }
}

Cache::Ptr make_cache(uint8_t latency, bool write_back = true) {
  Cache::Config config{};
  config.C = 10;
  config.L = log2ceil(VX_CFG_MEM_BLOCK_SIZE);
  config.S = config.L;
  config.W = 2;
  config.A = 1;
  config.addr_width = 32;
  config.num_inputs = 1;
  config.mem_ports = 1;
  config.write_back = write_back;
  config.write_reponse = write_back;
  config.mshr_size = 8;
  config.latency = latency;
  config.repl_policy = Cache::FIFO;
  config.is_llc = true;
  auto cache = Cache::Create("cache_forward", config);
  SimPlatform::instance().reset();
  tick(64);
  return cache;
}

void request(const Cache::Ptr& cache, uint64_t tag, bool write = false) {
  MemReq req;
  req.addr = 0x1000;
  req.tag = tag;
  req.op = write ? MemOp::ST : MemOp::LD;
  req.byteen = 1;
  if (write) {
    req.data = make_mem_block();
    req.data->fill(0x77);
  }
  check(cache->core_req_in[0].try_send(req), "request input full");
}

MemReq wait_miss(const Cache::Ptr& cache) {
  for (uint32_t i = 0; i < 100; ++i) {
    tick();
    if (!cache->mem_req_out[0].empty()) {
      auto req = cache->mem_req_out[0].peek();
      cache->mem_req_out[0].pop();
      return req;
    }
  }
  check(false, "fill request timeout");
  return {};
}

void fill(const Cache::Ptr& cache, const MemReq& req) {
  auto data = make_mem_block();
  data->fill(0x22);
  cache->mem_rsp_in[0].send(MemRsp(req.tag, req.hart_id, req.uuid, data));
}

MemRsp wait_response(const Cache::Ptr& cache) {
  for (uint32_t i = 0; i < 100; ++i) {
    tick();
    if (!cache->core_rsp_out[0].empty()) {
      auto rsp = cache->core_rsp_out[0].peek();
      cache->core_rsp_out[0].pop();
      return rsp;
    }
  }
  check(false, "core response timeout");
  return {};
}

uint64_t miss_latency(uint8_t latency) {
  auto cache = make_cache(latency);
  request(cache, 1);
  auto miss = wait_miss(cache);
  auto start = SimPlatform::instance().cycles();
  fill(cache, miss);
  auto rsp = wait_response(cache);
  check(rsp.tag == 1 && rsp.data && rsp.data->at(0) == 0x22,
        "incorrect forwarded read");
  return SimPlatform::instance().cycles() - start;
}

void check_chain(uint8_t latency, bool backpressure) {
  auto cache = make_cache(latency);
  request(cache, 1);
  auto miss = wait_miss(cache);
  request(cache, 2, true);
  tick();
  request(cache, 3);
  tick(16);
  check(cache->mem_req_out[0].empty(), "coalesced chain issued another fill");
  fill(cache, miss);
  if (backpressure) {
    tick(20);
  }
  auto first = wait_response(cache);
  auto store = wait_response(cache);
  auto last = wait_response(cache);
  check(first.tag == 1 && store.tag == 2 && last.tag == 3,
        "read/store/read responses reordered");
  check(first.data && first.data->at(0) == 0x22,
        "store changed an older forwarded read");
  check(last.data && last.data->at(0) == 0x77,
        "younger read missed the chained store");
  check(cache->mem_req_out[0].empty(), "replay issued an unexpected fill");
}

void check_writethrough(uint8_t latency, uint32_t delay) {
  auto cache = make_cache(latency, false);
  request(cache, 1);
  auto miss = wait_miss(cache);
  request(cache, 2, true);
  tick(delay);
  fill(cache, miss);
  auto first = wait_response(cache);
  check(first.tag == 1 && first.data && first.data->at(0) == 0x22,
        "write-through store overtook the older read");
  tick(32);
  check(!cache->mem_req_out[0].empty(), "missing write-through store");
  auto store = cache->mem_req_out[0].peek();
  check(store.is_write() && store.data && store.data->at(0) == 0x77,
        "incorrect write-through store");
  cache->mem_req_out[0].pop();
  request(cache, 3);
  auto last = wait_response(cache);
  check(last.tag == 3 && last.data && last.data->at(0) == 0x77,
        "fill lost a write-through store");
}

} // namespace

int main() {
  auto short_latency = miss_latency(2);
  auto long_latency = miss_latency(4);
  std::cout << "fill response latency: array2=" << short_latency
            << ", array4=" << long_latency << '\n';
  check(short_latency == long_latency, "fill forwarding waits for array installation");
  for (uint8_t latency : {2, 4}) {
    check_chain(latency, false);
    check_chain(latency, true);
    for (uint32_t delay = 0; delay < 8; ++delay) {
      check_writethrough(latency, delay);
    }
  }
  std::cout << "PASSED!\n";
}
