#include <vortex2.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <vector>

#include "common.h"
#include "pqc_config.h"

#define CHECK(_e) do { int _r = (_e); if (_r) { \
  std::fprintf(stderr, "FAIL %s:%d: '%s' -> %d\n", __FILE__, __LINE__, #_e, _r); \
  std::exit(-1); } } while (0)

static const char* mode_name(unsigned mode) {
  if (mode == LSU_PROBE_EMPTY) return "empty";
  if (mode == LSU_PROBE_CLEAN) return "clean";
  if (mode == LSU_PROBE_DIRTY) return "dirty";
  if (mode == LSU_PROBE_BARRIER) return "barrier";
  return "barrier_dirty";
}

static bool parse_mode(const char* text, uint32_t* mode) {
  if (std::strcmp(text, "empty") == 0) {
    *mode = LSU_PROBE_EMPTY;
    return true;
  }
  if (std::strcmp(text, "clean") == 0) {
    *mode = LSU_PROBE_CLEAN;
    return true;
  }
  if (std::strcmp(text, "dirty") == 0) {
    *mode = LSU_PROBE_DIRTY;
    return true;
  }
  if (std::strcmp(text, "barrier") == 0) {
    *mode = LSU_PROBE_BARRIER;
    return true;
  }
  if (std::strcmp(text, "barrier_dirty") == 0) {
    *mode = LSU_PROBE_BARRIER_DIRTY;
    return true;
  }
  return false;
}

static bool mode_writes(unsigned mode) {
  return mode == LSU_PROBE_DIRTY || mode == LSU_PROBE_BARRIER_DIRTY;
}

static uint32_t expected_checksum(unsigned request, unsigned lane,
                                  unsigned rounds) {
  uint32_t value = lsu_probe_seed(request, lane);
  for (unsigned round = 0; round < rounds; ++round) {
    value = lsu_probe_step(value, round);
  }
  return value;
}

