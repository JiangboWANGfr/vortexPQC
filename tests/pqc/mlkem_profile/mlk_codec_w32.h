#ifndef MLK_CODEC_W32_H
#define MLK_CODEC_W32_H

static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "ML-KEM codec requires a complete 32-lane ALU vector");

struct mlk_codec_args_t {
  uint8_t* bytes;
  const uint8_t* input_bytes;
  int16_t* coeffs;
  const int16_t* input_coeffs;
  unsigned bits;
  unsigned decode;
};

static mlk_codec_args_t mlk_codec_args[VX_CFG_NUM_WARPS];

extern "C" __attribute__((noinline, used)) void mlk_profile_codec_lanes() {
  const auto& args = mlk_codec_args[vx_warp_id()];
  const unsigned lane = vx_thread_id();
  if (args.bits == 4) {
    if (args.decode) {
#pragma clang loop unroll(full)
      for (unsigned k = 0; k < 4; ++k) {
        const unsigned group = lane + 32 * k;
        const uint8_t packed = args.input_bytes[group];
        args.coeffs[2 * group] = mlk_scalar_decompress_d4(packed & 0xf);
        args.coeffs[2 * group + 1] = mlk_scalar_decompress_d4(packed >> 4);
      }
    } else {
      uint8_t t[8];
#pragma clang loop unroll(full)
      for (unsigned i = 0; i < 8; ++i)
        t[i] = mlk_scalar_compress_d4(args.input_coeffs[8 * lane + i]);
#pragma clang loop unroll(full)
      for (unsigned i = 0; i < 4; ++i)
        args.bytes[4 * lane + i] = (uint8_t)(t[2 * i] | (t[2 * i + 1] << 4));
    }
  } else if (args.bits == 5) {
    if (args.decode) {
      const uint8_t* a = args.input_bytes + 5 * lane;
      uint8_t t[8];
      t[0] = 0x1f & a[0];
      t[1] = 0x1f & ((a[0] >> 5) | (a[1] << 3));
      t[2] = 0x1f & (a[1] >> 2);
      t[3] = 0x1f & ((a[1] >> 7) | (a[2] << 1));
      t[4] = 0x1f & ((a[2] >> 4) | (a[3] << 4));
      t[5] = 0x1f & (a[3] >> 1);
      t[6] = 0x1f & ((a[3] >> 6) | (a[4] << 2));
      t[7] = 0x1f & (a[4] >> 3);
#pragma clang loop unroll(full)
      for (unsigned i = 0; i < 8; ++i)
        args.coeffs[8 * lane + i] = mlk_scalar_decompress_d5(t[i]);
    } else {
      uint8_t t[8];
#pragma clang loop unroll(full)
      for (unsigned i = 0; i < 8; ++i)
        t[i] = mlk_scalar_compress_d5(args.input_coeffs[8 * lane + i]);
      uint8_t* r = args.bytes + 5 * lane;
      r[0] = (uint8_t)(t[0] | (t[1] << 5));
      r[1] = (uint8_t)((t[1] >> 3) | (t[2] << 2) | (t[3] << 7));
      r[2] = (uint8_t)((t[3] >> 1) | (t[4] << 4));
      r[3] = (uint8_t)((t[4] >> 4) | (t[5] << 1) | (t[6] << 6));
      r[4] = (uint8_t)((t[6] >> 2) | (t[7] << 3));
    }
  } else if (args.bits == 10) {
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 2; ++k) {
      const unsigned group = lane + 32 * k;
      if (args.decode) {
        const uint8_t* a = args.input_bytes + 5 * group;
        uint16_t t[4];
        t[0] = 0x3ff & (a[0] | ((uint16_t)a[1] << 8));
        t[1] = 0x3ff & ((a[1] >> 2) | ((uint16_t)a[2] << 6));
        t[2] = 0x3ff & ((a[2] >> 4) | ((uint16_t)a[3] << 4));
        t[3] = 0x3ff & ((a[3] >> 6) | ((uint16_t)a[4] << 2));
#pragma clang loop unroll(full)
        for (unsigned i = 0; i < 4; ++i)
          args.coeffs[4 * group + i] = mlk_scalar_decompress_d10(t[i]);
      } else {
        uint16_t t[4];
#pragma clang loop unroll(full)
        for (unsigned i = 0; i < 4; ++i)
          t[i] = mlk_scalar_compress_d10(args.input_coeffs[4 * group + i]);
        uint8_t* r = args.bytes + 5 * group;
        r[0] = (uint8_t)t[0];
        r[1] = (uint8_t)((t[0] >> 8) | (t[1] << 2));
        r[2] = (uint8_t)((t[1] >> 6) | (t[2] << 4));
        r[3] = (uint8_t)((t[2] >> 4) | (t[3] << 6));
        r[4] = (uint8_t)(t[3] >> 2);
      }
    }
  } else if (args.bits == 11) {
    if (args.decode) {
      const uint8_t* a = args.input_bytes + 11 * lane;
      uint16_t t[8];
      t[0] = 0x7ff & (a[0] | ((uint16_t)a[1] << 8));
      t[1] = 0x7ff & ((a[1] >> 3) | ((uint16_t)a[2] << 5));
      t[2] = 0x7ff & ((a[2] >> 6) | ((uint16_t)a[3] << 2) |
                      ((uint16_t)a[4] << 10));
      t[3] = 0x7ff & ((a[4] >> 1) | ((uint16_t)a[5] << 7));
      t[4] = 0x7ff & ((a[5] >> 4) | ((uint16_t)a[6] << 4));
      t[5] = 0x7ff & ((a[6] >> 7) | ((uint16_t)a[7] << 1) |
                      ((uint16_t)a[8] << 9));
      t[6] = 0x7ff & ((a[8] >> 2) | ((uint16_t)a[9] << 6));
      t[7] = 0x7ff & ((a[9] >> 5) | ((uint16_t)a[10] << 3));
#pragma clang loop unroll(full)
      for (unsigned i = 0; i < 8; ++i)
        args.coeffs[8 * lane + i] = mlk_scalar_decompress_d11(t[i]);
    } else {
      uint16_t t[8];
#pragma clang loop unroll(full)
      for (unsigned i = 0; i < 8; ++i)
        t[i] = mlk_scalar_compress_d11(args.input_coeffs[8 * lane + i]);
      uint8_t* r = args.bytes + 11 * lane;
      r[0] = (uint8_t)t[0];
      r[1] = (uint8_t)((t[0] >> 8) | (t[1] << 3));
      r[2] = (uint8_t)((t[1] >> 5) | (t[2] << 6));
      r[3] = (uint8_t)(t[2] >> 2);
      r[4] = (uint8_t)((t[2] >> 10) | (t[3] << 1));
      r[5] = (uint8_t)((t[3] >> 7) | (t[4] << 4));
      r[6] = (uint8_t)((t[4] >> 4) | (t[5] << 7));
      r[7] = (uint8_t)(t[5] >> 1);
      r[8] = (uint8_t)((t[5] >> 9) | (t[6] << 2));
      r[9] = (uint8_t)((t[6] >> 6) | (t[7] << 5));
      r[10] = (uint8_t)(t[7] >> 3);
    }
  } else {
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 4; ++k) {
      const unsigned group = lane + 32 * k;
      const unsigned byte = 3 * group;
      const unsigned coeff = 2 * group;
      if (args.decode) {
        args.coeffs[coeff] = (int16_t)(args.input_bytes[byte] |
            ((uint16_t)args.input_bytes[byte + 1] << 8)) & 0xfff;
        args.coeffs[coeff + 1] =
            (int16_t)(args.input_bytes[byte + 1] >> 4 |
                      ((uint16_t)args.input_bytes[byte + 2] << 4));
      } else {
        const uint16_t t0 = (uint16_t)args.input_coeffs[coeff];
        const uint16_t t1 = (uint16_t)args.input_coeffs[coeff + 1];
        args.bytes[byte] = (uint8_t)t0;
        args.bytes[byte + 1] = (uint8_t)((t0 >> 8) | (t1 << 4));
        args.bytes[byte + 2] = (uint8_t)(t1 >> 4);
      }
    }
  }
  __syncthreads();
}

