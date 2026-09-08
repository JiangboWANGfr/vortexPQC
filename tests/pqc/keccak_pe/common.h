// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// KECCAKF microbenchmark: the gateable parity vehicle, and the multi-warp
// ordering probe that keccak_sg5 structurally cannot be.
//
// keccak_sg5 has no warp axis -- its -t is lanes inside one warp -- so its
// twenty ordering probes all exercise a single caller, and they passed both
// before and after the acquire bug was found. The bug lives above one warp.
// This test therefore takes -b blocks as well as -t threads, and every hart
// runs the shape that makes both boundaries observable in one loop:
//
//   for p in 0..perms-1:
//       for j: s[j] ^= K          <- a CPU store with nothing between it and
//       vx_keccakf(s)                the instruction: the ACQUIRE boundary
//       acc = XOR s[j]; s[0] ^= acc  <- the program reads every word the engine
//                                      just wrote: the RELEASE boundary
//
// which is exactly what a sponge round does (xor_bytes, permute, extract_bytes)
// and is mirrored word for word on the host.

#ifndef KP_COMMON_H
#define KP_COMMON_H

#include <stdint.h>

#define KP_WORDS 25
#define KP_XOR_K 0x9e3779b97f4a7c15ull

typedef struct {
  uint64_t states_addr;  // out : nharts * 25 * uint64_t
  uint64_t cycles_addr;  // out : KP_CY_COUNT * uint64_t
  uint32_t perms;        // in  : chained permutations per hart
  uint32_t nharts;       // in  : blocks * threads
  uint32_t threads;      // in  : block_dim, for the global hart id
  uint32_t pad;
} kernel_arg_t;

#define KP_CY_RUN   0   // cycles for the whole permutation loop, hart 0
#define KP_CY_COUNT 1

// One seed per hart and word. Distinct per hart so a state written to the wrong
// hart's slot fails instead of matching.
static inline uint64_t kp_seed(uint32_t hart, uint32_t word) {
  return 0x0123456789abcdefull * (uint64_t)(hart + 1)
       + 0x9e3779b97f4a7c15ull * (uint64_t)word;
}

#endif