int main(int argc, char** argv) {
  const char* kernel_file = "kernel.vxbin";
  uint32_t requests = 1;
  uint32_t lanes = 32;
  uint32_t rounds = 32;
  uint32_t mode = LSU_PROBE_CLEAN;

  int option;
  while ((option = getopt(argc, argv, "k:b:t:r:m:h")) != -1) {
    if (option == 'k') {
      kernel_file = optarg;
    } else if (option == 'b') {
      requests = (uint32_t)std::atoi(optarg);
    } else if (option == 't') {
      lanes = (uint32_t)std::atoi(optarg);
    } else if (option == 'r') {
      rounds = (uint32_t)std::atoi(optarg);
    } else if (option == 'm') {
      if (!parse_mode(optarg, &mode)) {
        std::fprintf(stderr,
                     "FAIL: -m must be empty, clean, dirty, barrier, or "
                     "barrier_dirty\n");
        return -1;
      }
    } else {
      std::cout << "Usage: [-k kernel] [-b requests] [-t lanes] "
                   "[-r rounds] "
                   "[-m empty|clean|dirty|barrier|barrier_dirty]\n";
      return option == 'h' ? 0 : -1;
    }
  }
  if (requests < 1 || requests > LSU_PROBE_MAX_REQUESTS) {
    std::fprintf(stderr, "FAIL: -b must be from 1 to %d\n",
                 LSU_PROBE_MAX_REQUESTS);
    return -1;
  }
  if (lanes < 1 || lanes > LSU_PROBE_MAX_LANES || (lanes & (lanes - 1u))) {
    std::fprintf(stderr, "FAIL: -t must be a power of two from 1 to %d\n",
                 LSU_PROBE_MAX_LANES);
    return -1;
  }
  if (rounds == 0) {
    std::fprintf(stderr, "FAIL: -r must be positive\n");
    return -1;
  }

  vx_device_h device = nullptr;
  CHECK(vx_device_open(0, &device));
  const pqc::config cfg = pqc::print_config(device, requests, lanes);
  if (cfg.cores != 1) {
    std::fprintf(stderr, "FAIL: batch timestamps require one shared core cycle counter\n");
    vx_device_release(device);
    return -1;
  }
  if (pqc::require_slots(cfg, requests, lanes) != 0) {
    vx_device_release(device);
    return -1;
  }

  vx_queue_info_t queue_info = {
      sizeof(queue_info), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0};
  vx_queue_h queue = nullptr;
  CHECK(vx_queue_create(device, &queue_info, &queue));

  const size_t scratch_words = requests * LSU_PROBE_WORDS_PER_REQUEST;
  const size_t scratch_bytes = scratch_words * sizeof(uint32_t);
  const size_t results_bytes = requests * sizeof(lsu_probe_result_t);
  vx_buffer_h scratch_buffer = nullptr;
  vx_buffer_h results_buffer = nullptr;
  CHECK(vx_buffer_create(device, scratch_bytes, VX_MEM_READ_WRITE,
                         &scratch_buffer));
  CHECK(vx_buffer_create(device, results_bytes, VX_MEM_READ_WRITE,
                         &results_buffer));

  kernel_arg_t arg{};
  CHECK(vx_buffer_address(scratch_buffer, &arg.scratch_addr));
  CHECK(vx_buffer_address(results_buffer, &arg.results_addr));
  arg.requests = requests;
  arg.lanes = lanes;
  arg.rounds = rounds;
  arg.mode = mode;

  std::vector<uint32_t> scratch(scratch_words, LSU_PROBE_POISON);
  std::vector<lsu_probe_result_t> results(requests);
  for (auto& result : results) {
    result.start = UINT64_MAX;
    result.end = UINT64_MAX;
    result.checksum = LSU_PROBE_POISON;
    result.completed = LSU_PROBE_POISON;
  }
  CHECK(vx_enqueue_write(queue, scratch_buffer, 0, scratch.data(), scratch_bytes,
                         0, nullptr, nullptr));
  CHECK(vx_enqueue_write(queue, results_buffer, 0, results.data(), results_bytes,
                         0, nullptr, nullptr));

  vx_module_h module = nullptr;
  vx_kernel_h kernel = nullptr;
  CHECK(vx_module_load_file(device, kernel_file, &module));
  CHECK(vx_module_get_kernel(module, "main", &kernel));

  vx_launch_info_t launch{};
  launch.struct_size = sizeof(launch);
  launch.kernel = kernel;
  launch.args_host = &arg;
  launch.args_size = sizeof(arg);
  launch.ndim = 1;
  launch.grid_dim[0] = requests;
  launch.block_dim[0] = lanes;
  vx_event_h launched = nullptr;
  CHECK(vx_enqueue_launch(queue, &launch, 0, nullptr, &launched));
  CHECK(vx_event_wait_value(launched, 1, VX_TIMEOUT_INFINITE));

  auto read = [&](void* dst, vx_buffer_h buffer, size_t bytes) {
    vx_event_h event = nullptr;
    CHECK(vx_enqueue_read(queue, dst, buffer, 0, bytes, 1, &launched, &event));
    CHECK(vx_event_wait_value(event, 1, VX_TIMEOUT_INFINITE));
    vx_event_release(event);
  };
  read(scratch.data(), scratch_buffer, scratch_bytes);
  read(results.data(), results_buffer, results_bytes);

  unsigned errors = 0;
  uint64_t batch_start = UINT64_MAX;
  uint64_t batch_end = 0;
  uint64_t cycle_sum = 0;
  uint64_t cycle_min = UINT64_MAX;
  uint64_t cycle_max = 0;
  for (unsigned request = 0; request < requests; ++request) {
    const auto& result = results[request];
    unsigned request_bad = 0;
    const uint32_t checksum = expected_checksum(request, 0, rounds);
    request_bad += result.completed != LSU_PROBE_DONE;
    request_bad += result.start == UINT64_MAX || result.end <= result.start;
    request_bad += result.checksum != checksum;
    const size_t base = request * LSU_PROBE_WORDS_PER_REQUEST;
    for (unsigned index = 0; index < LSU_PROBE_WORDS_PER_REQUEST; ++index) {
      uint32_t expected = LSU_PROBE_POISON;
      if (mode_writes(mode) &&
          index < lanes * LSU_PROBE_STORES_PER_LANE) {
        const unsigned slot = index / lanes;
        const unsigned lane = index % lanes;
        expected = lsu_probe_store_value(
            expected_checksum(request, lane, rounds), slot);
      }
      request_bad += scratch[base + index] != expected;
    }

    const uint64_t cycles = result.end > result.start
                          ? result.end - result.start : 0;
    if (result.start < batch_start) batch_start = result.start;
    if (result.end > batch_end) batch_end = result.end;
    cycle_sum += cycles;
    if (cycles < cycle_min) cycle_min = cycles;
    if (cycles > cycle_max) cycle_max = cycles;
    errors += request_bad;
    std::printf("LSU_PROBE request=%u mode=%s M=%u L=%u rounds=%u "
                "start=%llu end=%llu cycles=%llu checksum=0x%08x bad=%u\n",
                request, mode_name(mode), requests, lanes, rounds,
                (unsigned long long)result.start,
                (unsigned long long)result.end,
                (unsigned long long)cycles, result.checksum, request_bad);
  }

  const uint64_t makespan = batch_end > batch_start ? batch_end - batch_start : 0;
  std::printf("LSU_PROBE_BATCH mode=%s M=%u L=%u rounds=%u start=%llu end=%llu "
              "makespan=%llu min=%llu max=%llu sum=%llu bad=%u\n",
              mode_name(mode), requests, lanes, rounds,
              (unsigned long long)batch_start, (unsigned long long)batch_end,
              (unsigned long long)makespan, (unsigned long long)cycle_min,
              (unsigned long long)cycle_max, (unsigned long long)cycle_sum, errors);
  vx_device_dump_perf(device, stdout);

  vx_event_release(launched);
  vx_buffer_release(scratch_buffer);
  vx_buffer_release(results_buffer);
  vx_kernel_release(kernel);
  vx_module_release(module);
  vx_queue_release(queue);
  vx_device_release(device);
  std::printf("%s\n", errors ? "FAILED!" : "PASSED!");
  return errors ? 1 : 0;
}
