#include <vortex2.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include "common.h"
#include "pqc_config.h"

#define CHECK(expr) do { \
  vx_result_t status = (expr); \
  if (status != VX_SUCCESS) { \
    std::fprintf(stderr, "FAIL: %s: %s\n", #expr, vx_result_string(status)); \
    std::exit(1); \
  } \
} while (0)

int main(int argc, char** argv) {
  const char* kernel_file = "kernel.vxbin";
  unsigned requests = 1;
  int option;
  while ((option = getopt(argc, argv, "b:k:h")) != -1) {
    if (option == 'b') {
      requests = (unsigned)std::strtoul(optarg, nullptr, 0);
    } else if (option == 'k') {
      kernel_file = optarg;
    } else {
      std::printf("Usage: [-b requests] [-k kernel]\n");
      return option == 'h' ? 0 : 1;
    }
  }
  if (requests < 1 || requests > ARITH_MAX_REQUESTS) {
    std::fprintf(stderr, "FAIL: -b must be from 1 to %u\n", ARITH_MAX_REQUESTS);
    return 1;
  }

  vx_device_h device = nullptr;
  CHECK(vx_device_open(0, &device));
  const auto cfg = pqc::print_config(device, requests, 32);
  if (cfg.cores != 1 || pqc::require_slots(cfg, requests, 32)) {
    std::fprintf(stderr, "FAIL: timing requires one core with all requests resident\n");
    vx_device_release(device);
    return 1;
  }
  vx_queue_info_t qi = {sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0};
  vx_queue_h queue = nullptr;
  CHECK(vx_queue_create(device, &qi, &queue));

  const size_t slots = ARITH_OP_COUNT * ARITH_CASES * ARITH_MODE_COUNT * requests;
  std::vector<int16_t> output(slots * ARITH_N, (int16_t)0xdead);
  std::vector<arith_result_t> results(slots);
  vx_buffer_h output_buffer = nullptr, results_buffer = nullptr, exhaustive_buffer = nullptr;
  CHECK(vx_buffer_create(device, output.size() * sizeof(output[0]), VX_MEM_READ_WRITE,
                         &output_buffer));
  CHECK(vx_buffer_create(device, results.size() * sizeof(results[0]), VX_MEM_READ_WRITE,
                         &results_buffer));
  CHECK(vx_buffer_create(device, ARITH_EXHAUSTIVE_COUNT * sizeof(int16_t), VX_MEM_READ_WRITE,
                         &exhaustive_buffer));
  CHECK(vx_enqueue_write(queue, output_buffer, 0, output.data(), output.size() * sizeof(output[0]),
                         0, nullptr, nullptr));
  CHECK(vx_enqueue_write(queue, results_buffer, 0, results.data(), results.size() * sizeof(results[0]),
                         0, nullptr, nullptr));

  kernel_arg_t arg{};
  arg.requests = requests;
  CHECK(vx_buffer_address(output_buffer, &arg.output_addr));
  CHECK(vx_buffer_address(results_buffer, &arg.results_addr));
  vx_module_h module = nullptr;
  vx_kernel_h kernel = nullptr;
  CHECK(vx_module_load_file(device, kernel_file, &module));
  CHECK(vx_module_get_kernel(module, "main", &kernel));
  vx_launch_info_t li{};
  li.struct_size = sizeof(li);
  li.kernel = kernel;
  li.args_host = &arg;
  li.args_size = sizeof(arg);
  li.ndim = 1;
  li.grid_dim[0] = requests;
  auto launch = [&]() {
    li.block_dim[0] = arg.mode == ARITH_SCALAR ? 1 : 32;
    vx_event_h event = nullptr;
    CHECK(vx_enqueue_launch(queue, &li, 0, nullptr, &event));
    CHECK(vx_event_wait_value(event, 1, VX_TIMEOUT_INFINITE));
    vx_event_release(event);
  };
  for (arg.operation = 0; arg.operation < ARITH_OP_COUNT; ++arg.operation) {
    for (int sample = -1; sample < ARITH_CASES; ++sample) {
      arg.sample = sample < 0 ? 0 : (unsigned)sample;
      for (unsigned order = 0; order < ARITH_MODE_COUNT; ++order) {
        arg.mode = (order + arg.sample) % ARITH_MODE_COUNT;
        launch();
      }
    }
  }
  auto read = [&](void* dst, vx_buffer_h buffer, size_t bytes) {
    vx_event_h event = nullptr;
    CHECK(vx_enqueue_read(queue, dst, buffer, 0, bytes, 0, nullptr, &event));
    CHECK(vx_event_wait_value(event, 1, VX_TIMEOUT_INFINITE));
    vx_event_release(event);
  };
  read(output.data(), output_buffer, output.size() * sizeof(output[0]));
  read(results.data(), results_buffer, results.size() * sizeof(results[0]));

  const char* operations[] = {"mulcache", "basemul", "poly_reduce"};
  const char* modes[] = {"scalar", "w32_c", "w32_ise"};
  unsigned errors = 0;
  std::printf("ARITH_CASES: 0=random 1=positive 2=negative 3=alternating "
              "4=edges_sparse_k 5=positive_cache_extreme 6=negative_cache_extreme "
              "7=random_cache warmup=1\n");
  for (unsigned operation = 0; operation < ARITH_OP_COUNT; ++operation) {
    for (unsigned sample = 0; sample < ARITH_CASES; ++sample) {
      for (unsigned mode = 0; mode < ARITH_MODE_COUNT; ++mode) {
        uint64_t start = UINT64_MAX, end = 0;
        unsigned mismatches = 0;
        const size_t group = (operation * ARITH_CASES + sample) * ARITH_MODE_COUNT;
        for (unsigned req = 0; req < requests; ++req) {
          const size_t slot = (group + mode) * requests + req;
          const size_t reference = group * requests + req;
          const auto& result = results[slot];
          if (result.end <= result.start) {
            std::printf("*** invalid timestamp op=%s mode=%s sample=%u request=%u\n",
                        operations[operation], modes[mode], sample, req);
            ++errors;
          }
          start = std::min(start, result.start);
          end = std::max(end, result.end);
          for (unsigned i = 0; i < ARITH_N; ++i) {
            const int16_t actual = output[slot * ARITH_N + i];
            const int16_t expected = operation == ARITH_MULCACHE && i >= ARITH_N / 2
                ? 0x5a5a : output[reference * ARITH_N + i];
            if (actual != expected || (operation == ARITH_REDUCE && (actual < 0 || actual >= 3329))) {
              if (mismatches < 4) {
                std::printf("*** op=%s mode=%s sample=%u request=%u [%u] got=%d expected=%d\n",
                            operations[operation], modes[mode], sample, req, i, actual, expected);
              }
              ++mismatches;
            }
          }
        }
        errors += mismatches;
        std::printf("ARITH_BATCH op=%s mode=%s M=%u sample=%u cycles=%llu checked=%u mismatch=%u\n",
                    operations[operation], modes[mode], requests, sample,
                    (unsigned long long)(end > start ? end - start : 0),
                    requests * (operation == ARITH_MULCACHE ? ARITH_N / 2 : ARITH_N), mismatches);
      }
    }
  }

  std::vector<int16_t> exhaustive(ARITH_EXHAUSTIVE_COUNT, -1);
  CHECK(vx_enqueue_write(queue, exhaustive_buffer, 0, exhaustive.data(),
                         exhaustive.size() * sizeof(exhaustive[0]), 0, nullptr, nullptr));
  CHECK(vx_buffer_address(exhaustive_buffer, &arg.output_addr));
  arg.exhaustive = 1;
  arg.mode = ARITH_W32_C;
  launch();
  read(exhaustive.data(), exhaustive_buffer, exhaustive.size() * sizeof(exhaustive[0]));
  unsigned exhaustive_bad = 0;
  for (unsigned i = 0; i < ARITH_EXHAUSTIVE_COUNT; ++i) {
    const int input = (int)i - 32768;
    const int expected = (input % 3329 + 3329) % 3329;
    if (exhaustive[i] != expected) {
      if (exhaustive_bad < 4) {
        std::printf("*** exhaustive input=%d got=%d expected=%d\n", input, exhaustive[i], expected);
      }
      ++exhaustive_bad;
    }
  }
  errors += exhaustive_bad;
  std::printf("ARITH_EXHAUSTIVE op=poly_reduce checked=%u mismatch=%u timing=excluded\n",
              ARITH_EXHAUSTIVE_COUNT, exhaustive_bad);
  vx_device_dump_perf(device, stdout);

  vx_buffer_release(output_buffer);
  vx_buffer_release(results_buffer);
  vx_buffer_release(exhaustive_buffer);
  vx_kernel_release(kernel);
  vx_module_release(module);
  vx_queue_release(queue);
  vx_device_release(device);
  std::printf("%s\n", errors ? "FAILED!" : "PASSED!");
  return errors ? 1 : 0;
}
