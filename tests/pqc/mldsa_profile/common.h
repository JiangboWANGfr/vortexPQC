#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <mldsa_native.h>
#include "mld_prof_counters.h"

#define MLDSA_PK_BYTES   MLDSA_PUBLICKEYBYTES(MLD_CONFIG_PARAMETER_SET)
#define MLDSA_SK_BYTES   MLDSA_SECRETKEYBYTES(MLD_CONFIG_PARAMETER_SET)
#define MLDSA_SIG_BYTES  MLDSA_BYTES(MLD_CONFIG_PARAMETER_SET)
#define MLDSA_MSG_BYTES  32

#define MLDSA_ST_KEYPAIR 0
#define MLDSA_ST_SIGN    1
#define MLDSA_ST_VERIFY  2
#define MLDSA_ST_COUNT   3

#define MLDSA_CY_KEYPAIR 0
#define MLDSA_CY_SIGN    1
#define MLDSA_CY_VERIFY  2
#define MLDSA_CY_START   3
#define MLDSA_CY_END     4
#define MLDSA_CY_COUNT   5

#define MLDSA_POINTWISE_CY_COUNT (MLDSA_ST_COUNT * 2)
#if defined(PQC_PROFILE_PHASES)
#define MLDSA_DETAIL_CY_COUNT (MLDSA_ST_COUNT * MLD_PHASE_COUNT)
#else
#define MLDSA_DETAIL_CY_COUNT 0
#endif
#define MLDSA_PROFILE_CY_COUNT (MLDSA_POINTWISE_CY_COUNT + MLDSA_DETAIL_CY_COUNT)

typedef struct {
  // Buffer dimensions below are per request.
  uint64_t seed_addr;     // in  : MLDSA_SEEDBYTES
  uint64_t rnd_addr;      // in  : MLDSA_RNDBYTES
  uint64_t msg_addr;      // in  : MLDSA_MSG_BYTES
  uint64_t pk_addr;       // out : MLDSA_PK_BYTES
  uint64_t sk_addr;       // out : MLDSA_SK_BYTES
  uint64_t sig_addr;      // out : MLDSA_SIG_BYTES
  uint64_t status_addr;   // out : MLDSA_ST_COUNT * int32_t
  uint64_t cycles_addr;   // out : MLDSA_CY_COUNT * uint64_t
  uint64_t arena_addr;    // out : 4 * uint32_t -- arena peak/fail, stack peak/span
  uint64_t counts_addr;   // out : MLD_PROF_COUNT * uint32_t per request
  uint64_t pointwise_cycles_addr; // out : MLDSA_PROFILE_CY_COUNT * uint64_t
  uint32_t requests;
} kernel_arg_t;

#endif
