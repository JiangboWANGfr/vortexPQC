#ifndef _COMMON_H_
#define _COMMON_H_
#include <stdint.h>
#include <VX_config.h>

// One ML-KEM-768 forward NTT, computed twice: once by the library's own C, once
// by an L-lane cooperative version, then compared coefficient by coefficient.
//
// This is the software half of the NTT ISA comparison. Measuring a cooperative
// NTT instruction against a single-lane software NTT would fold "we parallelised
// it" into "the instruction helped"; the two have to be separated, and that
// needs a cooperative software NTT to exist first.

#define NTT_N 256

typedef struct {
  uint64_t poly_addr;    // in/out : NTT_N * int16_t, the cooperative result
  uint64_t ref_addr;     // out    : NTT_N * int16_t, the library's own result
  uint64_t cycles_addr;  // out    : 4 * uint64_t {coop, ref, span, stack peak}
  uint64_t mism_addr;    // out    : 1 * uint32_t, coefficients that differ
  uint32_t lanes;        // in     : lanes cooperating on the one transform
} kernel_arg_t;

#define NTT_CY_COOP  0
#define NTT_CY_REF   1
#define NTT_CY_SPAN  2
#define NTT_CY_STACK 3
#define NTT_CY_COUNT 4

#endif