#if __riscv_xlen == 64
#define MLK_CODEC_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLK_CODEC_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLK_CODEC_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLK_CODEC_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

extern "C" __attribute__((naked, noinline)) void mlk_profile_codec_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLK_CODEC_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mlk_profile_codec_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLK_CODEC_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

static void mlk_profile_codec_dispatch() {
#if defined(PQC_PROFILE_PHASES) && !defined(PQC_PROFILE_PRIMITIVES)
  const mlk_phase_scope_t scope = mlk_phase_begin();
#endif
  vx_fence();
  __syncthreads();
  mlk_profile_codec_expand();
#if defined(PQC_PROFILE_PHASES) && !defined(PQC_PROFILE_PRIMITIVES)
  mlk_phase_end(MLK_PHASE_CODEC, scope);
#endif
}

extern "C" void mlk_profile_tobytes(uint8_t* r, const int16_t* a) {
  auto& args = mlk_codec_args[vx_warp_id()];
  args.bytes = r;
  args.input_coeffs = a;
  args.bits = 12;
  args.decode = 0;
  mlk_profile_codec_dispatch();
}

extern "C" void mlk_profile_frombytes(int16_t* a, const uint8_t* r) {
  auto& args = mlk_codec_args[vx_warp_id()];
  args.input_bytes = r;
  args.coeffs = a;
  args.bits = 12;
  args.decode = 1;
  mlk_profile_codec_dispatch();
}

extern "C" void mlk_profile_compress(uint8_t* r, const int16_t* a,
                                      unsigned bits) {
  auto& args = mlk_codec_args[vx_warp_id()];
  args.bytes = r;
  args.input_coeffs = a;
  args.bits = bits;
  args.decode = 0;
  mlk_profile_codec_dispatch();
}

extern "C" void mlk_profile_decompress(int16_t* r, const uint8_t* a,
                                        unsigned bits) {
  auto& args = mlk_codec_args[vx_warp_id()];
  args.input_bytes = a;
  args.coeffs = r;
  args.bits = bits;
  args.decode = 1;
  mlk_profile_codec_dispatch();
}

#undef MLK_CODEC_RESTORE_RA
#undef MLK_CODEC_SAVE_RA

#endif
