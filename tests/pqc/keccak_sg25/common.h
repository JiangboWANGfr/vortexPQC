#ifndef KECCAK_SG25_COMMON_H
#define KECCAK_SG25_COMMON_H

#include <stdint.h>

#define SG25_WORDS 25
#define SG25_ROUNDS 24
#define SG25_MODE_TRACE 0
#define SG25_MODE_SHAKE 1
#define SG25_MODE_BENCH 2
#define SG25_MODE_THETA 3
#define SG25_MODE_RHOPI 4
#define SG25_MODE_CHII 5
#if VX_CFG_XLEN == 64
#define SG25_STAGE_VARIANTS 1
#else
#define SG25_STAGE_VARIANTS 7
#endif

struct sg25_timing_t {
  uint64_t start;
  uint64_t end;
};

struct sg25_case_t {
  uint32_t input_offset;
  uint32_t output_offset;
  uint32_t input_bytes;
  uint32_t output_bytes;
  uint32_t rate;
};

struct kernel_arg_t {
  uint64_t input_addr;
  uint64_t output_addr;
  uint64_t cases_addr;
  uint64_t timing_addr;
  uint32_t mode;
  uint32_t permutations;
  uint32_t stage_variant;
  uint32_t round;
};

#endif
