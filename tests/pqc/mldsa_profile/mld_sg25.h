#ifndef MLD_SG25_H
#define MLD_SG25_H

#include <pqc/vx_ksg25.h>
#include <pqc/vx_kround25.h>

static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "SG25 requires a complete 32-lane ALU vector");

#if defined(PQC_KECCAK_SG25_SW)
static const uint8_t mldsg_rho[25] = {
  0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
  25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
};

static const uint64_t mldsg_round_constants[24] = {
  UINT64_C(0x0000000000000001), UINT64_C(0x0000000000008082),
  UINT64_C(0x800000000000808a), UINT64_C(0x8000000080008000),
  UINT64_C(0x000000000000808b), UINT64_C(0x0000000080000001),
  UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008009),
  UINT64_C(0x000000000000008a), UINT64_C(0x0000000000000088),
  UINT64_C(0x0000000080008009), UINT64_C(0x000000008000000a),
  UINT64_C(0x000000008000808b), UINT64_C(0x800000000000008b),
  UINT64_C(0x8000000000008089), UINT64_C(0x8000000000008003),
  UINT64_C(0x8000000000008002), UINT64_C(0x8000000000000080),
  UINT64_C(0x000000000000800a), UINT64_C(0x800000008000000a),
  UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008080),
  UINT64_C(0x0000000080000001), UINT64_C(0x8000000080008008),
};

static inline uint64_t mldsg_shuffle(uint64_t value, unsigned source) {
#if __riscv_xlen == 64
  return vx_shfl_idx(value, source, 31, 0);
#else
  uint32_t lo = vx_shfl_idx((uint32_t)value, source, 31, 0);
  uint32_t hi = vx_shfl_idx((uint32_t)(value >> 32), source, 31, 0);
  return ((uint64_t)hi << 32) | lo;
#endif
}

static inline uint64_t mldsg_rotate(uint64_t value, unsigned shift) {
  return (value << shift) | (value >> ((64 - shift) & 63));
}
#endif

#if defined(PQC_KECCAK_KROUND25)
#if __riscv_xlen == 64
#define MLDSG_KROUND_STEP(round) do {                                     \
  a = vx_kround_sg25(a, round);                                          \
} while (0)
#else
#define MLDSG_KROUND_STEP(round) do {                                     \
  const uint32_t lo = (uint32_t)a;                                       \
  const uint32_t hi = (uint32_t)(a >> 32);                               \
  const uint32_t out_lo = vx_kround_l_sg25(lo, hi, round);               \
  const uint32_t out_hi = vx_kround_h_sg25(lo, hi, round);               \
  a = ((uint64_t)out_hi << 32) | out_lo;                                 \
} while (0)
#endif
#endif

static __attribute__((noinline)) uint64_t mldsg_permute(uint64_t a) {
#if defined(PQC_KECCAK_KROUND25)
  MLDSG_KROUND_STEP(0);  MLDSG_KROUND_STEP(1);
  MLDSG_KROUND_STEP(2);  MLDSG_KROUND_STEP(3);
  MLDSG_KROUND_STEP(4);  MLDSG_KROUND_STEP(5);
  MLDSG_KROUND_STEP(6);  MLDSG_KROUND_STEP(7);
  MLDSG_KROUND_STEP(8);  MLDSG_KROUND_STEP(9);
  MLDSG_KROUND_STEP(10); MLDSG_KROUND_STEP(11);
  MLDSG_KROUND_STEP(12); MLDSG_KROUND_STEP(13);
  MLDSG_KROUND_STEP(14); MLDSG_KROUND_STEP(15);
  MLDSG_KROUND_STEP(16); MLDSG_KROUND_STEP(17);
  MLDSG_KROUND_STEP(18); MLDSG_KROUND_STEP(19);
  MLDSG_KROUND_STEP(20); MLDSG_KROUND_STEP(21);
  MLDSG_KROUND_STEP(22); MLDSG_KROUND_STEP(23);
#else
  unsigned round;
#if defined(PQC_KECCAK_SG25_SW)
  const unsigned lane = (unsigned)vx_thread_id();
  const unsigned t = lane % 25;
  const unsigned x = t % 5;
  const unsigned y = t / 5;
#endif
#if defined(PQC_KECCAK_UNROLL)
#pragma clang loop unroll(full)
#else
#pragma clang loop unroll(disable)
#endif
  for (round = 0; round < 24; ++round) {
#if defined(PQC_KECCAK_SG25_SW)
    const uint64_t pair = a ^ mldsg_shuffle(a, x + 5 * ((y + 1) % 5));
    const uint64_t four = pair ^ mldsg_shuffle(pair, x + 5 * ((y + 2) % 5));
    const uint64_t column = four ^ mldsg_shuffle(a, x + 5 * ((y + 4) % 5));
    a ^= mldsg_shuffle(column, (x + 4) % 5)
       ^ mldsg_rotate(mldsg_shuffle(column, (x + 1) % 5), 1);
    const uint64_t rotated = mldsg_rotate(a, mldsg_rho[t]);
    const uint64_t b = mldsg_shuffle(rotated,
                                     (x + 3 * y) % 5 + 5 * x);
    a = b ^ (~mldsg_shuffle(b, (x + 1) % 5 + 5 * y)
           & mldsg_shuffle(b, (x + 2) % 5 + 5 * y));
    const uint64_t mask = UINT64_C(0) - (uint64_t)(lane == 0);
    a ^= mldsg_round_constants[round] & mask;
#else
#if __riscv_xlen == 64
    a = vx_ktheta_sg25(a);
    a = vx_krhopi_sg25(a);
    a = vx_kchii_sg25(a, round);
#else
    uint32_t alo = (uint32_t)a;
    uint32_t ahi = (uint32_t)(a >> 32);
    uint32_t tlo = vx_ktheta_l_sg25(alo, ahi);
    uint32_t thi = vx_ktheta_h_sg25(alo, ahi);
    uint32_t blo = vx_krhopi_l_sg25(tlo, thi);
    uint32_t bhi = vx_krhopi_h_sg25(tlo, thi);
    alo = vx_kchii_l_sg25(blo, round);
    ahi = vx_kchii_h_sg25(bhi, round);
    a = ((uint64_t)ahi << 32) | alo;
#endif
#endif
  }
#endif
  return a;
}

#if defined(PQC_KECCAK_KROUND25)
#undef MLDSG_KROUND_STEP
#endif

static uint64_t* mld_sg25_state[MLD_PROF_SLOTS];

extern "C" __attribute__((noinline, used)) void mld_profile_keccak_lanes() {
  uint64_t* state = mld_sg25_state[mld_prof_slot()];
  const unsigned lane = vx_thread_id();
  uint64_t a = 0;
  if (lane < 25) {
    a = state[lane];
  }
  a = mldsg_permute(a);
  if (lane < 25) {
    state[lane] = a;
  }
  __syncthreads();
}

#if __riscv_xlen == 64
#define MLDSG_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLDSG_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLDSG_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLDSG_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

// Only the leader has a caller frame; workers retain their KMU-initialized stacks.
extern "C" __attribute__((naked, noinline)) void mld_profile_keccak_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLDSG_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mld_profile_keccak_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLDSG_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

#undef MLDSG_SAVE_RA
#undef MLDSG_RESTORE_RA

extern "C" void mld_profile_keccak(uint64_t* state) {
  mld_sg25_state[mld_prof_slot()] = state;
  __syncthreads();
  mld_profile_keccak_expand();
}

#endif
