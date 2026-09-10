#include <vortex2.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "common.h"
#include "pqc_config.h"
#include "keccak_sg25/reference.h"

#define CHECK(expr) do { \
  vx_result_t status = (expr); \
  if (status != VX_SUCCESS) { \
    std::fprintf(stderr, "FAIL: %s: %s\n", #expr, vx_result_string(status)); \
    std::exit(1); \
  } \
} while (0)

static int32_t signed16(uint32_t value) {
  const int32_t low = int32_t(value & 0xffff);
  return low < 32768 ? low : low - 65536;
}

static int32_t montgomery(int32_t a, int32_t b) {
  const int32_t product = a * b;
  const int32_t factor = signed16(uint32_t(product) * UINT32_C(62209));
  return signed16(uint32_t((product - factor * 3329) / 65536));
}

static std::vector<uint64_t> reference(const std::vector<uint64_t>& input) {
  std::vector<uint64_t> expected(MIX_STEPS * MIX_THREADS);
  for (uint32_t warp = 0; warp < MIX_WARPS; ++warp) {
    std::array<uint64_t, MIX_LANES> state;
    std::array<int32_t, MIX_LANES> zeta;
    for (uint32_t lane = 0; lane < MIX_LANES; ++lane) {
      state[lane] = input[warp * MIX_LANES + lane];
      zeta[lane] = signed16(uint32_t(state[lane] >> 32));
    }
    for (uint32_t step = 0; step < MIX_STEPS; ++step) {
      if (warp % 4 < 2) {
        sg25_ref::round(state.data(), 0);
        std::fill(state.begin() + SG25_WORDS, state.end(), 0);
      } else if (warp % 4 == 2) {
        for (uint32_t lane = 0; lane < MIX_LANES; ++lane) {
          state[lane] = uint32_t(montgomery(signed16(uint32_t(state[lane])), zeta[lane]));
        }
      } else {
        std::array<int32_t, MIX_LANES> ct;
        for (uint32_t lane = 0; lane < MIX_LANES; ++lane) {
          const uint32_t low = lane & ~16u;
          const int32_t a = signed16(uint32_t(state[low]));
          const int32_t t = montgomery(signed16(uint32_t(state[low | 16])), zeta[low]);
          ct[lane] = signed16(uint32_t(lane == low ? a + t : a - t));
        }
        for (uint32_t lane = 0; lane < MIX_LANES; ++lane) {
          const uint32_t low = lane & ~1u;
          const int32_t sum = signed16(uint32_t(ct[low] + ct[low | 1]));
          const int32_t quotient = (20159 * sum + (1 << 25)) >> 26;
          const int32_t value = lane == low ? signed16(uint32_t(sum - quotient * 3329))
              : montgomery(signed16(uint32_t(ct[low | 1] - ct[low])), zeta[low]);
          state[lane] = uint32_t(value);
        }
      }
      std::copy(state.begin(), state.end(), expected.begin() + step * MIX_THREADS + warp * MIX_LANES);
    }
  }
  return expected;
}

int main() {
  std::vector<uint64_t> input(MIX_THREADS), output(MIX_STEPS * MIX_THREADS, UINT64_MAX);
  for (uint32_t i = 0; i < MIX_THREADS; ++i) {
    input[i] = UINT64_C(0x9e3779b97f4a7c15) * (i + 1) ^ UINT64_C(0x87c37b91114253d5);
  }
  const auto expected = reference(input);
  vx_device_h device = nullptr;
  CHECK(vx_device_open(0, &device));
  const auto config = pqc::print_config(device, MIX_WARPS, MIX_LANES);
  if (config.warps != MIX_WARPS || config.threads != MIX_LANES) {
    std::fprintf(stderr, "FAIL: pqc_alu_mix requires W8T32\n");
    vx_device_release(device);
    return 1;
  }
  vx_queue_info_t queue_info = {sizeof(queue_info), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0};
  vx_queue_h queue = nullptr;
  vx_buffer_h inputs = nullptr, outputs = nullptr;
  CHECK(vx_queue_create(device, &queue_info, &queue));
  CHECK(vx_buffer_create(device, input.size() * sizeof(uint64_t), VX_MEM_READ, &inputs));
  CHECK(vx_buffer_create(device, output.size() * sizeof(uint64_t), VX_MEM_WRITE, &outputs));
  mix_arg_t arg{};
  CHECK(vx_buffer_address(inputs, &arg.input_addr));
  CHECK(vx_buffer_address(outputs, &arg.output_addr));
  vx_module_h module = nullptr;
  vx_kernel_h kernel = nullptr;
  CHECK(vx_module_load_file(device, "kernel.vxbin", &module));
  CHECK(vx_module_get_kernel(module, "main", &kernel));
  vx_event_h written = nullptr, initialized = nullptr, launched = nullptr, read = nullptr;
  CHECK(vx_enqueue_write(queue, inputs, 0, input.data(), input.size() * sizeof(uint64_t), 0, nullptr, &written));
  CHECK(vx_enqueue_write(queue, outputs, 0, output.data(), output.size() * sizeof(uint64_t), 1, &written, &initialized));
  vx_launch_info_t launch{};
  launch.struct_size = sizeof(launch);
  launch.kernel = kernel;
  launch.args_host = &arg;
  launch.args_size = sizeof(arg);
  launch.ndim = 1;
  launch.grid_dim[0] = 1;
  launch.block_dim[0] = MIX_THREADS;
  CHECK(vx_enqueue_launch(queue, &launch, 1, &initialized, &launched));
  CHECK(vx_enqueue_read(queue, output.data(), outputs, 0, output.size() * sizeof(uint64_t), 1, &launched, &read));
  CHECK(vx_event_wait_value(read, 1, VX_TIMEOUT_INFINITE));
  uint32_t mismatches = 0;
  for (uint32_t i = 0; i < output.size(); ++i) {
    if (output[i] == expected[i]) {
      continue;
    }
    if (mismatches < 8) {
      std::printf("*** step=%u warp=%u lane=%u expected=%016llx actual=%016llx\n",
                  i / MIX_THREADS, i / MIX_LANES % MIX_WARPS, i % MIX_LANES,
                  (unsigned long long)expected[i], (unsigned long long)output[i]);
    }
    ++mismatches;
  }
  std::printf("PQC_ALU_MIX: warps=%u steps=%u checked=%zu mismatches=%u\n",
              MIX_WARPS, MIX_STEPS, output.size(), mismatches);
  vx_device_dump_perf(device, stdout);
  vx_event_release(read);
  vx_event_release(launched);
  vx_event_release(initialized);
  vx_event_release(written);
  vx_kernel_release(kernel);
  vx_module_release(module);
  vx_buffer_release(outputs);
  vx_buffer_release(inputs);
  vx_queue_release(queue);
  vx_device_release(device);
  std::puts(mismatches ? "FAILED!" : "PASSED!");
  return mismatches ? 1 : 0;
}
