#ifndef KECCAK_SG25_MAPPING_H
#define KECCAK_SG25_MAPPING_H

extern "C" {
#include "src/common.h"
#include "src/fips202/keccakf1600.c"
#if defined(SG25_MAPPING_ASM)
void KeccakF1600_StatePermute_RV32ASM(uint64_t *state);
#endif
}

#if defined(SG25_MAPPING_SG5)
#include "../keccak_sg5/permute.h"
#if defined(SG25_SG5_BARRIER)
static_assert(VX_CFG_NUM_BARRIERS >= 2, "SG5 warp barriers reserve slot 1");
#endif
static uint64_t mapping_transpose[VX_CFG_NUM_WARPS][6][5][5];
#endif

static __attribute__((noinline)) void benchmark_mapping(
    kernel_arg_t* arg, unsigned lane, unsigned warp) {
  auto input = reinterpret_cast<const uint64_t*>(arg->input_addr);
  auto output = reinterpret_cast<uint64_t*>(arg->output_addr);
  auto timing = reinterpret_cast<sg25_timing_t*>(arg->timing_addr);
  const unsigned states = arg->states_per_warp;
  const unsigned permutations = arg->permutations;
#if defined(SG25_SG5_BARRIER)
  // Slot 0 belongs to the CTA timing barriers; slot 1 is private to each warp.
  const unsigned barrier_id = (1u << 8) | vx_warp_id();
#endif
#if defined(SG25_MAPPING_SG5)
  const unsigned group = lane / 5;
  const unsigned column = lane % 5;
  const bool active = lane < 5 * states;
  const unsigned state = warp * states + group;
  uint64_t a[5];
  if (active) {
    for (unsigned y = 0; y < 5; ++y) {
      a[y] = input[state * SG25_WORDS + column + 5 * y];
    }
  }
#else
  const bool active = lane < states;
  const unsigned state = warp * states + lane;
  uint64_t a[SG25_WORDS];
  if (active) {
    for (unsigned word = 0; word < SG25_WORDS; ++word) {
      a[word] = input[state * SG25_WORDS + word];
    }
  }
#endif
  __syncthreads();
  const uint64_t start = vx_rdcycle_sync();
  if (active) {
#pragma clang loop unroll(disable)
    for (unsigned i = 0; i < permutations; ++i) {
#if defined(SG25_MAPPING_SG5)
#if defined(SG25_SG5_BARRIER)
      ks_sg5_permute<true>(a, column, group * 5, 31,
                           mapping_transpose[warp][group], 0, barrier_id);
#else
      ks_sg5_permute(a, column, group * 5, 31,
                     mapping_transpose[warp][group], 2);
#endif
#elif defined(SG25_MAPPING_ASM)
      KeccakF1600_StatePermute_RV32ASM(a);
#else
      mlk_keccakf1600_permute(a);
#endif
    }
  }
  const uint64_t end = vx_rdcycle_sync();
  __syncthreads();
  if (active) {
#if defined(SG25_MAPPING_SG5)
    for (unsigned y = 0; y < 5; ++y) {
      output[state * SG25_WORDS + column + 5 * y] = a[y];
    }
#else
    for (unsigned word = 0; word < SG25_WORDS; ++word) {
      output[state * SG25_WORDS + word] = a[word];
    }
#endif
  }
  if (lane == 0) {
    timing[warp] = {start, end};
  }
}

#endif
