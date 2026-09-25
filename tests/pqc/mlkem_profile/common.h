#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <mlkem_native.h>
#include "mlk_prof_counters.h"

// The ML-KEM buffers stay in device memory, same reason as the mlkem test: the
// key material alone is ~4.7 KB against an 8 KB per-thread stack.
typedef struct {
  uint64_t scratch_addr;  // in/out : MLKEM working buffers
  uint64_t counts_addr;   // out    : MLK_PROF_COUNT * uint32_t per request
  uint64_t status_addr;   // out    : 3 * int32_t per request
  uint64_t cycles_addr;   // out    : P_CYCLE_COUNT * uint64_t per request
  uint32_t requests;
  uint32_t workers;
  uint32_t ntt_lanes;
} kernel_arg_t;

enum {
  P_CYCLE_KEYPAIR,
  P_CYCLE_ENCAPS,
  P_CYCLE_DECAPS,
  P_CYCLE_START,
  P_CYCLE_END,
#if defined(PQC_PROFILE_PHASES)
  P_CYCLE_PERMUTE,
  P_CYCLE_ABSORB,
  P_CYCLE_SQUEEZE,
  P_CYCLE_NTT,
  P_CYCLE_INTT,
  P_CYCLE_REJECTION,
  P_CYCLE_CODEC,
  P_CYCLE_NOISE,
  P_CYCLE_LINEAR,
  P_CYCLE_ZEROIZE,
#endif
#if defined(PQC_PROFILE_ARITH)
  P_CYCLE_MULCACHE,
  P_CYCLE_BASEMUL,
  P_CYCLE_REDUCE,
#endif
  P_CYCLE_COUNT
};

// Keep each section aligned for coalesced device accesses.
#define P_ALIGN64(n) (((n) + 63u) & ~63u)
#define P_OFF_COINS_KP  0
#define P_OFF_COINS_ENC 64
#define P_OFF_PK        128
#define P_OFF_SK        P_ALIGN64(P_OFF_PK + MLKEM_PUBLICKEYBYTES(MLK_CONFIG_PARAMETER_SET))
#define P_OFF_CT        P_ALIGN64(P_OFF_SK + MLKEM_SECRETKEYBYTES(MLK_CONFIG_PARAMETER_SET))
#define P_OFF_SS_ENC    P_ALIGN64(P_OFF_CT + MLKEM_CIPHERTEXTBYTES(MLK_CONFIG_PARAMETER_SET))
#define P_OFF_SS_DEC    (P_OFF_SS_ENC + 64)
#define P_SCRATCH_LEN   (P_OFF_SS_DEC + 64)

#endif
