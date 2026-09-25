#ifndef MLD_POINTWISE_W32_H
#define MLD_POINTWISE_W32_H

#include <pqc/vx_ntt.h>

static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "ML-DSA pointwise requires a complete 32-lane ALU vector");

static int32_t* mld_pointwise_a[MLD_PROF_SLOTS];
#if defined(PQC_POINTWISE_ISE)
static const int32_t* mld_pointwise_b[MLD_PROF_SLOTS];
#endif
#if defined(PQC_POINTWISE_L5_W32)
static const int32_t (*mld_pointwise_u[MLD_PROF_SLOTS])[256];
static const int32_t (*mld_pointwise_v[MLD_PROF_SLOTS])[256];
#if defined(PQC_POINTWISE_ISE)
static unsigned mld_pointwise_l5_mode[MLD_PROF_SLOTS];
#endif
#endif

#if __riscv_xlen == 64
#define MLD_POINTWISE_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLD_POINTWISE_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLD_POINTWISE_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLD_POINTWISE_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

extern "C" __attribute__((noinline, used)) void mld_profile_pointwise_lanes() {
  const unsigned slot = mld_prof_slot();
  const unsigned lane = vx_thread_id();
  int32_t* a = mld_pointwise_a[slot];
#if defined(PQC_POINTWISE_L5_W32)
#if defined(PQC_POINTWISE_ISE)
  if (mld_pointwise_l5_mode[slot])
#endif
  {
    const int32_t (*u)[256] = mld_pointwise_u[slot];
    const int32_t (*v)[256] = mld_pointwise_v[slot];
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 8; ++k) {
      const unsigned i = lane + 32 * k;
      int64_t sum = 0;
      for (unsigned j = 0; j < MLD_PROFILE_L; ++j)
        sum += (int64_t)u[j][i] * v[j][i];
      a[i] = mld_prof_montgomery_reduce(sum);
    }
  }
#if defined(PQC_POINTWISE_ISE)
  else
#endif
#endif
#if defined(PQC_POINTWISE_ISE)
  {
    const int32_t* b = mld_pointwise_b[slot];
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 8; ++k) {
      const unsigned i = lane + 32 * k;
      a[i] = vx_nttmul_d(a[i], b[i]);
    }
  }
#endif
  __syncthreads();
}

extern "C" __attribute__((naked, noinline)) void mld_profile_pointwise_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLD_POINTWISE_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mld_profile_pointwise_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLD_POINTWISE_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

#if defined(PQC_POINTWISE_ISE)
extern "C" void mld_profile_pointwise(int32_t* a, const int32_t* b) {
  const unsigned slot = mld_prof_slot();
  mld_pointwise_a[slot] = a;
  mld_pointwise_b[slot] = b;
#if defined(PQC_POINTWISE_L5_W32)
  mld_pointwise_l5_mode[slot] = 0;
#endif
  vx_fence();
  __syncthreads();
  mld_profile_pointwise_expand();
}
#endif

#if defined(PQC_POINTWISE_L5_W32)
extern "C" void mld_profile_pointwise_acc(int32_t* w,
                                          const int32_t u[MLD_PROFILE_L][256],
                                          const int32_t v[MLD_PROFILE_L][256]) {
  const unsigned slot = mld_prof_slot();
  mld_pointwise_a[slot] = w;
  mld_pointwise_u[slot] = u;
  mld_pointwise_v[slot] = v;
#if defined(PQC_POINTWISE_ISE)
  mld_pointwise_l5_mode[slot] = 1;
#endif
  vx_fence();
  __syncthreads();
  mld_profile_pointwise_expand();
}
#endif

#undef MLD_POINTWISE_SAVE_RA
#undef MLD_POINTWISE_RESTORE_RA

#endif
