#include <vortex2.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
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
  int iterations = 8;
  int option;
  while ((option = getopt(argc, argv, "n:k:h")) != -1) {
    switch (option) {
    case 'n': iterations = std::atoi(optarg); break;
    case 'k': kernel_file = optarg; break;
    default:
      std::printf("Usage: [-n iterations] [-k kernel]\n");
      return option == 'h' ? 0 : 1;
    }
  }
  if (iterations < 1) {
    std::fprintf(stderr, "FAIL: iterations must be positive\n");
    return 1;
  }

  vx_device_h device = nullptr;
  CHECK(vx_device_open(0, &device));
  pqc::print_config(device, 1, 1);
  vx_queue_info_t queue_info = {sizeof(queue_info), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0};
  vx_queue_h queue = nullptr;
  CHECK(vx_queue_create(device, &queue_info, &queue));
  vx_buffer_h result_buffer = nullptr;
  CHECK(vx_buffer_create(device, sizeof(cost_result_t), VX_MEM_WRITE, &result_buffer));
  cost_result_t result{};
  CHECK(vx_enqueue_write(queue, result_buffer, 0, &result, sizeof(result), 0, nullptr, nullptr));

  kernel_arg_t arg{};
  arg.iterations = iterations;
  CHECK(vx_buffer_address(result_buffer, &arg.result_addr));
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
  launch.grid_dim[0] = 1;
  launch.block_dim[0] = 1;
  vx_event_h launched = nullptr, read = nullptr;
  CHECK(vx_enqueue_launch(queue, &launch, 0, nullptr, &launched));
  CHECK(vx_enqueue_read(queue, &result, result_buffer, 0, sizeof(result), 1, &launched, &read));
  CHECK(vx_event_wait_value(read, 1, VX_TIMEOUT_INFINITE));

  const char* names[COST_COUNT] = {
      "ntt", "ntt_identity", "ntt_nttmul_k",
      "intt", "intt_identity", "intt_nttmul_k"};
  bool failed = result.completed != arg.iterations;
  std::printf("iterations=%u warmup=1 mapping=M1L1 unroll=disabled\n", result.completed);
  std::printf("%-16s %14s %14s %10s\n", "arm", "cycles/call", "instrs/call", "checksum");
  for (unsigned arm = 0; arm < COST_COUNT; ++arm) {
    std::printf("%-16s %14.1f %14.1f 0x%08x\n", names[arm],
                (double)result.cycles[arm] / iterations,
                (double)result.instructions[arm] / iterations,
                result.checksums[arm]);
    failed |= result.cycles[arm] == 0 || result.instructions[arm] == 0;
    if (arm % 3 != 1) {
      failed |= result.mismatches[arm] != 0;
    }
  }
  for (unsigned inverse = 0; inverse < 2; ++inverse) {
    const unsigned arm = 3 * inverse;
    const double fraction = result.cycles[arm]
        ? 1.0 - (double)result.cycles[arm + 1] / result.cycles[arm] : 0.0;
    const double speedup = result.cycles[arm + 2]
        ? (double)result.cycles[arm] / result.cycles[arm + 2] : 0.0;
    std::printf("%s fqmul_calls=%u cycle_sensitivity=%.2f%% nttmul_speedup=%.3fx "
                "c_mismatches=%u ise_mismatches=%u\n",
                names[arm], inverse ? 1152u : 896u, 100.0 * fraction,
                speedup, result.mismatches[arm], result.mismatches[arm + 2]);
  }
  std::printf("Identity outputs are intentionally invalid; this is not a KAT or an ISA speedup.\n");
  vx_device_dump_perf(device, stdout);

  vx_event_release(read);
  vx_event_release(launched);
  vx_buffer_release(result_buffer);
  vx_kernel_release(kernel);
  vx_module_release(module);
  vx_queue_release(queue);
  vx_device_release(device);
  std::printf("%s\n", failed ? "FAILED!" :
      "PASSED: C and NTTMUL.K NTT/INTT agree with upstream; timing collected.");
  return failed ? 1 : 0;
}
