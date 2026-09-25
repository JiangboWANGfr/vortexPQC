#ifndef MLD_NTT_REG32_H
#define MLD_NTT_REG32_H

#include "mldsa_ntt_reg32.h"

static int32_t* mld_ntt_state[MLD_PROF_SLOTS];
static unsigned mld_ntt_inverse[MLD_PROF_SLOTS];

extern "C" __attribute__((noinline, used)) void mld_profile_ntt_lanes() {
  const unsigned slot = mld_prof_slot();
  mld_ntt_reg32(mld_ntt_state[slot], mld_ntt_inverse[slot]);
  __syncthreads();
}

#if __riscv_xlen == 64
#define MLD_NTT_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLD_NTT_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLD_NTT_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLD_NTT_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

extern "C" __attribute__((naked, noinline)) void mld_profile_ntt_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLD_NTT_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mld_profile_ntt_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLD_NTT_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

#undef MLD_NTT_SAVE_RA
#undef MLD_NTT_RESTORE_RA

extern "C" void mld_profile_ntt(int32_t* p, unsigned inverse) {
  const unsigned slot = mld_prof_slot();
  mld_ntt_state[slot] = p;
  mld_ntt_inverse[slot] = inverse;
  vx_fence();
  __syncthreads();
  mld_profile_ntt_expand();
}

#endif
