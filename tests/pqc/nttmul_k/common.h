#ifndef NTTMUL_K_COMMON_H
#define NTTMUL_K_COMMON_H

#include <stdint.h>

struct kernel_arg_t {
  uint64_t input_a_addr;
  uint64_t input_b_addr;
  uint64_t output_addr;
  uint64_t cycles_addr;
  uint32_t count;
  uint32_t rounds;
  uint32_t mode;
};

enum {
  NTTMUL_MODE_NORMAL = 0,
  NTTMUL_MODE_RAW64 = 1,
};

#define NTTMUL_RAW_ROUNDS 64

#endif
