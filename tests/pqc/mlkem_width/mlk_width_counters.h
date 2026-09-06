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

#if defined(__VORTEX__)
// .bss is one address space shared by every hart, so these are warp-wide.
// All active lanes execute the same load-add-store on the same address and
// store the same value, so the net effect of a lockstep increment is +1.
static uint32_t mlkw_counts[MLKW_COUNT];
#endif

#endif
