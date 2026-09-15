#ifndef __VX_KROUND25_H__
#define __VX_KROUND25_H__

#include <stdint.h>

// The round must be an integer constant in 0..23; all 32 lanes participate.
#if __riscv_xlen == 32
#define vx_kround_l_sg25(lo, hi, round) __extension__ ({                  \
    uint32_t __result;                                                    \
    __asm__ volatile (".insn r 0x5b, 0, %3, %0, %1, %2"                 \
        : "=r" (__result) : "r" (lo), "r" (hi), "i" (round));          \
    __result;                                                            \
})

#define vx_kround_h_sg25(lo, hi, round) __extension__ ({                  \
    uint32_t __result;                                                    \
    __asm__ volatile (".insn r 0x5b, 1, %3, %0, %1, %2"                 \
        : "=r" (__result) : "r" (lo), "r" (hi), "i" (round));          \
    __result;                                                            \
})
#elif __riscv_xlen == 64
#define vx_kround_sg25(state, round) __extension__ ({                     \
    uint64_t __result;                                                    \
    __asm__ volatile (".insn r 0x5b, 0, %2, %0, %1, x0"                 \
        : "=r" (__result) : "r" (state), "i" (round));                \
    __result;                                                            \
})
#else
#error "KROUND25 requires XLEN=32 or XLEN=64"
#endif

#endif
