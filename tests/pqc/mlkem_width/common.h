#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include "mlk_width_counters.h"

typedef struct {
  uint64_t scratch_addr;  // in/out : MLKEM working buffers
  uint64_t counts_addr;   // out    : MLKW_COUNT * uint32_t
  uint64_t status_addr;   // out    : 3 * int32_t, one per KEM step
  uint64_t cycles_addr;   // out    : 3 * uint64_t, one per KEM step
  uint64_t stack_addr;    // out    : 1 * uint32_t, peak stack bytes (lane 0)
  uint32_t lanes;         // in     : lanes the launch made active
} kernel_arg_t;

// Scratch layout for ML-KEM-768, 64-byte aligned sections.
#define P_OFF_COINS_KP  0      // 64
#define P_OFF_COINS_ENC 64     // 32
#define P_OFF_PK        128    // 1184
#define P_OFF_SK        1344   // 2400
#define P_OFF_CT        3776   // 1088
#define P_OFF_SS_ENC    4864   // 32
#define P_OFF_SS_DEC    4928   // 32
#define P_SCRATCH_LEN   4992

// FNV-1a of the key material at W=1. The coins are fixed and match
// tests/pqc/mlkem_profile, so these do not depend on the lane count -- asserting
// them is what makes the wide arms comparable rather than merely non-erroring.
//
// These are an INVARIANCE anchor, captured from a run, not a KAT: the coins here
// are h_scr[i] = i rather than the FIPS 203 vectors, so there is no published
// answer to compare against. The CORRECTNESS anchor is the separate check that
// decapsulation reproduces encapsulation's shared secret, which no amount of
// consistent-but-wrong arithmetic satisfies.
#define MLKW_SUM_PK 0xb4ce96bcu
#define MLKW_SUM_SK 0xfd91150du
#define MLKW_SUM_CT 0xeef3c43eu
#define MLKW_SUM_SS 0xfe82d471u

#define MLKW_PAINT 0xa5a5a5a5u

#endif
