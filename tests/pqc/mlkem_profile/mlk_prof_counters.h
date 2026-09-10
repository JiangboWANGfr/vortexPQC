// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Call counters shared by the two profiling backends and read by the kernel.
// Separate header because both backends and the kernel include it, and the
// backends are pulled in from deep inside the library's own include graph.

#ifndef MLK_PROF_COUNTERS_H
#define MLK_PROF_COUNTERS_H

#include <stdint.h>

enum {
  MLK_PROF_KECCAK_X1 = 0,   // Keccak-f1600, one lane
  MLK_PROF_KECCAK_X4,       // Keccak-f1600, four lanes at once
  MLK_PROF_NTT,
  MLK_PROF_INTT,
  MLK_PROF_REJ_UNIFORM,
  MLK_PROF_MULCACHE,
  MLK_PROF_BASEMUL,
  MLK_PROF_POLY_REDUCE,
  MLK_PROF_ARM,
  MLK_PROF_COUNT
};

enum {
  MLK_ARM_KECCAK_PE = 1,
  MLK_ARM_ABLATE_KECCAK = 2,
  MLK_ARM_ABLATE_NTT = 4,
  MLK_ARM_NTT_COOP = 8,
  MLK_ARM_NTT_REG32 = 16,
  MLK_ARM_NTT_SMEM32 = 32,
  MLK_ARM_NTTMUL_K = 64,
  MLK_ARM_NTTBF_K = 128,
  MLK_ARM_EXPECTED =
#if defined(PQC_KECCAK_PE)
      MLK_ARM_KECCAK_PE |
#endif
#if defined(PQC_ABLATE_KECCAK)
      MLK_ARM_ABLATE_KECCAK |
#endif
#if defined(PQC_ABLATE_NTT)
      MLK_ARM_ABLATE_NTT |
#endif
#if defined(PQC_NTT_COOP)
      MLK_ARM_NTT_COOP |
#endif
#if defined(PQC_NTT_REG32)
      MLK_ARM_NTT_REG32 |
#endif
#if defined(PQC_NTT_SMEM32)
      MLK_ARM_NTT_SMEM32 |
#endif
#if defined(PQC_NTTMUL_K)
      MLK_ARM_NTTMUL_K |
#endif
#if defined(PQC_NTTBF_K)
      MLK_ARM_NTTBF_K |
#endif
      0
};

#if defined(__VORTEX__)
#include <VX_config.h>
#include <vx_intrinsics.h>

// Concurrent requests must not share their counter row.
static uint32_t mlk_prof_counts[VX_CFG_NUM_CLUSTERS * VX_CFG_NUM_CORES *
                                VX_CFG_NUM_WARPS * VX_CFG_NUM_THREADS][MLK_PROF_COUNT];
#endif

#endif /* MLK_PROF_COUNTERS_H */
