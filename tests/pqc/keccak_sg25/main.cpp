#include <vortex2.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>
#include "common.h"
#include "pqc_config.h"
#include "reference.h"

#define CHECK(_e) do { int _r = (_e); if (_r) { \
  std::fprintf(stderr, "FAIL %s:%d: '%s' -> %d\n", __FILE__, __LINE__, #_e, _r); \
  std::exit(-1); } } while (0)

static const size_t guard_bytes = 16;
static const uint8_t poison = 0xa5;
static const unsigned trace_states = 17;

struct shake_test_t {
  std::vector<uint8_t> input;
  uint32_t output_bytes;
  uint32_t rate;
};

static std::vector<uint64_t> make_states() {
  std::vector<uint64_t> states(trace_states * SG25_WORDS, 0);
  uint64_t value = 0x0123456789abcdefULL;
  for (size_t i = SG25_WORDS; i < states.size(); ++i) {
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    states[i] = value;
  }
  return states;
}

static bool check_reference(const std::vector<uint64_t>& states) {
  for (unsigned i = 0; i < trace_states; ++i) {
    uint64_t reference[SG25_WORDS], scalar[SG25_WORDS];
    std::memcpy(reference, states.data() + i * SG25_WORDS, sizeof(reference));
    std::memcpy(scalar, reference, sizeof(scalar));
    mlk_keccakf1600_permute(reference);
    for (unsigned round = 0; round < SG25_ROUNDS; ++round) {
      sg25_ref::round(scalar, round);
    }
    if (std::memcmp(reference, scalar, sizeof(reference)) != 0) {
      std::fprintf(stderr, "Host round reference disagrees with mlkem-native: state %u\n", i);
      return false;
    }
  }
  for (const auto& kat : sg25_ref::shake_kats) {
    uint8_t output[32];
    char hex[65];
    sg25_ref::shake(reinterpret_cast<const uint8_t*>(kat.input),
                    std::strlen(kat.input), output, sizeof(output), kat.rate);
    for (size_t i = 0; i < sizeof(output); ++i) {
      std::snprintf(hex + 2 * i, 3, "%02x", output[i]);
    }
    if (std::strcmp(hex, kat.output_hex) != 0) {
      std::fprintf(stderr, "Host SHAKE%u KAT failed for '%s'\n",
                   kat.rate == 168 ? 128 : 256, kat.input);
      return false;
    }
  }
  std::printf("Host reference: %u permutations and 4 SHAKE KATs passed\n", trace_states);
  return true;
}

static std::vector<shake_test_t> make_shake_tests() {
  std::vector<shake_test_t> tests;
  for (uint32_t rate : {168u, 136u}) {
    const uint32_t lengths[] = {
      0, 1, 7, 8, rate - 1, rate, rate + 1, 2 * rate - 1, 2 * rate, 2 * rate + 1
    };
    for (uint32_t input_bytes : lengths) {
      for (uint32_t output_bytes : lengths) {
        shake_test_t test{std::vector<uint8_t>(input_bytes), output_bytes, rate};
        for (uint32_t i = 0; i < input_bytes; ++i) {
          test.input[i] = uint8_t(i * 149 + input_bytes * 17 + rate);
        }
        tests.push_back(test);
      }
    }
  }
  for (const auto& kat : sg25_ref::shake_kats) {
    const auto* input = reinterpret_cast<const uint8_t*>(kat.input);
    tests.push_back({std::vector<uint8_t>(input, input + std::strlen(kat.input)),
                     32, kat.rate});
  }
  return tests;
}

