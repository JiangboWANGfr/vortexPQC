// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include <vx_pqc.h>

#include "common.h"

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto out    = reinterpret_cast<uint64_t*>(arg->states_addr);
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr);

  const unsigned tid  = (unsigned)vx_thread_id();
  const unsigned hart = blockIdx.x * arg->threads + tid;
  if (hart >= arg->nharts) {
    return;
  }

  // The state is a stack automatic, so every hart's lives in its own 8 KB slab
  // (vx_start.S: sp = STACK_BASE - (mhartid << STACK_LOG2_SIZE)). Distinct
  // addresses per hart is the whole point: the acquire bug showed up as one
  // hart reading words another had already written.
  uint64_t s[KP_WORDS];
  for (unsigned j = 0; j < KP_WORDS; ++j) {
    s[j] = kp_seed(hart, j);
  }

  // Cache the loop bound. Reading arg->perms through the uniform pointer every
  // iteration puts a global load inside the timed loop, and its cost swamps what
  // this benchmark is trying to measure -- it was worth 70-85 cycles a
  // permutation of simx/rtlsim disagreement that looked like a model defect.
  const unsigned perms = arg->perms;
  const uint64_t t0 = vx_rdcycle();
  for (unsigned p = 0; p < perms; ++p) {
    // ACQUIRE boundary: a CPU store into the state with nothing between it and
    // the instruction.
    for (unsigned j = 0; j < KP_WORDS; ++j) {
      s[j] ^= KP_XOR_K;
    }
    vx_keccakf(s);
    // Preserve all output bits while forcing an immediate read of every word.
    uint64_t acc = 0;
    for (unsigned j = 0; j < KP_WORDS; ++j) {
      acc ^= s[j];
    }
    s[0] = acc;
  }
  const uint64_t t1 = vx_rdcycle();

  for (unsigned j = 0; j < KP_WORDS; ++j) {
    out[hart * KP_WORDS + j] = s[j];
  }
  cycles[KP_CY_T0(hart)] = t0;
  cycles[KP_CY_T1(hart)] = t1;
}
