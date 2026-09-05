// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Call counters shared by the ML-DSA profiling backends and the kernel.
// Mirrors tests/pqc/mlkem_profile/mlk_prof_counters.h.

#ifndef MLD_PROF_COUNTERS_H
#define MLD_PROF_COUNTERS_H

#include <stdint.h>

enum {
  MLD_PROF_KECCAK_X1 = 0,
  MLD_PROF_KECCAK_X4,
  MLD_PROF_NTT,
  MLD_PROF_INTT,
  MLD_PROF_REJ_UNIFORM,
  MLD_PROF_COUNT
};

// Device-side only: one translation unit holds the library, both backends and
// the kernel. A definition on the host would be an unused variable, which this
// build treats as an error -- and, more to the point, a second copy of state
// that must be single, which is exactly the failure the ML-DSA arena hit.
#if defined(__VORTEX__)
static uint32_t mld_prof_counts[MLD_PROF_COUNT];
#endif

#endif /* MLD_PROF_COUNTERS_H */
