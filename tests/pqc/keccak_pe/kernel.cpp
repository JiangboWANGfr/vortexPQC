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

  const uint64_t t0 = vx_rdcycle();
  for (unsigned p = 0; p < arg->perms; ++p) {
    // ACQUIRE boundary: a CPU store into the state with nothing between it and
    // the instruction.
    for (unsigned j = 0; j < KP_WORDS; ++j) {
      s[j] ^= KP_XOR_K;
    }
    vx_keccakf(s);
    // RELEASE boundary: the program reads back every word the engine wrote.
    // Not removable by the compiler -- s[0] depends on all of them.
    uint64_t acc = 0;
    for (unsigned j = 0; j < KP_WORDS; ++j) {
      acc ^= s[j];
    }
    s[0] ^= acc;
  }
  const uint64_t t1 = vx_rdcycle();

  for (unsigned j = 0; j < KP_WORDS; ++j) {
    out[hart * KP_WORDS + j] = s[j];
  }
  if (hart == 0) {
    cycles[KP_CY_RUN] = t1 - t0;
  }
}
