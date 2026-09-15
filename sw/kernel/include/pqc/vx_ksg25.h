#ifndef __VX_KSG25_H__
#define __VX_KSG25_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// All 32 lanes participate; lanes 0–24 hold the state and lanes 25–31 return zero.
#if __riscv_xlen == 32
static inline uint32_t vx_ktheta_l_sg25(uint32_t lo, uint32_t hi) {
    uint32_t result;
    __asm__ volatile (".insn r 0x0b, 0, 0x06, %0, %1, %2"
        : "=r" (result) : "r" (lo), "r" (hi));
    return result;
}

static inline uint32_t vx_ktheta_h_sg25(uint32_t lo, uint32_t hi) {
    uint32_t result;
    __asm__ volatile (".insn r 0x0b, 1, 0x06, %0, %1, %2"
        : "=r" (result) : "r" (lo), "r" (hi));
    return result;
}

static inline uint32_t vx_krhopi_l_sg25(uint32_t lo, uint32_t hi) {
    uint32_t result;
    __asm__ volatile (".insn r 0x0b, 2, 0x06, %0, %1, %2"
        : "=r" (result) : "r" (lo), "r" (hi));
    return result;
}

static inline uint32_t vx_krhopi_h_sg25(uint32_t lo, uint32_t hi) {
    uint32_t result;
    __asm__ volatile (".insn r 0x0b, 3, 0x06, %0, %1, %2"
        : "=r" (result) : "r" (lo), "r" (hi));
    return result;
}

// Both CHII forms require the same round in 0..23 across all 32 lanes.
static inline uint32_t vx_kchii_l_sg25(uint32_t state, uint32_t round) {
    uint32_t result;
    __asm__ volatile (".insn r 0x0b, 4, 0x06, %0, %1, %2"
        : "=r" (result) : "r" (state), "r" (round));
    return result;
}

static inline uint32_t vx_kchii_h_sg25(uint32_t state, uint32_t round) {
    uint32_t result;
    __asm__ volatile (".insn r 0x0b, 5, 0x06, %0, %1, %2"
        : "=r" (result) : "r" (state), "r" (round));
    return result;
}
#elif __riscv_xlen == 64
static inline uint64_t vx_ktheta_sg25(uint64_t state) {
    uint64_t result;
    __asm__ volatile (".insn r 0x0b, 0, 0x06, %0, %1, x0"
        : "=r" (result) : "r" (state));
    return result;
}

static inline uint64_t vx_krhopi_sg25(uint64_t state) {
    uint64_t result;
    __asm__ volatile (".insn r 0x0b, 2, 0x06, %0, %1, x0"
        : "=r" (result) : "r" (state));
    return result;
}

// The round must be uniform in 0..23 across all 32 lanes.
static inline uint64_t vx_kchii_sg25(uint64_t state, uint64_t round) {
    uint64_t result;
    __asm__ volatile (".insn r 0x0b, 4, 0x06, %0, %1, %2"
        : "=r" (result) : "r" (state), "r" (round));
    return result;
}
#else
#error "KSG25 requires XLEN=32 or XLEN=64"
#endif

#ifdef __cplusplus
}
#endif

#endif
