#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <mldsa_native.h>

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
#define MLDSA_CY_COUNT   3

// Every output buffer is an array of `requests` slices; the inputs are shared,
// read-only and identical for every request. Signing here is derandomised
// (MLD_CONFIG_NO_RANDOMIZED_API, explicit rnd), so identical inputs must give
// byte-identical outputs -- which is what makes cross-request interference
// visible instead of letting the whole batch agree on the same wrong answer.
typedef struct {
  uint64_t seed_addr;     // in  : MLDSA_SEEDBYTES, shared
  uint64_t rnd_addr;      // in  : MLDSA_RNDBYTES, shared
  uint64_t msg_addr;      // in  : MLDSA_MSG_BYTES, shared
  uint64_t pk_addr;       // out : requests * MLDSA_PK_BYTES
  uint64_t sk_addr;       // out : requests * MLDSA_SK_BYTES
  uint64_t sig_addr;      // out : requests * MLDSA_SIG_BYTES
  uint64_t status_addr;   // out : requests * MLDSA_ST_COUNT * int32_t
  uint64_t cycles_addr;   // out : requests * MLDSA_CY_COUNT * uint64_t
  uint64_t arena_addr;    // out : requests * MLDSA_AR_COUNT * uint32_t
  uint32_t requests;      // in  : independent keypair->sign->verify chains
} kernel_arg_t;

// arena_addr slots
#define MLDSA_AR_PEAK       0
#define MLDSA_AR_FAIL       1
#define MLDSA_AR_STACK_PEAK 2
#define MLDSA_AR_STACK_SPAN 3
#define MLDSA_AR_COUNT      4

#endif
