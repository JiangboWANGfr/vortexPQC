#ifndef MLKEM_NTT_COST_COMMON_H
#define MLKEM_NTT_COST_COMMON_H

#include <stdint.h>

enum {
  COST_NTT,
  COST_NTT_IDENTITY,
  COST_NTT_ISE,
  COST_INTT,
  COST_INTT_IDENTITY,
  COST_INTT_ISE,
  COST_COUNT
};

struct cost_result_t {
  uint64_t cycles[COST_COUNT];
  uint64_t instructions[COST_COUNT];
  uint32_t checksums[COST_COUNT];
  uint32_t mismatches[COST_COUNT];
  uint32_t completed;
};

struct kernel_arg_t {
  uint64_t result_addr;
  uint32_t iterations;
};

#endif
