#ifndef _LSU_MODEL_PROBE_COMMON_H_
#define _LSU_MODEL_PROBE_COMMON_H_

#include <stdint.h>

#define LSU_PROBE_MAX_REQUESTS 8
#define LSU_PROBE_MAX_LANES 32
#define LSU_PROBE_STORES_PER_LANE 4
#define LSU_PROBE_WORDS_PER_REQUEST \
  (LSU_PROBE_MAX_LANES * LSU_PROBE_STORES_PER_LANE)
#define LSU_PROBE_DONE 0x4c535550u
#define LSU_PROBE_POISON 0xdeadbeefu

enum lsu_probe_mode_t {
  LSU_PROBE_EMPTY = 0,
  LSU_PROBE_CLEAN = 1,
  LSU_PROBE_DIRTY = 2,
  LSU_PROBE_BARRIER = 3,
  LSU_PROBE_BARRIER_DIRTY = 4,
};

typedef struct {
  uint64_t scratch_addr;
  uint64_t results_addr;
  uint32_t requests;
  uint32_t lanes;
  uint32_t rounds;
  uint32_t mode;
} kernel_arg_t;

typedef struct {
  uint64_t start;
  uint64_t end;
  uint32_t checksum;
  uint32_t completed;
} lsu_probe_result_t;

static inline uint32_t lsu_probe_seed(uint32_t request, uint32_t lane) {
  return 0x9e3779b9u * (request + 1u) ^ 0x85ebca6bu * (lane + 1u);
}

static inline uint32_t lsu_probe_step(uint32_t value, uint32_t round) {
  value ^= round + 0x7f4a7c15u;
  value *= 0x27d4eb2du;
  return value ^ (value >> 15);
}

static inline uint32_t lsu_probe_store_value(uint32_t value, uint32_t slot) {
  return value ^ (0x01010101u * (slot + 1u));
}

#endif
