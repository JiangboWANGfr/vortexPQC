// Call counters for the Keccak width sweep. Shared by the SIMT FIPS-202
// backend and the kernel that reads them out.
#ifndef MLK_WIDTH_COUNTERS_H
#define MLK_WIDTH_COUNTERS_H

#include <stdint.h>

enum {
  MLKW_KECCAK_X1 = 0,   // serial Keccak-f1600 permutations (one state)
  MLKW_KECCAK_X4,       // x4-API permutation calls (one "slot")
  MLKW_SLOTS,           // lane-groups actually issued = sum of ceil(4/W) per x4 call
  MLKW_X1_KP,           // x1 permutations at end of keypair (cumulative)
  MLKW_X1_ENC,          //                        end of encaps
  MLKW_X1_DEC,          //                        end of decaps
  MLKW_X4_KP,
  MLKW_X4_ENC,
  MLKW_X4_DEC,
  MLKW_COUNT
};

// One row per warp. The lanes of a warp are lockstep and all store the same
// value, so a shared row is +1 per warp -- but two warps running different CTAs
// are not lockstep with each other, and a single shared row would be a genuine
// race the moment -b exceeds 1.
#define MLKW_MAX_WARPS 8

#if defined(__VORTEX__)
static uint32_t mlkw_counts[MLKW_MAX_WARPS][MLKW_COUNT];
#endif

#endif
