#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>

#define MB_KECCAK_F1600  0
#define MB_POLY_NTT      1
#define MB_POLY_INVNTT   2
#define MB_COUNT         3

typedef struct {
  uint32_t iters;
  uint64_t cycles_addr;   // out : MB_COUNT * uint64_t, total over iters
  uint64_t scratch_addr;  // in/out
} kernel_arg_t;

// ML-DSA coefficients are int32_t, so one mld_poly is 1024 bytes -- twice
// ML-KEM's. Sections stay 64-byte aligned.
#define MB_OFF_STATE   0      // uint64_t[25]
#define MB_OFF_POLY    256    // int32_t[256]
#define MB_SCRATCH_LEN 1280

#endif
