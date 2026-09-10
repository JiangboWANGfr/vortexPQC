#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include "common.h"
#ifdef SG25_ISE
#include <pqc/vx_ksg25.h>
#ifndef VX_CFG_EXT_KSG25_ENABLE
#error "SG25 ISE requires VX_CFG_EXT_KSG25_ENABLE in CONFIGS"
#endif
#endif
#ifdef SG25_KROUND_ISA
#include <pqc/vx_kround25.h>
#ifndef VX_CFG_EXT_KROUND25_ENABLE
#error "KROUND ISE requires VX_CFG_EXT_KROUND25_ENABLE in CONFIGS"
#endif
#endif

static_assert(VX_CFG_XLEN == 32, "SG25 software baseline requires RV32");
static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_NUM_ALU_LANES == 32,
              "SG25 requires a complete 32-lane ALU vector");

#ifndef SG25_KROUND_ISA
#ifndef SG25_CHII_ISA
static const uint64_t round_constants[SG25_ROUNDS] = {
  0x0000000000000001ULL, 0x0000000000008082ULL,
  0x800000000000808aULL, 0x8000000080008000ULL,
  0x000000000000808bULL, 0x0000000080000001ULL,
  0x8000000080008081ULL, 0x8000000000008009ULL,
  0x000000000000008aULL, 0x0000000000000088ULL,
  0x0000000080008009ULL, 0x000000008000000aULL,
  0x000000008000808bULL, 0x800000000000008bULL,
  0x8000000000008089ULL, 0x8000000000008003ULL,
  0x8000000000008002ULL, 0x8000000000000080ULL,
  0x000000000000800aULL, 0x800000008000000aULL,
  0x8000000080008081ULL, 0x8000000000008080ULL,
  0x0000000080000001ULL, 0x8000000080008008ULL,
};

#endif

#ifndef SG25_RHOPI_ISA
static const uint8_t rho[SG25_WORDS] = {
  0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
  25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
};

#endif

#if !defined(SG25_THETA_ISA) || !defined(SG25_RHOPI_ISA) || !defined(SG25_CHII_ISA)
static inline uint64_t shuffle(uint64_t value, unsigned source) {
  const uint32_t lo = vx_shfl_idx(static_cast<uint32_t>(value), source, 31, 0);
  const uint32_t hi = vx_shfl_idx(static_cast<uint32_t>(value >> 32), source, 31, 0);
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

#endif

#if !defined(SG25_THETA_ISA) || !defined(SG25_RHOPI_ISA)
static inline uint64_t rotate(uint64_t value, unsigned shift) {
  return (value << shift) | (value >> ((64 - shift) & 63));
}

#endif

static inline __attribute__((always_inline)) uint64_t keccak_round(
    uint64_t a, unsigned lane, unsigned round) {
  // Padding lanes follow valid indices but never contribute to a state lane.
#if !defined(SG25_THETA_ISA) || !defined(SG25_RHOPI_ISA) || !defined(SG25_CHII_ISA)
  const unsigned t = lane % SG25_WORDS;
  const unsigned x = t % 5;
  const unsigned y = t / 5;
#else
  (void)lane;
#endif
#ifdef SG25_THETA_ISA
  const uint32_t lo = static_cast<uint32_t>(a);
  const uint32_t hi = static_cast<uint32_t>(a >> 32);
  const uint32_t theta_lo = vx_ktheta_l_sg25(lo, hi);
  const uint32_t theta_hi = vx_ktheta_h_sg25(lo, hi);
  a = (static_cast<uint64_t>(theta_hi) << 32) | theta_lo;
#else
  const uint64_t pair = a ^ shuffle(a, x + 5 * ((y + 1) % 5));
  const uint64_t four = pair ^ shuffle(pair, x + 5 * ((y + 2) % 5));
  const uint64_t column = four ^ shuffle(a, x + 5 * ((y + 4) % 5));
  a ^= shuffle(column, (x + 4) % 5)
     ^ rotate(shuffle(column, (x + 1) % 5), 1);
#endif

#ifdef SG25_RHOPI_ISA
  const uint32_t rho_lo = vx_krhopi_l_sg25(uint32_t(a), uint32_t(a >> 32));
  const uint32_t rho_hi = vx_krhopi_h_sg25(uint32_t(a), uint32_t(a >> 32));
  const uint64_t b = (uint64_t(rho_hi) << 32) | rho_lo;
#else
  const uint64_t rotated = rotate(a, rho[t]);
  const uint64_t b = shuffle(rotated, (x + 3 * y) % 5 + 5 * x);
#endif
#ifdef SG25_CHII_ISA
  const uint32_t chi_lo = vx_kchii_l_sg25(uint32_t(b), round);
  const uint32_t chi_hi = vx_kchii_h_sg25(uint32_t(b >> 32), round);
  return (uint64_t(chi_hi) << 32) | chi_lo;
#else
  a = b ^ (~shuffle(b, (x + 1) % 5 + 5 * y)
         & shuffle(b, (x + 2) % 5 + 5 * y));
  const uint64_t mask = 0ULL - static_cast<uint64_t>(lane == 0);
  return a ^ (round_constants[round] & mask);
#endif
}
#endif

#ifdef SG25_KROUND_ISA
#define SG25_KROUND_STEP(round) do {                                      \
  const uint32_t lo = static_cast<uint32_t>(a);                           \
  const uint32_t hi = static_cast<uint32_t>(a >> 32);                     \
  const uint32_t out_lo = vx_kround_l_sg25(lo, hi, round);                \
  const uint32_t out_hi = vx_kround_h_sg25(lo, hi, round);                \
  a = (static_cast<uint64_t>(out_hi) << 32) | out_lo;                     \
} while (0)