static void execute(vx_device_h dev, vx_queue_h queue, vx_kernel_h kernel,
                    uint32_t mode, uint32_t blocks,
                    const std::vector<uint8_t>& input,
                    std::vector<uint8_t>& output,
                    const std::vector<sg25_case_t>& cases,
                    uint32_t permutations = 0, uint32_t stage_variant = 0, uint32_t round = 0) {
  vx_buffer_h input_buffer = nullptr, output_buffer = nullptr, cases_buffer = nullptr;
  const size_t cases_bytes = cases.size() * sizeof(sg25_case_t);
  CHECK(vx_buffer_create(dev, input.size(), VX_MEM_READ, &input_buffer));
  CHECK(vx_buffer_create(dev, output.size(), VX_MEM_WRITE, &output_buffer));
  CHECK(vx_buffer_create(dev, cases_bytes, VX_MEM_READ, &cases_buffer));
  CHECK(vx_enqueue_write(queue, input_buffer, 0, input.data(), input.size(), 0, nullptr, nullptr));
  CHECK(vx_enqueue_write(queue, output_buffer, 0, output.data(), output.size(), 0, nullptr, nullptr));
  CHECK(vx_enqueue_write(queue, cases_buffer, 0, cases.data(), cases_bytes, 0, nullptr, nullptr));

  kernel_arg_t args{};
  CHECK(vx_buffer_address(input_buffer, &args.input_addr));
  CHECK(vx_buffer_address(output_buffer, &args.output_addr));
  CHECK(vx_buffer_address(cases_buffer, &args.cases_addr));
  args.output_addr += guard_bytes;
  args.timing_addr = args.output_addr + blocks * SG25_WORDS * sizeof(uint64_t);
  args.mode = mode;
  args.permutations = permutations;
  args.stage_variant = stage_variant;
  args.round = round;
  vx_launch_info_t launch{};
  launch.struct_size = sizeof(launch);
  launch.kernel = kernel;
  launch.args_host = &args;
  launch.args_size = sizeof(args);
  launch.ndim = 1;
  launch.grid_dim[0] = (mode == SG25_MODE_BENCH) ? 1 : blocks;
  launch.block_dim[0] = (mode == SG25_MODE_BENCH) ? 32 * blocks : 32;
  vx_event_h launched = nullptr, read = nullptr;
  CHECK(vx_enqueue_launch(queue, &launch, 0, nullptr, &launched));
  CHECK(vx_enqueue_read(queue, output.data(), output_buffer, 0, output.size(),
                        1, &launched, &read));
  CHECK(vx_event_wait_value(read, 1, VX_TIMEOUT_INFINITE));
  CHECK(vx_event_release(read));
  CHECK(vx_event_release(launched));
  CHECK(vx_buffer_release(cases_buffer));
  CHECK(vx_buffer_release(output_buffer));
  CHECK(vx_buffer_release(input_buffer));
}

static size_t compare_bytes(const std::vector<uint8_t>& output,
                            const std::vector<uint8_t>& expected,
                            const char* mode, size_t first_case) {
  size_t errors = 0;
  for (size_t i = 0; i < output.size(); ++i) {
    if (output[i] != expected[i]) {
      if (errors < 4) {
        if (std::strcmp(mode, "TRACE") == 0 && i >= guard_bytes
            && i < output.size() - guard_bytes) {
          const size_t word = (i - guard_bytes) / sizeof(uint64_t);
          std::printf("TRACE state=%zu round=%zu lane=%zu byte=%zu: got %02x, expected %02x\n",
                      first_case + word / (SG25_ROUNDS * SG25_WORDS),
                      word / SG25_WORDS % SG25_ROUNDS, word % SG25_WORDS,
                      (i - guard_bytes) % sizeof(uint64_t), output[i], expected[i]);
        } else {
          std::printf("%s batch starting at case %zu byte %zu: got %02x, expected %02x\n",
                      mode, first_case, i, output[i], expected[i]);
        }
      }
      ++errors;
    }
  }
  return errors;
}

static size_t test_trace(vx_device_h dev, vx_queue_h queue, vx_kernel_h kernel,
                         uint32_t batch, const std::vector<uint64_t>& states) {
  size_t errors = 0;
  for (unsigned start = 0; start < trace_states; start += batch) {
    const unsigned count = std::min(batch, trace_states - start);
    std::vector<uint8_t> input(count * SG25_WORDS * sizeof(uint64_t));
    std::memcpy(input.data(), states.data() + start * SG25_WORDS, input.size());
    std::vector<uint8_t> output(2 * guard_bytes + count * SG25_ROUNDS
                               * SG25_WORDS * sizeof(uint64_t), poison);
    std::vector<uint8_t> expected(output);
    for (unsigned i = 0; i < count; ++i) {
      uint64_t state[SG25_WORDS];
      std::memcpy(state, states.data() + (start + i) * SG25_WORDS, sizeof(state));
      for (unsigned round = 0; round < SG25_ROUNDS; ++round) {
        sg25_ref::round(state, round);
        const size_t offset = guard_bytes + (i * SG25_ROUNDS + round) * sizeof(state);
        std::memcpy(expected.data() + offset, state, sizeof(state));
      }
    }
    execute(dev, queue, kernel, SG25_MODE_TRACE, count, input, output, {{}});
    errors += compare_bytes(output, expected, "TRACE", start);
  }
  std::printf("TRACE: states=%u rounds=%u word_checks=%u bad_bytes=%zu\n",
              trace_states, SG25_ROUNDS, trace_states * SG25_ROUNDS * SG25_WORDS, errors);
  return errors;
}

