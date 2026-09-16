#ifndef MLK_COOP_DISPATCH_H
#define MLK_COOP_DISPATCH_H

#if defined(PQC_PROFILE_PHASES)
#include "mlk_phase_profile.h"
#endif

struct mlk_coop_args_t {
  int16_t* polynomial;
  unsigned inverse;
  unsigned lanes;
};

static mlk_coop_args_t mlk_coop_args[VX_CFG_NUM_WARPS];

extern "C" __attribute__((noinline, used)) void mlk_profile_ntt_lanes() {
  const auto& args = mlk_coop_args[vx_warp_id()];
#if defined(PQC_NTT_SMEM32)
  auto scratch = reinterpret_cast<int32_t*>(__local_mem());
  if (args.inverse) {
    mlk_invntt_smem_w32(args.polynomial, scratch, vx_thread_id());
  } else {
    mlk_ntt_smem_w32(args.polynomial, scratch, vx_thread_id());
  }
  __syncthreads();
#elif defined(PQC_NTT_REG32)
  if (args.inverse) {
    mlk_invntt_w32(args.polynomial, vx_thread_id());
  } else {
    mlk_ntt_w32(args.polynomial, vx_thread_id());
  }
  __syncthreads();
#else
  if (args.inverse) {
    mlk_invntt_coop(args.polynomial, args.lanes, vx_thread_id());
  } else {
    mlk_ntt_coop(args.polynomial, args.lanes, vx_thread_id());
  }
#endif
}

#if __riscv_xlen == 64
#define MLK_NTT_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLK_NTT_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLK_NTT_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLK_NTT_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

// Only the leader has a live caller frame; workers enter with their own KMU stacks.
extern "C" __attribute__((naked, noinline)) void mlk_profile_ntt_expand(unsigned) {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLK_NTT_SAVE_RA
      ".insn r %0, 0, 0, x0, a0, x0\n\t"
      "call mlk_profile_ntt_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLK_NTT_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

#undef MLK_NTT_RESTORE_RA
#undef MLK_NTT_SAVE_RA

extern "C" void mlk_profile_ntt(int16_t* p, unsigned inverse) {
#if defined(PQC_PROFILE_PHASES)
  const mlk_phase_scope_t scope = mlk_phase_begin();
#endif
  auto& args = mlk_coop_args[vx_warp_id()];
  args.polynomial = p;
  args.inverse = inverse;
  // A one-warp BAR publishes the descriptor without flushing unrelated cache lines.
  __syncthreads();
  const unsigned mask = args.lanes == 32 ? UINT32_MAX : (1u << args.lanes) - 1u;
  mlk_profile_ntt_expand(mask);
#if defined(PQC_PROFILE_PHASES)
  mlk_phase_end(inverse ? MLK_PHASE_INTT : MLK_PHASE_NTT, scope);
#endif
}

#endif
