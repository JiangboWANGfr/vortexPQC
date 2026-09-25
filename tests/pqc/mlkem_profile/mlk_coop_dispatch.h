#ifndef MLK_COOP_DISPATCH_H
#define MLK_COOP_DISPATCH_H

#if defined(PQC_PROFILE_PHASES)
#include "mlk_phase_profile.h"
#endif

struct mlk_coop_args_t {
  int16_t* polynomial;
  unsigned inverse;
  unsigned lanes;
#if defined(PQC_REJ_WARP)
  int16_t* rej_output;
  const uint8_t* rej_buffer;
  unsigned rej_len;
  unsigned rej_buflen;
  unsigned rej_count;
#endif
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

#if defined(PQC_REJ_WARP)
extern "C" __attribute__((noinline, used)) void mlk_profile_rej_uniform_lanes() {
  auto& args = mlk_coop_args[vx_warp_id()];
  const unsigned lane = vx_thread_id();
  const unsigned triplets = args.rej_buflen / 3;
  const uint32_t earlier = lane ? (1u << lane) - 1u : 0u;
  unsigned count = 0;

  for (unsigned base = 0; base < triplets && count < args.rej_len; base += 32) {
    const unsigned triplet = base + lane;
    const bool in_range = triplet < triplets;
    unsigned val0 = 0, val1 = 0;
    if (in_range) {
      const unsigned pos = 3 * triplet;
      val0 = (args.rej_buffer[pos] |
              ((unsigned)args.rej_buffer[pos + 1] << 8)) & 0xfff;
      val1 = ((unsigned)args.rej_buffer[pos + 1] >> 4) |
             ((unsigned)args.rej_buffer[pos + 2] << 4);
    }
    const bool keep0 = in_range && val0 < MLKEM_Q;
    const bool keep1 = in_range && val1 < MLKEM_Q;
    const uint32_t mask0 = vx_vote_ballot(keep0);
    const uint32_t mask1 = vx_vote_ballot(keep1);
    const unsigned rank0 = count + __builtin_popcount(mask0 & earlier) +
                           __builtin_popcount(mask1 & earlier);
    if (keep0 && rank0 < args.rej_len) {
      args.rej_output[rank0] = (int16_t)val0;
    }
    if (keep1 && rank0 + (unsigned)keep0 < args.rej_len) {
      args.rej_output[rank0 + (unsigned)keep0] = (int16_t)val1;
    }
    const unsigned next = count + __builtin_popcount(mask0) +
                          __builtin_popcount(mask1);
    count = next < args.rej_len ? next : args.rej_len;
  }

  __syncthreads();
  if (lane == 0) {
    args.rej_count = count;
  }
}
#endif

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

#if defined(PQC_REJ_WARP)
extern "C" __attribute__((naked, noinline)) void mlk_profile_rej_uniform_expand(unsigned) {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLK_NTT_SAVE_RA
      ".insn r %0, 0, 0, x0, a0, x0\n\t"
      "call mlk_profile_rej_uniform_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLK_NTT_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}
#endif

#undef MLK_NTT_RESTORE_RA
#undef MLK_NTT_SAVE_RA

extern "C" void mlk_profile_ntt(int16_t* p, unsigned inverse) {
#if defined(PQC_PROFILE_PHASES)
  const mlk_phase_scope_t scope = mlk_phase_begin();
#endif
  auto& args = mlk_coop_args[vx_warp_id()];
  args.polynomial = p;
  args.inverse = inverse;
  // Publish the descriptor before reactivating worker lanes.
  vx_fence();
  __syncthreads();
  const unsigned mask = args.lanes == 32 ? UINT32_MAX : (1u << args.lanes) - 1u;
  mlk_profile_ntt_expand(mask);
#if defined(PQC_PROFILE_PHASES)
  mlk_phase_end(inverse ? MLK_PHASE_INTT : MLK_PHASE_NTT, scope);
#endif
}

#if defined(PQC_REJ_WARP)
extern "C" int mlk_profile_rej_uniform(int16_t* r, unsigned len,
                                        const uint8_t* buf, unsigned buflen) {
#if defined(PQC_PROFILE_PHASES)
  const mlk_phase_scope_t scope = mlk_phase_begin();
#endif
  auto& args = mlk_coop_args[vx_warp_id()];
  args.rej_output = r;
  args.rej_buffer = buf;
  args.rej_len = len;
  args.rej_buflen = buflen;
  vx_fence();
  __syncthreads();
  mlk_profile_rej_uniform_expand(UINT32_MAX);
#if defined(PQC_PROFILE_PHASES)
  mlk_phase_end(MLK_PHASE_REJECTION, scope);
#endif
  return (int)args.rej_count;
}
#endif

#endif