static size_t test_shake(vx_device_h dev, vx_queue_h queue, vx_kernel_h kernel,
                         uint32_t batch) {
  const auto tests = make_shake_tests();
  size_t errors = 0, output_bytes = 0, checked_bytes = 0;
  for (size_t start = 0; start < tests.size(); start += batch) {
    const uint32_t count = uint32_t(std::min(size_t(batch), tests.size() - start));
    std::vector<uint8_t> input(3, poison), output(guard_bytes, poison);
    std::vector<sg25_case_t> cases;
    for (unsigned i = 0; i < count; ++i) {
      const auto& test = tests[start + i];
      output.resize(output.size() + guard_bytes + 1, poison);
      sg25_case_t descriptor{};
      descriptor.input_offset = uint32_t(input.size());
      descriptor.output_offset = uint32_t(output.size() - guard_bytes);
      descriptor.input_bytes = uint32_t(test.input.size());
      descriptor.output_bytes = test.output_bytes;
      descriptor.rate = test.rate;
      cases.push_back(descriptor);
      input.insert(input.end(), test.input.begin(), test.input.end());
      input.push_back(poison);
      output.resize(output.size() + guard_bytes + test.output_bytes, poison);
      output_bytes += test.output_bytes;
    }
    output.resize(output.size() + guard_bytes, poison);
    std::vector<uint8_t> expected(output);
    for (const auto& descriptor : cases) {
      sg25_ref::shake(input.data() + descriptor.input_offset, descriptor.input_bytes,
                      expected.data() + guard_bytes + descriptor.output_offset,
                      descriptor.output_bytes, descriptor.rate);
    }
    execute(dev, queue, kernel, SG25_MODE_SHAKE, count, input, output, cases);
    errors += compare_bytes(output, expected, "SHAKE", start);
    checked_bytes += output.size();
  }
  std::printf("SHAKE: cases=%zu output_bytes=%zu checked_bytes_with_guards=%zu bad_bytes=%zu\n",
              tests.size(), output_bytes, checked_bytes, errors);
  return errors;
}

static size_t test_benchmark(vx_device_h dev, vx_queue_h queue, vx_kernel_h kernel,
                            uint32_t batch, uint32_t permutations,
                            const std::vector<uint64_t>& states) {
  const size_t states_bytes = batch * SG25_WORDS * sizeof(uint64_t);
  const size_t timing_bytes = batch * sizeof(sg25_timing_t);
  std::vector<uint8_t> input(states_bytes);
  std::memcpy(input.data(), states.data(), states_bytes);
  std::vector<uint8_t> output(2 * guard_bytes + states_bytes + timing_bytes, poison);
  std::vector<uint8_t> expected(output);
  for (unsigned i = 0; i < batch; ++i) {
    uint64_t state[SG25_WORDS];
    std::memcpy(state, states.data() + i * SG25_WORDS, sizeof(state));
    for (unsigned p = 0; p < permutations; ++p) {
      mlk_keccakf1600_permute(state);
    }
    std::memcpy(expected.data() + guard_bytes + i * sizeof(state), state, sizeof(state));
  }
  execute(dev, queue, kernel, SG25_MODE_BENCH, batch, input, output, {{}}, permutations);
  std::vector<sg25_timing_t> timing(batch);
  std::memcpy(timing.data(), output.data() + guard_bytes + states_bytes, timing_bytes);
  std::memcpy(expected.data() + guard_bytes + states_bytes, timing.data(), timing_bytes);
  size_t errors = compare_bytes(output, expected, "BENCH", 0);
  uint64_t first_start = UINT64_MAX, last_end = 0;
  for (unsigned i = 0; i < batch; ++i) {
    const auto& sample = timing[i];
    if (sample.end <= sample.start) {
      std::fprintf(stderr, "Invalid cycle interval for state %u\n", i);
      ++errors;
      continue;
    }
    first_start = std::min(first_start, sample.start);
    last_end = std::max(last_end, sample.end);
    std::printf("CHAIN: state=%u permutations=%u start=%llu end=%llu cycles=%llu "
                "cycles_per_permutation=%.3f\n", i, permutations,
                (unsigned long long)sample.start, (unsigned long long)sample.end,
                (unsigned long long)(sample.end - sample.start),
                double(sample.end - sample.start) / permutations);
  }
  if (last_end > first_start && errors == 0) {
    const uint64_t span = last_end - first_start;
    const uint64_t completed = uint64_t(batch) * permutations;
    std::printf("BENCH: arm=%s batch_warps=%u permutations_per_state=%u "
                "completed_permutations=%llu span_cycles=%llu "
                "cycles_per_completed_permutation=%.3f permutations_per_cycle=%.9f "
                "bad_bytes=%zu\n",
#ifdef SG25_KROUND_ISA
                "KROUND",
#else
#ifdef SG25_THETA_ISA
                "T"
#else
                "-"
#endif
#ifdef SG25_RHOPI_ISA
                "R"
#else
                "-"
#endif
#ifdef SG25_CHII_ISA
                "C",
#else
                "-",
#endif
#endif
                batch, permutations, (unsigned long long)completed,
                (unsigned long long)span, double(span) / completed,
                double(completed) / span, errors);
  }
  return errors;
}

