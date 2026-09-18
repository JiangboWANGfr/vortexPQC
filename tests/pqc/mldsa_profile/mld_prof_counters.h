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
  MLD_PROF_POINTWISE,
  MLD_PROF_POINTWISE_L5,
  // Not a call count: a bitmask of the arm the DEVICE was actually built with.
  // A stale copied Makefile in build32 silently drops KECCAK=pe -- the guard
  // `$(error KECCAK must be pe or unset)` lives in the file that did not get
  // copied, and the host is built from the same stale file, so both sides agree
  // on being wrong. The only witness is the device saying what it is, so the
  // operator can see "asked for pe, got base" instead of reading a PE arm that
  // reproduces its own baseline to the cycle as a result.
  MLD_PROF_ARM,
  MLD_PROF_COUNT
};

enum {
  MLD_ARM_KECCAK_PE = 1,
  MLD_ARM_ABLATE_KECCAK = 2,
  MLD_ARM_ABLATE_NTT = 4,
  MLD_ARM_KECCAK_SG25 = 8,
  MLD_ARM_KECCAK_SG25_SW = 16,
  MLD_ARM_KECCAK_KROUND25 = 32,
  MLD_ARM_KECCAK_UNROLL = 64,
  MLD_ARM_NTT_REG32 = 128,
  MLD_ARM_NTTMUL_D = 256,
  MLD_ARM_NTTBF_D = 512,
  MLD_ARM_POINTWISE_ISE = 1024,
  MLD_ARM_POINTWISE_L5_W32 = 2048,
  MLD_ARM_EXPECTED =
#if defined(PQC_KECCAK_PE)
      MLD_ARM_KECCAK_PE |
#endif
#if defined(PQC_ABLATE_KECCAK)
      MLD_ARM_ABLATE_KECCAK |
#endif
#if defined(PQC_ABLATE_NTT)
      MLD_ARM_ABLATE_NTT |
#endif
#if defined(PQC_KECCAK_SG25)
      MLD_ARM_KECCAK_SG25 |
#endif
#if defined(PQC_KECCAK_SG25_SW)
      MLD_ARM_KECCAK_SG25_SW |
#endif
#if defined(PQC_KECCAK_KROUND25)
      MLD_ARM_KECCAK_KROUND25 |
#endif
#if defined(PQC_KECCAK_UNROLL)
      MLD_ARM_KECCAK_UNROLL |
#endif
#if defined(PQC_NTT_REG32)
      MLD_ARM_NTT_REG32 |
#endif
#if defined(PQC_NTTMUL_D)
      MLD_ARM_NTTMUL_D |
#endif
#if defined(PQC_NTTBF_D)
      MLD_ARM_NTTBF_D |
#endif
#if defined(PQC_POINTWISE_ISE)
      MLD_ARM_POINTWISE_ISE |
#endif
#if defined(PQC_POINTWISE_L5_W32)
      MLD_ARM_POINTWISE_L5_W32 |
#endif
      0
};

#if defined(__VORTEX__)
#include <VX_config.h>
#include <vx_intrinsics.h>
#define MLD_PROF_SLOTS (VX_CFG_NUM_CLUSTERS * VX_CFG_NUM_CORES * VX_CFG_NUM_WARPS)
static uint32_t mld_prof_counts[MLD_PROF_SLOTS][MLD_PROF_COUNT];
static uint64_t mld_prof_pointwise_cycles[MLD_PROF_SLOTS][3][2];
static unsigned mld_prof_phase[MLD_PROF_SLOTS];
static inline unsigned mld_prof_slot(void) {
  return (unsigned)vx_hart_id() / VX_CFG_NUM_THREADS;
}
#endif

#endif /* MLD_PROF_COUNTERS_H */
