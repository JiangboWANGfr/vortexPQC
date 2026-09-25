#ifndef MLK_NOISE_W32_H
#define MLK_NOISE_W32_H

static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "ML-KEM noise sampling requires a complete 32-lane ALU vector");

struct mlk_noise_args_t {
  int16_t* coeffs;
  const uint8_t* bytes;
  unsigned eta;
};

static mlk_noise_args_t mlk_noise_args[VX_CFG_NUM_WARPS];

extern "C" __attribute__((noinline, used)) void mlk_profile_noise_lanes() {
  const auto& args = mlk_noise_args[vx_warp_id()];
  const unsigned lane = vx_thread_id();
  if (args.eta == 2) {
    const uint8_t* a = args.bytes + 4 * lane;
    const uint32_t t = (uint32_t)a[0] | ((uint32_t)a[1] << 8) |
                       ((uint32_t)a[2] << 16) | ((uint32_t)a[3] << 24);
    uint32_t d = (t & 0x55555555) + ((t >> 1) & 0x55555555);
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < 8; ++i) {
      const int16_t x = (int16_t)((d >> (4 * i)) & 0x3);
      const int16_t y = (int16_t)((d >> (4 * i + 2)) & 0x3);
      args.coeffs[8 * lane + i] = (int16_t)(x - y);
    }
  } else {
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 2; ++k) {
      const unsigned group = lane + 32 * k;
      const uint8_t* a = args.bytes + 3 * group;
      const uint32_t t = (uint32_t)a[0] | ((uint32_t)a[1] << 8) |
                         ((uint32_t)a[2] << 16);
      uint32_t d = t & 0x00249249;
      d += (t >> 1) & 0x00249249;
      d += (t >> 2) & 0x00249249;
#pragma clang loop unroll(full)
      for (unsigned i = 0; i < 4; ++i) {
        const int16_t x = (int16_t)((d >> (6 * i)) & 0x7);
        const int16_t y = (int16_t)((d >> (6 * i + 3)) & 0x7);
        args.coeffs[4 * group + i] = (int16_t)(x - y);
      }
    }
  }
  __syncthreads();
}

#if __riscv_xlen == 64
#define MLK_NOISE_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLK_NOISE_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLK_NOISE_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLK_NOISE_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

extern "C" __attribute__((naked, noinline)) void mlk_profile_noise_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLK_NOISE_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mlk_profile_noise_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLK_NOISE_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

static void mlk_profile_noise(int16_t* r, const uint8_t* a, unsigned eta) {
#if defined(PQC_PROFILE_PHASES)
  const mlk_phase_scope_t scope = mlk_phase_begin();
#endif
  auto& args = mlk_noise_args[vx_warp_id()];
  args.coeffs = r;
  args.bytes = a;
  args.eta = eta;
  vx_fence();
  __syncthreads();
  mlk_profile_noise_expand();
#if defined(PQC_PROFILE_PHASES)
  mlk_phase_end(MLK_PHASE_NOISE, scope);
#endif
}

extern "C" void mlk_profile_poly_cbd2(mlk_poly* r, const uint8_t* buf) {
  mlk_profile_noise(r->coeffs, buf, 2);
}

#if MLKEM_ETA1 == 3
extern "C" void mlk_profile_poly_cbd3(mlk_poly* r, const uint8_t* buf) {
  mlk_profile_noise(r->coeffs, buf, 3);
}
#endif

#undef MLK_NOISE_RESTORE_RA
#undef MLK_NOISE_SAVE_RA

#endif
