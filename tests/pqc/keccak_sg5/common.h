#ifndef _COMMON_H_
#define _COMMON_H_
#include <stdint.h>
#include <VX_config.h>

// Plain-ISA SG5: Keccak-f1600 with the state distributed across five lanes.
//
// This is the first step of pqc/docs/proposals/cooperative_ise_proposal.md S9,
// and it needs no new hardware: SHFL_IDX with mask = 0 already gives arbitrary
// per-lane indexing across the whole warp (VX_alu_int.sv:179-221), so the entire
// school-D data movement is expressible today. What it measures is the plain-ISA
// row of that document's S4 -- three derived numbers (176 instructions per round,
// 3.49x latency, 0.66x throughput) that have never been run.
//
// SG1 is the control: one lane holds a whole state, which is what schools A, B
// and C all assume. Both arms permute the same seeds the same number of times
// and are verified against the library's own mlk_keccakf1600_permute on the host,
// so a lane-mapping error cannot pass as a speedup -- the failure mode that
// produced a fake 1.249x in the cooperative NTT (pqc/results/ntt_cooperative.csv).

#define KS_ARM_SG1 0
#define KS_ARM_SG5 1
#define KS_ARM_PE  2   // the KECCAKF instruction

#define KS_LANES_PER_STATE 5
#define KS_WORDS 25

typedef struct {
  uint64_t states_addr;  // out : nstates * 25 * uint64_t, the permuted states
  uint64_t cycles_addr;  // out : KS_CY_COUNT * uint64_t
  uint32_t arm;          // in  : KS_ARM_SG1 | KS_ARM_SG5
  uint32_t lanes;        // in  : block_dim, i.e. W
  uint32_t perms;        // in  : chained permutations per state
  uint32_t fences;       // in  : SG5 only -- 0, 1 or 2 fences per round
  uint32_t nstates;      // in  : W for SG1, W/5 for SG5
} kernel_arg_t;

#define KS_CY_RUN   0   // cycles for the whole permutation loop
#define KS_CY_SPAN  1   // paintable stack span
#define KS_CY_STACK 2   // stack peak
#define KS_CY_COUNT 3

#endif
