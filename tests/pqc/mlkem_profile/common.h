#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include "mlk_prof_counters.h"

// The ML-KEM buffers stay in device memory, same reason as the mlkem test: the
// key material alone is ~4.7 KB against an 8 KB per-thread stack.
typedef struct {
  uint64_t scratch_addr;  // in/out : MLKEM working buffers
  uint64_t counts_addr;   // out    : MLK_PROF_COUNT * uint32_t per request
  uint64_t status_addr;   // out    : 3 * int32_t per request
  uint64_t cycles_addr;   // out    : P_CYCLE_COUNT * uint64_t per request
  uint32_t requests;
  uint32_t ntt_lanes;
} kernel_arg_t;

enum {
  P_CYCLE_KEYPAIR,
  P_CYCLE_ENCAPS,
  P_CYCLE_DECAPS,
  P_CYCLE_START,
  P_CYCLE_END,
#if defined(PQC_PROFILE_ARITH)
  P_CYCLE_MULCACHE,
  P_CYCLE_BASEMUL,
  P_CYCLE_REDUCE,
#endif
  P_CYCLE_COUNT
};

// Scratch layout for ML-KEM-768, 64-byte aligned sections.
#define P_OFF_COINS_KP  0      // 64
#define P_OFF_COINS_ENC 64     // 32
#define P_OFF_PK        128    // 1184
#define P_OFF_SK        1344   // 2400
#define P_OFF_CT        3776   // 1088
#define P_OFF_SS_ENC    4864   // 32
#define P_OFF_SS_DEC    4928   // 32
#define P_SCRATCH_LEN   4992

#endif