static __attribute__((noinline)) uint64_t kround_dispatch(
    uint64_t a, unsigned round) {
#define SG25_KROUND_CASE(r) case r: SG25_KROUND_STEP(r); break
  switch (round) {
    SG25_KROUND_CASE(0);  SG25_KROUND_CASE(1);
    SG25_KROUND_CASE(2);  SG25_KROUND_CASE(3);
    SG25_KROUND_CASE(4);  SG25_KROUND_CASE(5);
    SG25_KROUND_CASE(6);  SG25_KROUND_CASE(7);
    SG25_KROUND_CASE(8);  SG25_KROUND_CASE(9);
    SG25_KROUND_CASE(10); SG25_KROUND_CASE(11);
    SG25_KROUND_CASE(12); SG25_KROUND_CASE(13);
    SG25_KROUND_CASE(14); SG25_KROUND_CASE(15);
    SG25_KROUND_CASE(16); SG25_KROUND_CASE(17);
    SG25_KROUND_CASE(18); SG25_KROUND_CASE(19);
    SG25_KROUND_CASE(20); SG25_KROUND_CASE(21);
    SG25_KROUND_CASE(22); SG25_KROUND_CASE(23);
  }
  return a;
}
#undef SG25_KROUND_CASE
#endif

static __attribute__((noinline)) uint64_t permute(uint64_t a, unsigned lane) {
#ifdef SG25_KROUND_ISA
  (void)lane;
  SG25_KROUND_STEP(0);  SG25_KROUND_STEP(1);
  SG25_KROUND_STEP(2);  SG25_KROUND_STEP(3);
  SG25_KROUND_STEP(4);  SG25_KROUND_STEP(5);
  SG25_KROUND_STEP(6);  SG25_KROUND_STEP(7);
  SG25_KROUND_STEP(8);  SG25_KROUND_STEP(9);
  SG25_KROUND_STEP(10); SG25_KROUND_STEP(11);
  SG25_KROUND_STEP(12); SG25_KROUND_STEP(13);
  SG25_KROUND_STEP(14); SG25_KROUND_STEP(15);
  SG25_KROUND_STEP(16); SG25_KROUND_STEP(17);
  SG25_KROUND_STEP(18); SG25_KROUND_STEP(19);
  SG25_KROUND_STEP(20); SG25_KROUND_STEP(21);
  SG25_KROUND_STEP(22); SG25_KROUND_STEP(23);
#else
#pragma clang loop unroll(disable)
  for (unsigned round = 0; round < SG25_ROUNDS; ++round) {
    a = keccak_round(a, lane, round);
  }
#endif
  return a;
}

static inline uint64_t load_word(const uint8_t* input, unsigned offset, unsigned bytes) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) {
    if (offset + i < bytes) {
      value |= static_cast<uint64_t>(input[offset + i]) << (8 * i);
    }
  }
  return value;
}

static __attribute__((noinline)) void shake(
    const uint8_t* input, uint8_t* output, unsigned input_bytes,
    unsigned output_bytes, unsigned rate, unsigned lane) {
  uint64_t a = 0;
  const unsigned offset = 8 * lane;
  while (input_bytes >= rate) {
    a ^= load_word(input, offset, rate);
    a = permute(a, lane);
    input += rate;
    input_bytes -= rate;
  }
  a ^= load_word(input, offset, input_bytes);
  if (lane == input_bytes / 8) {
    a ^= 0x1fULL << (8 * (input_bytes % 8));
  }
  if (lane == rate / 8 - 1) {
    a ^= 0x8000000000000000ULL;
  }
  a = permute(a, lane);

  while (output_bytes != 0) {
    const unsigned bytes = output_bytes < rate ? output_bytes : rate;
    for (unsigned i = 0; i < 8; ++i) {
      if (offset + i < bytes) {
        output[offset + i] = static_cast<uint8_t>(a >> (8 * i));
      }
    }
    output += bytes;
    output_bytes -= bytes;
    if (output_bytes != 0) {
      a = permute(a, lane);
    }
  }
}

