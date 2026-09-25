#ifndef SHARED_NTT_COMMON_H
#define SHARED_NTT_COMMON_H
#include <stdint.h>
#include <VX_config.h>

#if defined(PQC_NTT_D)
typedef int32_t ntt_coeff_t;
#else
typedef int16_t ntt_coeff_t;
#endif

#define NTT_N 256
#define NTT_MAX_REQUESTS 8
#define NTT_CASES 5
#define NTT_DIRECTIONS 2

typedef struct {
  uint64_t poly_addr;
  uint64_t ref_addr;
  uint64_t results_addr;
  uint32_t lanes;
  uint32_t requests;
  uint32_t inverse;
  uint32_t reference;
  uint32_t sample;
} kernel_arg_t;

typedef struct {
  uint64_t coop_start;
  uint64_t coop_end;
  uint64_t ref_start;
  uint64_t ref_end;
  uint32_t mismatches;
  uint32_t stack_span;
  uint32_t stack_peak;
} ntt_result_t;

#endif
