#ifndef MLD_SIGN_W32_H
#define MLD_SIGN_W32_H

static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "ML-DSA sign arithmetic requires a complete 32-lane ALU vector");

enum {
  MLD_SIGN_MODE_DECOMPOSE,
  MLD_SIGN_MODE_USE_HINT,
  MLD_SIGN_MODE_CHKNORM,
  MLD_SIGN_MODE_CADDQ,
  MLD_SIGN_MODE_ZUNPACK
};

static int32_t* mld_sign_a[MLD_PROF_SLOTS];
#if defined(PQC_SIGN_DECOMPOSE_WARP)
static int32_t* mld_sign_a1[MLD_PROF_SLOTS];
#endif
#if defined(PQC_SIGN_HINT_WARP)
static const int32_t* mld_sign_h[MLD_PROF_SLOTS];
#endif
#if defined(PQC_SIGN_ZUNPACK_WARP)
static const uint8_t* mld_sign_packed[MLD_PROF_SLOTS];
#endif
#if defined(PQC_SIGN_CHKNORM_WARP)
static const int32_t* mld_sign_check_a[MLD_PROF_SLOTS];
static int32_t mld_sign_bound[MLD_PROF_SLOTS];
#endif
static unsigned mld_sign_mode[MLD_PROF_SLOTS];
#if defined(PQC_SIGN_CHKNORM_WARP)
static int mld_sign_result[MLD_PROF_SLOTS];
static uint32_t mld_sign_invalid[MLD_PROF_SLOTS][32];
#endif

#if __riscv_xlen == 64
#define MLD_SIGN_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLD_SIGN_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLD_SIGN_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLD_SIGN_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

extern "C" __attribute__((noinline, used)) void mld_profile_sign_lanes() {
  const unsigned slot = mld_prof_slot();
  const unsigned lane = vx_thread_id();
  int32_t* a = mld_sign_a[slot];

  switch (mld_sign_mode[slot]) {
#if defined(PQC_SIGN_DECOMPOSE_WARP)
  case MLD_SIGN_MODE_DECOMPOSE: {
    int32_t* a1 = mld_sign_a1[slot];
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 8; ++k) {
      const unsigned i = lane + 32 * k;
      mld_decompose(&a[i], &a1[i], a[i]);
    }
  } break;
#endif
#if defined(PQC_SIGN_HINT_WARP)
  case MLD_SIGN_MODE_USE_HINT: {
    const int32_t* h = mld_sign_h[slot];
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 8; ++k) {
      const unsigned i = lane + 32 * k;
      a[i] = mld_use_hint(a[i], h[i]);
    }
  } break;
#endif
#if defined(PQC_SIGN_CHKNORM_WARP)
  case MLD_SIGN_MODE_CHKNORM: {
    const int32_t* check_a = mld_sign_check_a[slot];
    const int32_t bound = mld_sign_bound[slot];
    int invalid = 0;
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 8; ++k) {
      const int32_t value = check_a[lane + 32 * k];
      invalid |= mld_ct_cmask_neg_i32(bound - 1 - mld_ct_abs_i32(value));
    }
    mld_sign_invalid[slot][lane] = invalid != 0;
    __syncthreads();
    if (lane == 0) {
      uint32_t any_invalid = 0;
      for (unsigned i = 0; i < 32; ++i)
        any_invalid |= mld_sign_invalid[slot][i];
      mld_sign_result[slot] = any_invalid != 0;
    }
  } break;
#endif
#if defined(PQC_SIGN_CADDQ_WARP)
  case MLD_SIGN_MODE_CADDQ:
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 8; ++k) {
      const unsigned i = lane + 32 * k;
      a[i] = mld_caddq(a[i]);
    }
    break;
