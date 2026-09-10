#ifndef MLKEM_ARITH_XN_COMMON_H
#define MLKEM_ARITH_XN_COMMON_H

#include <stdint.h>

enum {
  ARITH_MULCACHE,
  ARITH_BASEMUL,
  ARITH_REDUCE,
  ARITH_OP_COUNT
};

enum {
  ARITH_SCALAR,
  ARITH_W32_C,
  ARITH_W32_ISE,
  ARITH_MODE_COUNT
};

enum {
  ARITH_N = 256,
  ARITH_CASES = 8,
  ARITH_MAX_REQUESTS = 8,
  ARITH_EXHAUSTIVE_COUNT = 65536
};

struct arith_result_t {
  uint64_t start;
  uint64_t end;
};

struct kernel_arg_t {
  uint64_t output_addr;
  uint64_t results_addr;
  uint32_t requests;
  uint32_t operation;
  uint32_t mode;
  uint32_t sample;
  uint32_t exhaustive;
};

#endif
