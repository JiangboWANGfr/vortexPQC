// Keccak call counters for the ML-DSA lane-width sweep. One row per warp: the
// lanes of a warp are lockstep and all store the same value, so a shared row is
// +1 per warp, but two warps running different requests are not lockstep and a
// single shared row would be a real race.
#ifndef MLD_SIMT_COUNTERS_H
#define MLD_SIMT_COUNTERS_H

#include <stdint.h>

enum {
  MLDW_KECCAK_X1 = 0,   // serial Keccak-f1600 permutations (one state)
  MLDW_KECCAK_X4,       // x4-API calls, i.e. lane-groups of four states
  MLDW_SLOTS,           // sequential permutation steps = sum of ceil(4/W)
  MLDW_COUNT
};

#define MLDW_MAX_WARPS 8

#if defined(__VORTEX__)
static uint32_t mldw_counts[MLDW_MAX_WARPS][MLDW_COUNT];
#endif

#endif
