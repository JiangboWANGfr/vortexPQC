#include <vx_intrinsics.h>
#include <vx_spawn2.h>

#include "common.h"

template <unsigned Mode>
static __attribute__((noinline)) uint32_t run_probe(
    volatile uint32_t* scratch, unsigned request, unsigned lane,
    unsigned lanes, unsigned rounds) {
  uint32_t value = lsu_probe_seed(request, lane);
  for (unsigned round = 0; round < rounds; ++round) {
    value = lsu_probe_step(value, round);
    if constexpr (Mode == LSU_PROBE_DIRTY ||
                  Mode == LSU_PROBE_BARRIER_DIRTY) {
      for (unsigned slot = 0; slot < LSU_PROBE_STORES_PER_LANE; ++slot) {
        scratch[slot * lanes + lane] = lsu_probe_store_value(value, slot);
      }
    }
    if constexpr (Mode == LSU_PROBE_CLEAN || Mode == LSU_PROBE_DIRTY) {
      vx_fence();
    } else if constexpr (Mode == LSU_PROBE_BARRIER ||
                         Mode == LSU_PROBE_BARRIER_DIRTY) {
      __syncthreads();
    }
  }
  return value;
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const unsigned request = blockIdx.x;
  const unsigned lane = threadIdx.x;
  const unsigned lanes = arg->lanes;
  const unsigned rounds = arg->rounds;
  const unsigned mode = arg->mode;
  auto scratch = reinterpret_cast<volatile uint32_t*>(arg->scratch_addr)
               + request * LSU_PROBE_WORDS_PER_REQUEST;
  auto result = reinterpret_cast<lsu_probe_result_t*>(arg->results_addr) + request;

  vx_barrier(1u << 8, arg->requests);
  const uint64_t start = vx_rdcycle_sync();
  uint32_t checksum;
  if (mode == LSU_PROBE_EMPTY) {
    checksum = run_probe<LSU_PROBE_EMPTY>(scratch, request, lane, lanes, rounds);
  } else if (mode == LSU_PROBE_CLEAN) {
    checksum = run_probe<LSU_PROBE_CLEAN>(scratch, request, lane, lanes, rounds);
  } else if (mode == LSU_PROBE_DIRTY) {
    checksum = run_probe<LSU_PROBE_DIRTY>(scratch, request, lane, lanes, rounds);
  } else if (mode == LSU_PROBE_BARRIER) {
    checksum = run_probe<LSU_PROBE_BARRIER>(scratch, request, lane, lanes, rounds);
  } else {
    checksum = run_probe<LSU_PROBE_BARRIER_DIRTY>(
        scratch, request, lane, lanes, rounds);
  }
  const uint64_t end = vx_rdcycle_sync();
  vx_barrier(1u << 8, arg->requests);

  if (lane == 0) {
    result->start = start;
    result->end = end;
    result->checksum = checksum;
    result->completed = LSU_PROBE_DONE;
  }
}