#ifdef SG25_ISE
static size_t test_stage(vx_device_h dev, vx_queue_h queue, vx_kernel_h kernel,
                        uint32_t batch, unsigned mode, const char* name) {
  const bool chi = mode == SG25_MODE_CHII;
  const unsigned state_count = chi ? 8 : 3 + 2 * SG25_WORDS;
  const unsigned rounds = chi ? SG25_ROUNDS : 1;
  std::vector<uint64_t> states(state_count * 32, 0);
  uint64_t random = 0x0123456789abcdefULL;
  for (unsigned state = 0; state < state_count; ++state) {
    for (unsigned lane = 0; lane < 32; ++lane) {
      random ^= random << 13;
      random ^= random >> 7;
      random ^= random << 17;
      if (lane >= SG25_WORDS || state == 1) {
        states[state * 32 + lane] = random;
      } else if (state == 2) {
        states[state * 32 + lane] = UINT64_MAX;
      } else if (state >= 3 && (chi ? lane % 5 == state - 3 : lane == (state - 3) / 2)) {
        states[state * 32 + lane] = chi ? 0xaaaaaaaa55555555ULL
            : uint64_t(1) << (31 + 32 * ((state - 3) % 2));
      }
    }
  }
  static const unsigned rho[25] = {
    0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
    25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14
  };
  size_t errors = 0, checks = 0;
  for (unsigned round = 0; round < rounds; ++round) {
    for (unsigned variant = 0; variant < SG25_STAGE_VARIANTS; ++variant) {
      if (chi && variant != 0 && round != 0 && round != 23) continue;
      for (unsigned start = 0; start < state_count; start += batch) {
        const unsigned count = std::min(batch, state_count - start);
        std::vector<uint8_t> input(count * 32 * sizeof(uint64_t));
        std::memcpy(input.data(), states.data() + start * 32, input.size());
        std::vector<uint8_t> output(2 * guard_bytes + input.size(), poison);
        std::vector<uint8_t> expected(output);
        for (unsigned i = 0; i < count; ++i) {
          uint64_t source[32], result[32] = {}, columns[5] = {};
          std::memcpy(source, states.data() + (start + i) * 32, sizeof(source));
          for (unsigned lane = 0; lane < SG25_WORDS; ++lane) {
            if (chi) {
              if (variant == 3) source[lane] = uint64_t(round) * 0x100000001ULL;
              if (variant == 4) source[lane] = 0;
            } else {
              if (variant == 3) source[lane] = uint32_t(source[lane]) * 0x100000001ULL;
              if (variant == 4) source[lane] &= 0xffffffff00000000ULL;
              if (variant == 5) source[lane] &= 0xffffffffULL;
            }
            columns[lane % 5] ^= source[lane];
          }
          for (unsigned y = 0; y < 5; ++y) {
            for (unsigned x = 0; x < 5; ++x) {
              const unsigned t = x + 5*y;
              if (mode == SG25_MODE_THETA) {
                result[t] = source[t] ^ columns[(x+4)%5]
                          ^ sg25_ref::rotate(columns[(x+1)%5], 1);
              } else if (mode == SG25_MODE_RHOPI) {
                result[y + 5*((2*x + 3*y)%5)] = sg25_ref::rotate(source[t], rho[t]);
              } else {
                result[t] = source[t] ^ (~source[(x+1)%5 + 5*y] & source[(x+2)%5 + 5*y]);
              }
            }
          }
          if (chi) result[0] ^= sg25_ref::round_constants[variant == 5 ? 0 : round];
          std::memcpy(expected.data() + guard_bytes + i * sizeof(result), result, sizeof(result));
        }
        execute(dev, queue, kernel, mode, count, input, output, {{}}, 0, variant, round);
        errors += compare_bytes(output, expected, name, (round * SG25_STAGE_VARIANTS + variant) * state_count + start);
        checks += count * 32;
      }
    }
  }
  std::printf("%s: states=%u rounds=%u word_checks=%zu bad_bytes=%zu\n",
              name, state_count, rounds, checks, errors);
  return errors;
}
#endif

