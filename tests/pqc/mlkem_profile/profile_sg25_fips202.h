// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#ifndef MLK_PROFILE_SG25_FIPS202_H
#define MLK_PROFILE_SG25_FIPS202_H

#include "mlk_prof_counters.h"

#define MLKSG_COUNT_PERMUTATIONS(permutations) do {                       \
  if (vx_thread_id() == 0) {                                             \
    mlk_prof_counts[vx_hart_id()][MLK_PROF_KECCAK_X1] += (permutations); \
  }                                                                      \
} while (0)
#include "sg25_fips202.h"
#undef MLKSG_COUNT_PERMUTATIONS

#endif
