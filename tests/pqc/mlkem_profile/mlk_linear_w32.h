#ifndef MLK_LINEAR_W32_H
#define MLK_LINEAR_W32_H

static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "ML-KEM linear mapping requires a complete 32-lane ALU vector");

enum {
  MLK_LINEAR_TOMONT,
  MLK_LINEAR_ADD,
  MLK_LINEAR_SUB,
  MLK_LINEAR_FROMMSG,
  MLK_LINEAR_TOMSG,
};

struct mlk_linear_args_t {
  int16_t* output;
  const int16_t* input;
  uint8_t* bytes;
  const uint8_t* input_bytes;
  unsigned count;
  unsigned operation;
};

static mlk_linear_args_t mlk_linear_args[VX_CFG_NUM_WARPS];

extern "C" __attribute__((noinline, used)) void mlk_profile_linear_lanes() {
  const auto& args = mlk_linear_args[vx_warp_id()];
  const unsigned lane = vx_thread_id();
  if (args.operation <= MLK_LINEAR_SUB) {
    for (unsigned index = lane; index < args.count; index += 32) {
      if (args.operation == MLK_LINEAR_TOMONT) {
#if defined(PQC_ARITH_NTTMUL)
        args.output[index] = vx_nttmul_k(args.output[index], 1353);
#else
        args.output[index] = mlk_fqmul(args.output[index], 1353);
#endif
      } else if (args.operation == MLK_LINEAR_ADD) {
        args.output[index] = (int16_t)(args.output[index] + args.input[index]);
      } else {
        args.output[index] = (int16_t)(args.output[index] - args.input[index]);
      }
    }
  } else if (args.operation == MLK_LINEAR_FROMMSG) {
    const uint8_t value = args.input_bytes[lane];
#pragma clang loop unroll(full)
    for (unsigned bit = 0; bit < 8; ++bit) {
      const int16_t mask = (int16_t)-(int16_t)((value >> bit) & 1u);
      args.output[8 * lane + bit] = mask & MLKEM_Q_HALF;
    }
  } else {
    uint8_t value = 0;
#pragma clang loop unroll(full)
    for (unsigned bit = 0; bit < 8; ++bit) {
      const uint32_t coeff = (uint16_t)args.input[8 * lane + bit];
      const uint32_t compressed =
          (coeff * UINT32_C(1290168) + (UINT32_C(1) << 30)) >> 31;
      value |= (uint8_t)(compressed << bit);
    }
    args.bytes[lane] = value;
  }
  __syncthreads();
}

#if __riscv_xlen == 64
#define MLK_LINEAR_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLK_LINEAR_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLK_LINEAR_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLK_LINEAR_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

extern "C" __attribute__((naked, noinline)) void mlk_profile_linear_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLK_LINEAR_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mlk_profile_linear_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLK_LINEAR_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

static void mlk_profile_linear_dispatch(unsigned operation, int16_t* output,
                                        const int16_t* input, unsigned count,
                                        uint8_t* bytes,
                                        const uint8_t* input_bytes) {
#if defined(PQC_PROFILE_PHASES)
  const mlk_phase_scope_t scope = mlk_phase_begin();
#endif
  auto& args = mlk_linear_args[vx_warp_id()];
  args.output = output;
  args.input = input;
  args.bytes = bytes;
  args.input_bytes = input_bytes;
  args.count = count;
  args.operation = operation;
  vx_fence();
  __syncthreads();
  mlk_profile_linear_expand();
#if defined(PQC_PROFILE_PHASES)
  mlk_phase_end(MLK_PHASE_LINEAR, scope);
#endif
}

extern "C" void mlk_profile_polyvec_tomont(mlk_polyvec* r) {
  mlk_profile_linear_dispatch(MLK_LINEAR_TOMONT, r->vec[0].coeffs, nullptr,
                              MLKEM_K * MLKEM_N, nullptr, nullptr);
}

extern "C" void mlk_profile_polyvec_add(mlk_polyvec* r,
                                         const mlk_polyvec* b) {
  mlk_profile_linear_dispatch(MLK_LINEAR_ADD, r->vec[0].coeffs,
                              b->vec[0].coeffs, MLKEM_K * MLKEM_N,
                              nullptr, nullptr);
}

extern "C" void mlk_profile_poly_add(mlk_poly* r, const mlk_poly* b) {
  mlk_profile_linear_dispatch(MLK_LINEAR_ADD, r->coeffs, b->coeffs,
                              MLKEM_N, nullptr, nullptr);
}

extern "C" void mlk_profile_poly_sub(mlk_poly* r, const mlk_poly* b) {
  mlk_profile_linear_dispatch(MLK_LINEAR_SUB, r->coeffs, b->coeffs,
                              MLKEM_N, nullptr, nullptr);
}

extern "C" void mlk_profile_poly_frommsg(mlk_poly* r, const uint8_t* msg) {
  mlk_profile_linear_dispatch(MLK_LINEAR_FROMMSG, r->coeffs, nullptr,
                              MLKEM_N, nullptr, msg);
}

extern "C" void mlk_profile_poly_tomsg(uint8_t* msg, const mlk_poly* r) {
  mlk_profile_linear_dispatch(MLK_LINEAR_TOMSG, nullptr, r->coeffs,
                              MLKEM_N, msg, nullptr);
}

#undef MLK_LINEAR_RESTORE_RA
#undef MLK_LINEAR_SAVE_RA

#endif
