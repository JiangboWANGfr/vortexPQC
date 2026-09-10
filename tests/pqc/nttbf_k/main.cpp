#include <vortex2.h>

#include <cstdint>
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

static int32_t signed16(uint32_t value) {
  int32_t result = (int32_t)(value & UINT32_C(0xffff));
  return result < 0x8000 ? result : result - 0x10000;
}

static int32_t wrap16(int32_t value) {
  return signed16((uint32_t)value);
}

static int32_t montgomery(int32_t a, int32_t b) {
  const int32_t product = a * b;
  const uint32_t inverted =
      (((uint32_t)product & UINT32_C(0xffff)) * UINT32_C(62209))
      & UINT32_C(0xffff);
  const int32_t factor = signed16(inverted);
  const int32_t difference = product - factor * 3329;
  return signed16((uint32_t)difference >> 16);
}

static int32_t barrett(int32_t value) {
  const int32_t quotient = (20159 * value + (1 << 25)) >> 26;
  return wrap16(value - quotient * 3329);
}

static uint32_t xorshift32(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

static int32_t reference(const std::vector<int16_t>& values,
                         const std::vector<int16_t>& zetas,
                         uint32_t vector, uint32_t lane, uint32_t op) {
  const uint32_t stage = op % NTTBF_K_STAGES;
  const uint32_t distance = 1u << stage;
  const uint32_t low_lane = lane & ~distance;
  const uint32_t high_lane = low_lane | distance;
  const uint32_t base = vector * NTTBF_K_LANES;
  const int32_t a = values[base + low_lane];
  const int32_t b = values[base + high_lane];
  const int32_t zeta = zetas[base + low_lane];
  if (op < NTTBF_K_STAGES) {
    const int32_t product = montgomery(b, zeta);
    return lane == low_lane ? wrap16(a + product) : wrap16(a - product);
  }
  const int32_t sum = wrap16(a + b);
  const int32_t difference = wrap16(b - a);
  return lane == low_lane ? barrett(sum) : montgomery(difference, zeta);
}

int main(int argc, char** argv) {
  const char* kernel_file = "kernel.vxbin";
  int option;
  while ((option = getopt(argc, argv, "k:h")) != -1) {
    if (option == 'k') {
      kernel_file = optarg;
    } else {
      std::printf("Usage: [-k kernel]\n");
      return option == 'h' ? 0 : 1;
    }
  }

  static const int16_t edges[] = {
      INT16_MIN, INT16_MIN + 1, -3329, -1665, -1664, -1, 0, 1,
      1664, 1665, 3328, 3329, INT16_MAX - 1, INT16_MAX};
  constexpr uint32_t random_vectors = 64;
  constexpr uint32_t edge_count = sizeof(edges) / sizeof(edges[0]);
  constexpr uint32_t edge_vectors = NTTBF_K_STAGES * edge_count * edge_count;
  constexpr uint32_t vector_count = edge_vectors + random_vectors;
  constexpr uint32_t lane_count = vector_count * NTTBF_K_LANES;
  std::vector<int16_t> values(lane_count);
  std::vector<int16_t> zetas(lane_count);
  uint32_t random_state = UINT32_C(0x6d2b79f5);
  for (uint32_t vector = 0; vector < vector_count; ++vector) {
    uint32_t target_stage = 0;
    uint32_t edge_a = 0;
    uint32_t edge_b = 0;
    uint32_t zeta_base;
    if (vector < edge_vectors) {
      target_stage = vector / (edge_count * edge_count);
      const uint32_t pair = vector % (edge_count * edge_count);
      edge_a = pair / edge_count;
      edge_b = pair % edge_count;
      zeta_base = (uint16_t)edges[(edge_a + edge_b + target_stage) % edge_count];
    } else {
      zeta_base = xorshift32(random_state);
    }
    for (uint32_t lane = 0; lane < NTTBF_K_LANES; ++lane) {
      const uint32_t index = vector * NTTBF_K_LANES + lane;
      if (vector < edge_vectors) {
        values[index] = edges[(lane & (1u << target_stage)) ? edge_b : edge_a];
      } else {
        values[index] = (int16_t)xorshift32(random_state);
      }
      // Distinct per-lane zetas make a pair-high twiddle bug observable.
      zetas[index] = (int16_t)(zeta_base + lane * UINT32_C(2053));
    }
  }

  vx_device_h device = nullptr;
  CHECK(vx_device_open(0, &device));
  const auto config = pqc::print_config(device, 1, NTTBF_K_LANES);
  if (pqc::require_slots(config, 1, NTTBF_K_LANES)) {
    vx_device_release(device);
    return 1;
  }

  vx_queue_info_t queue_info = {
      sizeof(queue_info), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0};
  vx_queue_h queue = nullptr;
  vx_buffer_h values_buffer = nullptr;
  vx_buffer_h zetas_buffer = nullptr;
  vx_buffer_h output_buffer = nullptr;
  CHECK(vx_queue_create(device, &queue_info, &queue));
  CHECK(vx_buffer_create(device, values.size() * sizeof(values[0]),
                         VX_MEM_READ, &values_buffer));
  CHECK(vx_buffer_create(device, zetas.size() * sizeof(zetas[0]),
                         VX_MEM_READ, &zetas_buffer));
  std::vector<int32_t> output(NTTBF_K_OPS * lane_count, INT32_MAX);
  CHECK(vx_buffer_create(device, output.size() * sizeof(output[0]),
                         VX_MEM_WRITE, &output_buffer));

  kernel_arg_t arg{};
  CHECK(vx_buffer_address(values_buffer, &arg.values_addr));
  CHECK(vx_buffer_address(zetas_buffer, &arg.zetas_addr));
  CHECK(vx_buffer_address(output_buffer, &arg.output_addr));
  arg.vectors = vector_count;

  vx_module_h module = nullptr;
  vx_kernel_h kernel = nullptr;
  CHECK(vx_module_load_file(device, kernel_file, &module));
  CHECK(vx_module_get_kernel(module, "main", &kernel));
  vx_event_h write_values = nullptr;
  vx_event_h write_zetas = nullptr;
  vx_event_h launched = nullptr;
  vx_event_h read = nullptr;
  CHECK(vx_enqueue_write(queue, values_buffer, 0, values.data(),
                         values.size() * sizeof(values[0]), 0, nullptr,
                         &write_values));
  CHECK(vx_enqueue_write(queue, zetas_buffer, 0, zetas.data(),
                         zetas.size() * sizeof(zetas[0]), 1, &write_values,
                         &write_zetas));
  vx_launch_info_t launch{};
  launch.struct_size = sizeof(launch);
  launch.kernel = kernel;
  launch.args_host = &arg;
  launch.args_size = sizeof(arg);
  launch.ndim = 1;
  launch.grid_dim[0] = 1;
  launch.block_dim[0] = NTTBF_K_LANES;
  CHECK(vx_enqueue_launch(queue, &launch, 1, &write_zetas, &launched));
  CHECK(vx_enqueue_read(queue, output.data(), output_buffer, 0,
                        output.size() * sizeof(output[0]), 1, &launched, &read));
  CHECK(vx_event_wait_value(read, 1, VX_TIMEOUT_INFINITE));

  uint32_t mismatches = 0;
  for (uint32_t op = 0; op < NTTBF_K_OPS; ++op) {
    for (uint32_t vector = 0; vector < vector_count; ++vector) {
      for (uint32_t lane = 0; lane < NTTBF_K_LANES; ++lane) {
        const uint32_t index = vector * NTTBF_K_LANES + lane;
        const int32_t expected = reference(values, zetas, vector, lane, op);
        const int32_t actual = output[op * lane_count + index];
        if (actual != expected) {
          if (mismatches < 10) {
            std::printf("*** op=%u vector=%u lane=%u expected=%d actual=%d\n",
                        op, vector, lane, expected, actual);
          }
          ++mismatches;
        }
      }
    }
  }
  std::printf("NTTBF.K: vectors=%u outputs=%u mismatches=%u\n",
              vector_count, NTTBF_K_OPS * lane_count, mismatches);
  vx_device_dump_perf(device, stdout);

  vx_event_release(read);
  vx_event_release(launched);
  vx_event_release(write_zetas);
  vx_event_release(write_values);
  vx_kernel_release(kernel);
  vx_module_release(module);
  vx_buffer_release(output_buffer);
  vx_buffer_release(zetas_buffer);
  vx_buffer_release(values_buffer);
  vx_queue_release(queue);
  vx_device_release(device);
  std::printf("%s\n", mismatches ? "FAILED!" : "PASSED!");
  return mismatches ? 1 : 0;
}