#endif
#if defined(PQC_SIGN_ZUNPACK_WARP)
  case MLD_SIGN_MODE_ZUNPACK: {
    const uint8_t* packed = mld_sign_packed[slot];
#if MLD_CONFIG_PARAMETER_SET == 44
    constexpr int32_t gamma1 = 1 << 17;
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 2; ++k) {
      const unsigned group = lane + 32 * k;
      const uint8_t* in = packed + 9 * group;
      int32_t* out = a + 4 * group;
      out[0] = gamma1 - ((in[0] | (int32_t)in[1] << 8 |
                                  (int32_t)in[2] << 16) & 0x3FFFF);
      out[1] = gamma1 - ((in[2] >> 2 | (int32_t)in[3] << 6 |
                                  (int32_t)in[4] << 14) & 0x3FFFF);
      out[2] = gamma1 - ((in[4] >> 4 | (int32_t)in[5] << 4 |
                                  (int32_t)in[6] << 12) & 0x3FFFF);
      out[3] = gamma1 - ((in[6] >> 6 | (int32_t)in[7] << 2 |
                                  (int32_t)in[8] << 10) & 0x3FFFF);
    }
#else
    constexpr int32_t gamma1 = 1 << 19;
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 4; ++k) {
      const unsigned group = lane + 32 * k;
      const uint8_t* in = packed + 5 * group;
      int32_t* out = a + 2 * group;
      out[0] = gamma1 - ((in[0] | (int32_t)in[1] << 8 |
                                  (int32_t)in[2] << 16) & 0xFFFFF);
      out[1] = gamma1 - (in[2] >> 4 | (int32_t)in[3] << 4 |
                                (int32_t)in[4] << 12);
    }
#endif
  } break;
#endif
  }
  __syncthreads();
}

extern "C" __attribute__((naked, noinline)) void mld_profile_sign_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLD_SIGN_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mld_profile_sign_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLD_SIGN_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

#if defined(PQC_SIGN_DECOMPOSE_WARP)
extern "C" void mld_profile_decompose(int32_t* a1, int32_t* a0) {
  const unsigned slot = mld_prof_slot();
  mld_sign_a[slot] = a0;
  mld_sign_a1[slot] = a1;
  mld_sign_mode[slot] = MLD_SIGN_MODE_DECOMPOSE;
  vx_fence();
  __syncthreads();
  mld_profile_sign_expand();
}
#endif

#if defined(PQC_SIGN_CADDQ_WARP)
extern "C" void mld_profile_caddq(int32_t* a) {
  const unsigned slot = mld_prof_slot();
  mld_sign_a[slot] = a;
  mld_sign_mode[slot] = MLD_SIGN_MODE_CADDQ;
  vx_fence();
  __syncthreads();
  mld_profile_sign_expand();
}
#endif

#if defined(PQC_SIGN_ZUNPACK_WARP)
extern "C" void mld_profile_zunpack(int32_t* r, const uint8_t* a) {
  const unsigned slot = mld_prof_slot();
  mld_sign_a[slot] = r;
  mld_sign_packed[slot] = a;
  mld_sign_mode[slot] = MLD_SIGN_MODE_ZUNPACK;
  vx_fence();
  __syncthreads();
  mld_profile_sign_expand();
}
#endif

#if defined(PQC_SIGN_HINT_WARP)
extern "C" void mld_profile_use_hint(int32_t* a, const int32_t* h) {
  const unsigned slot = mld_prof_slot();
  mld_sign_a[slot] = a;
  mld_sign_h[slot] = h;
  mld_sign_mode[slot] = MLD_SIGN_MODE_USE_HINT;
  vx_fence();
  __syncthreads();
  mld_profile_sign_expand();
}
#endif

#if defined(PQC_SIGN_CHKNORM_WARP)
extern "C" int mld_profile_chknorm(const int32_t* a, int32_t bound) {
  const unsigned slot = mld_prof_slot();
  mld_sign_check_a[slot] = a;
  mld_sign_bound[slot] = bound;
  mld_sign_mode[slot] = MLD_SIGN_MODE_CHKNORM;
  vx_fence();
  __syncthreads();
  mld_profile_sign_expand();
  return mld_sign_result[slot];
}
#endif

#undef MLD_SIGN_SAVE_RA
#undef MLD_SIGN_RESTORE_RA

#endif
