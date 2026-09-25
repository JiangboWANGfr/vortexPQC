#include <vortex2.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <vector>
#include "common.h"
#include "pqc_config.h"

#define CHECK(_e) do { int _r = (_e); if (_r) { \
  std::fprintf(stderr, "FAIL %s:%d: '%s' -> %d\n", __FILE__, __LINE__, #_e, _r); \
  std::exit(-1); } } while (0)

int main(int argc, char** argv) {
#if defined(PQC_NTT_ISE)
  const char* implementation = "hardware";
#else
  const char* implementation = "software";
#endif
  const char* kf = "kernel.vxbin";
  uint32_t lanes = 32, requests = 1;
  int c;
  while ((c = getopt(argc, argv, "k:t:b:h")) != -1) {
    if (c == 'k') {
      kf = optarg;
    } else if (c == 't') {
      lanes = (uint32_t)std::atoi(optarg);
    } else if (c == 'b') {
      requests = (uint32_t)std::atoi(optarg);
    } else {
      std::cout << "Usage: [-k kernel] [-t lanes] [-b requests]\n";
      return c == 'h' ? 0 : -1;
    }
  }
  if (lanes < 1 || lanes > 32 || (lanes & (lanes - 1u))) {
    std::fprintf(stderr, "FAIL: -t must be a power of two from 1 to 32\n");
    return -1;
  }
  if (requests < 1 || requests > NTT_MAX_REQUESTS) {
    std::fprintf(stderr, "FAIL: -b must be from 1 to %d\n", NTT_MAX_REQUESTS);
    return -1;
  }
  if (lanes != 32) {
    std::fprintf(stderr, "FAIL: W32 NTT requires -t 32\n");
    return -1;
  }

  vx_device_h dev = nullptr;
  CHECK(vx_device_open(0, &dev));
  const pqc::config cfg = pqc::print_config(dev, requests, lanes);
  if (cfg.cores != 1) {
    std::fprintf(stderr, "FAIL: batch timestamps require one shared core cycle counter\n");
    vx_device_release(dev);
    return -1;
  }
  if (pqc::require_slots(cfg, requests, lanes) != 0) {
    vx_device_release(dev);
    return -1;
  }
  vx_queue_info_t qi = {sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0};
  vx_queue_h q = nullptr;
  CHECK(vx_queue_create(dev, &qi, &q));

  const size_t slots = NTT_DIRECTIONS * NTT_CASES * requests;
  const size_t poly_bytes = slots * NTT_N * sizeof(ntt_coeff_t);
  vx_buffer_h pb = nullptr, rb = nullptr, results_buffer = nullptr;
  CHECK(vx_buffer_create(dev, poly_bytes, VX_MEM_READ_WRITE, &pb));
  CHECK(vx_buffer_create(dev, poly_bytes, VX_MEM_WRITE, &rb));
  CHECK(vx_buffer_create(dev, slots * sizeof(ntt_result_t), VX_MEM_READ_WRITE, &results_buffer));

  kernel_arg_t arg{};
  CHECK(vx_buffer_address(pb, &arg.poly_addr));
  CHECK(vx_buffer_address(rb, &arg.ref_addr));
  CHECK(vx_buffer_address(results_buffer, &arg.results_addr));
  arg.lanes = lanes;
  arg.requests = requests;

  std::vector<ntt_result_t> results(slots);
  for (auto& result : results) {
    result.mismatches = 0xDEADBEEFu;
  }
  CHECK(vx_enqueue_write(q, results_buffer, 0, results.data(),
                        results.size() * sizeof(ntt_result_t), 0, nullptr, nullptr));

  vx_module_h mod = nullptr;
  vx_kernel_h kern = nullptr;
  CHECK(vx_module_load_file(dev, kf, &mod));
  CHECK(vx_module_get_kernel(mod, "main", &kern));

  vx_launch_info_t li{};
  li.struct_size = sizeof(li);
  li.kernel = kern;
  li.args_host = &arg;
  li.args_size = sizeof(arg);
  li.ndim = 1;
  li.grid_dim[0] = requests;
  for (arg.inverse = 0; arg.inverse < NTT_DIRECTIONS; ++arg.inverse) {
    for (arg.sample = 0; arg.sample < NTT_CASES; ++arg.sample) {
      // Separate launches prevent scalar reference work overlapping the batch.
      for (arg.reference = 0; arg.reference < 2; ++arg.reference) {
        li.block_dim[0] = arg.reference ? 1 : lanes;
        vx_event_h launch = nullptr;
        CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &launch));
        CHECK(vx_event_wait_value(launch, 1, VX_TIMEOUT_INFINITE));
        vx_event_release(launch);
      }
    }
  }

  auto read = [&](void* dst, vx_buffer_h buffer, size_t bytes) {
    vx_event_h event = nullptr;
    CHECK(vx_enqueue_read(q, dst, buffer, 0, bytes, 0, nullptr, &event));
    CHECK(vx_event_wait_value(event, 1, VX_TIMEOUT_INFINITE));
    vx_event_release(event);
  };
  std::vector<ntt_coeff_t> h_out(slots * NTT_N), h_ref(slots * NTT_N);
  read(h_out.data(), pb, poly_bytes);
  read(h_ref.data(), rb, poly_bytes);
  read(results.data(), results_buffer, results.size() * sizeof(ntt_result_t));

  int errors = 0;
  for (unsigned inverse = 0; inverse < NTT_DIRECTIONS; ++inverse) {
    const char* direction = inverse ? "inverse" : "forward";
    for (unsigned sample = 0; sample < NTT_CASES; ++sample) {
      uint64_t coop_start = UINT64_MAX, coop_end = 0;
      uint64_t ref_start = UINT64_MAX, ref_end = 0;
      uint32_t total_bad = 0;
      for (unsigned req = 0; req < requests; ++req) {
        const size_t slot = (inverse * NTT_CASES + sample) * requests + req;
        const auto& result = results[slot];
        uint32_t bad = 0;
        for (unsigned i = 0; i < NTT_N; ++i) {
          const size_t index = slot * NTT_N + i;
          if (h_out[index] != h_ref[index]) {
            if (bad < 4) {
              std::printf("*** %s sample=%u request=%u [%u] coop=%d ref=%d\n",
                          direction, sample, req, i, h_out[index], h_ref[index]);
            }
            ++bad;
          }
        }
        total_bad += bad;
        if (bad || result.mismatches != bad) {
          std::printf("*** %s sample=%u request=%u mismatch=%u device_mismatch=%u\n",
                      direction, sample, req, bad, result.mismatches);
          ++errors;
        }
        if (result.coop_end <= result.coop_start || result.ref_end <= result.ref_start) {
          std::printf("*** %s sample=%u request=%u invalid timestamps\n", direction, sample, req);
          ++errors;
        }
        if (!result.stack_span || result.stack_peak >= result.stack_span) {
          std::printf("*** %s sample=%u request=%u stack=%u/%u\n",
                      direction, sample, req, result.stack_peak, result.stack_span);
          ++errors;
        }
        coop_start = std::min(coop_start, result.coop_start);
        coop_end = std::max(coop_end, result.coop_end);
        ref_start = std::min(ref_start, result.ref_start);
        ref_end = std::max(ref_end, result.ref_end);
        if (sample == 0) {
          std::printf("NTT_REQUEST implementation=%s direction=%s M=%u L=%u request=%u "
                      "coop_start=%llu coop_end=%llu "
                      "ref_start=%llu ref_end=%llu stack=%u/%u mismatch=%u\n",
                      implementation, direction, requests, lanes, req,
                      (unsigned long long)result.coop_start, (unsigned long long)result.coop_end,
                      (unsigned long long)result.ref_start, (unsigned long long)result.ref_end,
                      result.stack_peak, result.stack_span, bad);
        }
      }
      const uint64_t coop = coop_end > coop_start ? coop_end - coop_start : 0;
      const uint64_t ref = ref_end > ref_start ? ref_end - ref_start : 0;
      std::printf("NTT_BATCH implementation=%s direction=%s M=%u L=%u sample=%u "
                  "coop=%llu ref=%llu "
                  "speedup=%.3f checked=%u mismatch=%u\n",
                  implementation, direction, requests, lanes, sample,
                  (unsigned long long)coop, (unsigned long long)ref,
                  coop ? (double)ref / (double)coop : 0.0, requests * NTT_N, total_bad);
      if (!inverse && !sample && requests == 1) {
        const auto& result = results[0];
        std::printf("NTT implementation=%s W=%u  coop=%llu  ref=%llu  speedup=%.3f  "
                    "stack=%u/%u  mismatch=%u\n",
                    implementation, lanes, (unsigned long long)coop, (unsigned long long)ref,
                    coop ? (double)ref / (double)coop : 0.0,
                    result.stack_peak, result.stack_span, total_bad);
      }
    }
  }
  vx_device_dump_perf(dev, stdout);

  vx_buffer_release(pb);
  vx_buffer_release(rb);
  vx_buffer_release(results_buffer);
  vx_kernel_release(kern);
  vx_module_release(mod);
  vx_queue_release(q);
  vx_device_release(dev);
  std::cout << (errors ? "FAILED!" : "PASSED!") << std::endl;
  return errors ? -1 : 0;
}
