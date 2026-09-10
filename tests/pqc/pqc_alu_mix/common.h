#ifndef PQC_ALU_MIX_COMMON_H
#define PQC_ALU_MIX_COMMON_H

#include <stdint.h>

#define MIX_WARPS 8
#define MIX_LANES 32
#define MIX_STEPS 8
#define MIX_THREADS (MIX_WARPS * MIX_LANES)

struct mix_arg_t {
  uint64_t input_addr;
  uint64_t output_addr;
};

#endif
