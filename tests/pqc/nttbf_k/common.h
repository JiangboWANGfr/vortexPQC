#ifndef NTTBF_K_COMMON_H
#define NTTBF_K_COMMON_H

#include <stdint.h>

#define NTTBF_K_LANES 32
#define NTTBF_K_STAGES 5
#define NTTBF_K_OPS (2 * NTTBF_K_STAGES)

struct kernel_arg_t {
  uint64_t values_addr;
  uint64_t zetas_addr;
  uint64_t output_addr;
  uint32_t vectors;
};

#endif
