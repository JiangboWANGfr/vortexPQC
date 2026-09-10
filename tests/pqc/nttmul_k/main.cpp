#include <vortex2.h>

#include <cstdint>
#include <cstring>
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

static int16_t reference(int16_t a, int16_t b) {
  const int32_t product = (int32_t)a * (int32_t)b;
  const uint16_t inverted = (uint16_t)((uint16_t)product * UINT32_C(62209));
  const int32_t signed_inverted = inverted <= INT16_MAX
      ? (int32_t)inverted : (int32_t)inverted - 65536;
  const int32_t reduced = product - signed_inverted * 3329;
  return (int16_t)(reduced / 65536);
}

static uint32_t xorshift32(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

int main(int argc, char** argv) {
  const char* kernel_file = "kernel.vxbin";
  uint32_t count = 4093;
  uint32_t lanes = 13;
  uint32_t rounds = 17;
  uint32_t mode = NTTMUL_MODE_NORMAL;
  int option;
  while ((option = getopt(argc, argv, "n:t:r:m:k:h")) != -1) {
    switch (option) {
    case 'n': count = (uint32_t)std::strtoul(optarg, nullptr, 0); break;
    case 't': lanes = (uint32_t)std::strtoul(optarg, nullptr, 0); break;
    case 'r': rounds = (uint32_t)std::strtoul(optarg, nullptr, 0); break;
    case 'm':
      if (std::strcmp(optarg, "normal") == 0) {
        mode = NTTMUL_MODE_NORMAL;
      } else if (std::strcmp(optarg, "raw64") == 0) {
        mode = NTTMUL_MODE_RAW64;
      } else {
        std::fprintf(stderr, "FAIL: mode must be normal or raw64\n");
        return 1;
      }
      break;
    case 'k': kernel_file = optarg; break;
    default:
      std::printf("Usage: [-n coefficients] [-t lanes] [-r rounds] "
                  "[-m normal|raw64] [-k kernel]\n");
      return option == 'h' ? 0 : 1;
    }
  }
  if (mode == NTTMUL_MODE_RAW64) {
    rounds = NTTMUL_RAW_ROUNDS;
  }
  if (count == 0 || lanes == 0 || rounds == 0) {
    std::fprintf(stderr, "FAIL: count, lanes, and rounds must be positive\n");
    return 1;
  }

  vx_device_h device = nullptr;
  CHECK(vx_device_open(0, &device));
  const auto config = pqc::print_config(device, 1, lanes);
  if (pqc::require_slots(config, 1, lanes)) {
    vx_device_release(device);
    return 1;
  }

  static const int16_t edges[] = {
      INT16_MIN, INT16_MIN + 1, -3329, -1665, -1664, -1, 0, 1,
      1664, 1665, 3328, 3329, INT16_MAX - 1, INT16_MAX};
  std::vector<int16_t> input_a(count), input_b(count);
  std::vector<int32_t> output(count, 0);
  std::vector<uint64_t> cycles(mode == NTTMUL_MODE_RAW64 ? count : 0);
  uint32_t random_state = UINT32_C(0x6d2b79f5);
  const size_t edge_count = sizeof(edges) / sizeof(edges[0]);
  for (uint32_t i = 0; i < count; ++i) {
    if (i < edge_count * edge_count) {
      input_a[i] = edges[i / edge_count];
      input_b[i] = edges[i % edge_count];
    } else {
      input_a[i] = (int16_t)xorshift32(random_state);
      input_b[i] = (int16_t)xorshift32(random_state);
    }
  }

  vx_queue_info_t queue_info = {sizeof(queue_info), nullptr,
                                VX_QUEUE_PRIORITY_NORMAL, 0};
  vx_queue_h queue = nullptr;
  vx_buffer_h input_a_buffer = nullptr;
  vx_buffer_h input_b_buffer = nullptr;
  vx_buffer_h output_buffer = nullptr;
  vx_buffer_h cycles_buffer = nullptr;
  CHECK(vx_queue_create(device, &queue_info, &queue));
  CHECK(vx_buffer_create(device, input_a.size() * sizeof(input_a[0]),
                         VX_MEM_READ, &input_a_buffer));
  CHECK(vx_buffer_create(device, input_b.size() * sizeof(input_b[0]),
                         VX_MEM_READ, &input_b_buffer));
  CHECK(vx_buffer_create(device, output.size() * sizeof(output[0]),
                         VX_MEM_WRITE, &output_buffer));
  if (mode == NTTMUL_MODE_RAW64) {
    CHECK(vx_buffer_create(device, cycles.size() * sizeof(cycles[0]),
                           VX_MEM_WRITE, &cycles_buffer));
  }

  kernel_arg_t arg{};
  arg.count = count;
  arg.rounds = rounds;
  arg.mode = mode;
  CHECK(vx_buffer_address(input_a_buffer, &arg.input_a_addr));
  CHECK(vx_buffer_address(input_b_buffer, &arg.input_b_addr));
  CHECK(vx_buffer_address(output_buffer, &arg.output_addr));
  if (mode == NTTMUL_MODE_RAW64) {
    CHECK(vx_buffer_address(cycles_buffer, &arg.cycles_addr));
  }

  vx_module_h module = nullptr;
  vx_kernel_h kernel = nullptr;
  CHECK(vx_module_load_file(device, kernel_file, &module));
  CHECK(vx_module_get_kernel(module, "main", &kernel));

  vx_event_h write_a = nullptr;
  vx_event_h write_b = nullptr;
  vx_event_h launched = nullptr;
  vx_event_h read = nullptr;
  vx_event_h read_cycles = nullptr;
  CHECK(vx_enqueue_write(queue, input_a_buffer, 0, input_a.data(),
                         input_a.size() * sizeof(input_a[0]), 0, nullptr, &write_a));
  CHECK(vx_enqueue_write(queue, input_b_buffer, 0, input_b.data(),
                         input_b.size() * sizeof(input_b[0]), 1, &write_a, &write_b));
  vx_launch_info_t launch{};
  launch.struct_size = sizeof(launch);
  launch.kernel = kernel;
  launch.args_host = &arg;
  launch.args_size = sizeof(arg);
  launch.ndim = 1;
  launch.grid_dim[0] = 1;
  launch.block_dim[0] = lanes;
  CHECK(vx_enqueue_launch(queue, &launch, 1, &write_b, &launched));
  CHECK(vx_enqueue_read(queue, output.data(), output_buffer, 0,
                        output.size() * sizeof(output[0]), 1, &launched, &read));
  if (mode == NTTMUL_MODE_RAW64) {
    CHECK(vx_enqueue_read(queue, cycles.data(), cycles_buffer, 0,
                          cycles.size() * sizeof(cycles[0]), 1, &read,
                          &read_cycles));
  }
  CHECK(vx_event_wait_value(read_cycles ? read_cycles : read, 1,
                            VX_TIMEOUT_INFINITE));

  uint32_t mismatches = 0;
  for (uint32_t i = 0; i < count; ++i) {
    int16_t expected = input_a[i];
    for (uint32_t round = 0; round < rounds; ++round) {
      expected = reference(expected, input_b[i]);
    }
    if (output[i] != expected) {
      if (mismatches < 10) {
        std::printf("*** i=%u a=%d b=%d expected=%d actual=%d\n", i,
                    input_a[i], input_b[i], expected, output[i]);
      }
      ++mismatches;
    }
  }
  std::printf("NTTMUL.K: mode=%s checked=%u lanes=%u rounds=%u mismatches=%u\n",
              mode == NTTMUL_MODE_RAW64 ? "raw64" : "normal",
              count, lanes, rounds, mismatches);
  if (mode == NTTMUL_MODE_RAW64) {
    uint64_t min_cycles = UINT64_MAX;
    uint64_t max_cycles = 0;
    uint64_t total_cycles = 0;
    for (uint64_t elapsed : cycles) {
      if (elapsed < min_cycles) min_cycles = elapsed;
      if (elapsed > max_cycles) max_cycles = elapsed;
      total_cycles += elapsed;
    }
    const double mean_cycles = (double)total_cycles / count;
    std::printf("RAW chain: ops=%u min=%llu mean=%.2f max=%llu "
                "mean_cycles_per_op=%.4f\n",
                NTTMUL_RAW_ROUNDS, (unsigned long long)min_cycles, mean_cycles,
                (unsigned long long)max_cycles,
                mean_cycles / NTTMUL_RAW_ROUNDS);
  }
  vx_device_dump_perf(device, stdout);

  if (read_cycles) vx_event_release(read_cycles);
  vx_event_release(read);
  vx_event_release(launched);
  vx_event_release(write_b);
  vx_event_release(write_a);
  vx_kernel_release(kernel);
  vx_module_release(module);
  if (cycles_buffer) vx_buffer_release(cycles_buffer);
  vx_buffer_release(output_buffer);
  vx_buffer_release(input_b_buffer);
  vx_buffer_release(input_a_buffer);
  vx_queue_release(queue);
  vx_device_release(device);
  std::printf("%s\n", mismatches ? "FAILED!" : "PASSED!");
  return mismatches ? 1 : 0;
}