int main(int argc, char** argv) {
  const char* kernel_file = "kernel.vxbin";
  uint32_t batch = 8;
  uint32_t permutations = 0;
  bool stage_only = false;
  int option;
  while ((option = getopt(argc, argv, "k:b:p:sh")) != -1) {
    if (option == 'k') {
      kernel_file = optarg;
    } else if (option == 'b' || option == 'p') {
      char* end = nullptr;
      const unsigned long value = std::strtoul(optarg, &end, 10);
      if (end == optarg || *end != '\0' || value == 0 || value > UINT32_MAX) {
        std::fprintf(stderr, "-%c must be a positive 32-bit integer\n", option);
        return -1;
      }
      if (option == 'b') {
        batch = uint32_t(value);
      } else {
        permutations = uint32_t(value);
      }
    } else if (option == 's') {
      stage_only = true;
    } else {
      std::printf("Usage: %s [-k kernel.vxbin] [-b batch_warps (1..8)] "
                  "[-p benchmark_permutations | -s enabled_stages_only]\n", argv[0]);
      return option == 'h' ? 0 : -1;
    }
  }
  if (batch > 8 || (stage_only && permutations != 0)) {
    std::fprintf(stderr, "-b must be at most 8; -s and -p are mutually exclusive\n");
    return -1;
  }
#ifndef SG25_ISE
  if (stage_only) {
    std::fprintf(stderr, "-s requires at least one enabled ISE stage\n");
    return -1;
  }
#endif
  const auto states = make_states();
  if (!check_reference(states)) {
    return -1;
  }
  vx_device_h dev = nullptr;
  CHECK(vx_device_open(0, &dev));
  const pqc::config config = pqc::print_config(dev, batch, 32);
#ifdef SG25_ISE
  if ((config.isa_flags & VX_ISA_EXT_KSG25) == 0) {
    std::fprintf(stderr, "SG25 ISE requires a device with KSG25 enabled\n");
    CHECK(vx_device_release(dev));
    return -1;
  }
#endif
#ifdef SG25_KROUND_ISA
  if ((config.isa_flags & VX_ISA_EXT_KROUND25) == 0) {
    std::fprintf(stderr, "KROUND ISE requires a device with KROUND25 enabled\n");
    CHECK(vx_device_release(dev));
    return -1;
  }
#endif
  if (permutations != 0 && (config.clusters != 1 || config.cores != 1)) {
    std::fprintf(stderr, "Benchmark cycle intervals require one core in one cluster\n");
    CHECK(vx_device_release(dev));
    return -1;
  }
  if (config.threads != 32 || pqc::require_slots(config, batch, 32) != 0) {
    std::fprintf(stderr, "SG25 requires 32 threads per warp and at least %u warp slots\n", batch);
    CHECK(vx_device_release(dev));
    return -1;
  }
  vx_queue_info_t queue_info{sizeof(queue_info), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0};
  vx_queue_h queue = nullptr;
  CHECK(vx_queue_create(dev, &queue_info, &queue));
  vx_module_h module = nullptr;
  vx_kernel_h kernel = nullptr;
  CHECK(vx_module_load_file(dev, kernel_file, &module));
  CHECK(vx_module_get_kernel(module, "main", &kernel));

  size_t errors = 0;
  if (permutations != 0) {
    errors += test_benchmark(dev, queue, kernel, batch, permutations, states);
  } else {
#ifdef SG25_THETA_ISA
    errors += test_stage(dev, queue, kernel, batch, SG25_MODE_THETA, "THETA");
#endif
#ifdef SG25_RHOPI_ISA
    errors += test_stage(dev, queue, kernel, batch, SG25_MODE_RHOPI, "RHOPI");
#endif
#ifdef SG25_CHII_ISA
    errors += test_stage(dev, queue, kernel, batch, SG25_MODE_CHII, "CHII");
#endif
    if (!stage_only) {
      errors += test_trace(dev, queue, kernel, batch, states);
      errors += test_shake(dev, queue, kernel, batch);
    }
  }
  CHECK(vx_device_dump_perf(dev, stdout));

  CHECK(vx_kernel_release(kernel));
  CHECK(vx_module_release(module));
  CHECK(vx_queue_release(queue));
  CHECK(vx_device_release(dev));
  std::puts(errors ? "FAILED!" : "PASSED!");
  return errors ? -1 : 0;
}
