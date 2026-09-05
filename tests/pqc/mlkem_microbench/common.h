#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>

// One slot per primitive. Names mirror the library's, so a row in the output
// maps onto a function you can go read.
#define MB_KECCAK_F1600  0
#define MB_POLY_NTT      1
#define MB_POLY_INVNTT   2
#define MB_POLY_REJ_UNIF 3
#define MB_COUNT         4

typedef struct {
  uint32_t iters;         // repetitions per primitive
  uint64_t cycles_addr;   // out : MB_COUNT * uint64_t, total over iters
  uint64_t scratch_addr;  // in/out : working buffers, kept off the 8 KB stack
} kernel_arg_t;

// Scratch layout, 64-byte aligned sections.
#define MB_OFF_STATE   0     // uint64_t[25]  Keccak state
#define MB_OFF_POLY    256   // int16_t[256]  one polynomial
#define MB_OFF_SEED    832   // uint8_t[34]   rej_uniform seed
#define MB_SCRATCH_LEN 896

#endif
