// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MLK_PHASE_PROFILE_H
#define MLK_PHASE_PROFILE_H

#include "mlk_prof_counters.h"

#if defined(__VORTEX__) && defined(PQC_PROFILE_PHASES)
enum {
  MLK_PHASE_PERMUTE,
  MLK_PHASE_ABSORB,
  MLK_PHASE_SQUEEZE,
  MLK_PHASE_NTT,
  MLK_PHASE_INTT,
  MLK_PHASE_COUNT
};

typedef struct {
  uint64_t start;
  uint64_t permute;
} mlk_phase_scope_t;

static uint64_t mlk_phase_cycles[VX_CFG_NUM_WARPS][MLK_PHASE_COUNT];

static inline uint64_t mlk_phase_timestamp() {
  asm volatile ("" ::: "memory");
  const uint64_t value = vx_rdcycle();
  asm volatile ("" ::: "memory");
  return value;
}

static inline mlk_phase_scope_t mlk_phase_begin() {
  mlk_phase_scope_t scope = {
    mlk_phase_timestamp(),
    mlk_phase_cycles[vx_warp_id()][MLK_PHASE_PERMUTE]
  };
  return scope;
}

static inline void mlk_phase_end(unsigned phase, mlk_phase_scope_t scope) {
  const uint64_t elapsed = mlk_phase_timestamp() - scope.start;
  if (vx_thread_id() == 0) {
    mlk_phase_cycles[vx_warp_id()][phase] += elapsed;
  }
}

static inline void mlk_phase_end_excluding_permute(
    unsigned phase, mlk_phase_scope_t scope) {
  const uint64_t elapsed = mlk_phase_timestamp() - scope.start;
  if (vx_thread_id() == 0) {
    const uint64_t permute =
        mlk_phase_cycles[vx_warp_id()][MLK_PHASE_PERMUTE] - scope.permute;
    mlk_phase_cycles[vx_warp_id()][phase] += elapsed - permute;
  }
}
#endif

#endif