static __attribute__((noinline)) void benchmark(kernel_arg_t* arg, unsigned lane,
                                                unsigned state) {
  auto input = reinterpret_cast<const uint64_t*>(arg->input_addr);
  auto output = reinterpret_cast<uint64_t*>(arg->output_addr);
  auto timing = reinterpret_cast<sg25_timing_t*>(arg->timing_addr);
  uint64_t a = 0;
  if (lane < SG25_WORDS) {
    a = input[state * SG25_WORDS + lane];
  }
  const unsigned permutations = arg->permutations;
  // Keep other warps' input/output traffic outside the timed batch.
  __syncthreads();
  const uint64_t start = vx_rdcycle_sync();
#pragma clang loop unroll(disable)
  for (unsigned i = 0; i < permutations; ++i) {
    a = permute(a, lane);
  }
  const uint64_t end = vx_rdcycle_sync();
  __syncthreads();
  if (lane < SG25_WORDS) {
    output[state * SG25_WORDS + lane] = a;
  }
  if (lane == 0) {
    timing[state] = {start, end};
  }
}

#ifdef SG25_ISE
template <unsigned Op>
static __attribute__((noinline)) uint32_t stage_instruction(
    uint32_t first, uint32_t second, unsigned variant) {
  uint32_t result;
  switch (variant) {
  case 1:
    result = first;
    __asm__ volatile (".insn r 0x0b, %2, 6, %0, %0, %1" : "+r"(result) : "r"(second), "i"(Op));
    break;
  case 2:
    result = second;
    __asm__ volatile (".insn r 0x0b, %2, 6, %0, %1, %0" : "+r"(result) : "r"(first), "i"(Op));
    break;
  case 3:
    __asm__ volatile (".insn r 0x0b, %2, 6, %0, %1, %1" : "=r"(result) : "r"(first), "i"(Op));
    break;
  case 4:
    __asm__ volatile (".insn r 0x0b, %2, 6, %0, x0, %1" : "=r"(result) : "r"(second), "i"(Op));
    break;
  case 5:
    __asm__ volatile (".insn r 0x0b, %2, 6, %0, %1, x0" : "=r"(result) : "r"(first), "i"(Op));
    break;
  default:
    if (variant == 6) {
      __asm__ volatile (".insn r 0x0b, %2, 6, x0, %0, %1" :: "r"(first), "r"(second), "i"(Op));
    }
    __asm__ volatile (".insn r 0x0b, %3, 6, %0, %1, %2" : "=r"(result) : "r"(first), "r"(second), "i"(Op));
    break;
  }
  return result;
}

static __attribute__((noinline)) void test_stage(kernel_arg_t* arg, unsigned lane,
                                               unsigned state) {
  auto input = reinterpret_cast<const uint64_t*>(arg->input_addr);
  auto output = reinterpret_cast<uint64_t*>(arg->output_addr);
  const uint64_t a = input[state * 32 + lane];
  uint32_t lo = uint32_t(a), hi = uint32_t(a >> 32);
  uint32_t result_lo, result_hi;
  const unsigned variant = arg->stage_variant;
  if (arg->mode == SG25_MODE_THETA) {
    result_lo = stage_instruction<0>(lo, hi, variant);
    result_hi = stage_instruction<1>(lo, hi, variant);
  } else if (arg->mode == SG25_MODE_RHOPI) {
    result_lo = stage_instruction<2>(lo, hi, variant);
    result_hi = stage_instruction<3>(lo, hi, variant);
  } else {
    const unsigned round = arg->round;
    // rs1=rs2 must still supply a uniform, valid round operand.
    if (variant == 3) lo = hi = round;
    result_lo = stage_instruction<4>(lo, round, variant);
    result_hi = stage_instruction<5>(hi, round, variant);
  }
  output[state * 32 + lane] = (uint64_t(result_hi) << 32) | result_lo;
}
#endif

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const unsigned lane = vx_thread_id();
  const unsigned state = blockIdx.x;
  if (arg->mode == SG25_MODE_TRACE) {
    auto input = reinterpret_cast<const uint64_t*>(arg->input_addr);
    auto output = reinterpret_cast<uint64_t*>(arg->output_addr);
    uint64_t a = 0;
    if (lane < SG25_WORDS) {
      a = input[state * SG25_WORDS + lane];
    }
    // Unrolling the guarded stores exceeds the compiler's divergence-pass BB limit.
#pragma clang loop unroll(disable)
    for (unsigned round = 0; round < SG25_ROUNDS; ++round) {
#ifdef SG25_KROUND_ISA
      a = kround_dispatch(a, round);
#else
      a = keccak_round(a, lane, round);
#endif
      if (lane < SG25_WORDS) {
        output[(state * SG25_ROUNDS + round) * SG25_WORDS + lane] = a;
      }
    }
  } else if (arg->mode == SG25_MODE_BENCH) {
    benchmark(arg, lane, threadIdx.x / 32);
#ifdef SG25_ISE
  } else if (arg->mode >= SG25_MODE_THETA) {
    test_stage(arg, lane, state);
#endif
  } else {
    auto cases = reinterpret_cast<const sg25_case_t*>(arg->cases_addr);
    const sg25_case_t test = cases[state];
    auto input = reinterpret_cast<const uint8_t*>(arg->input_addr) + test.input_offset;
    auto output = reinterpret_cast<uint8_t*>(arg->output_addr) + test.output_offset;
    shake(input, output, test.input_bytes, test.output_bytes, test.rate, lane);
  }
}

#ifdef SG25_KROUND_ISA
#undef SG25_KROUND_STEP
#endif
