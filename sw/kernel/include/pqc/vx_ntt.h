// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __VX_NTT_H__
#define __VX_NTT_H__

#include "vx_pqc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// ML-KEM Montgomery multiplication. Both operands are interpreted as signed
// 16-bit values; the signed 16-bit result is sign-extended in rd.
static inline int16_t vx_nttmul_k(int16_t a, int16_t b) {
    intptr_t result;
    const intptr_t lhs = a;
    const intptr_t rhs = b;
    __asm__ volatile (".insn r %[op], %[f3], %[f7], %[rd], %[rs1], %[rs2]"
        : [rd] "=r" (result)
        : [op]  "i" (VX_PQC_EXT_OPCODE),
          [f3]  "i" (VX_PQC_F3_NTTMUL_K),
          [f7]  "i" (VX_PQC_FUNCT7),
          [rs1] "r" (lhs),
          [rs2] "r" (rhs));
    return (int16_t)result;
}

static inline int32_t vx_nttmul_d(int32_t a, int32_t b) {
    intptr_t result;
    __asm__ volatile (".insn r %[op], %[f3], %[f7], %[rd], %[rs1], %[rs2]"
        : [rd] "=r" (result)
        : [op]  "i" (VX_PQC_EXT_OPCODE),
          [f3]  "i" (VX_PQC_F3_NTTMUL_D),
          [f7]  "i" (VX_PQC_FUNCT7),
          [rs1] "r" ((intptr_t)a),
          [rs2] "r" ((intptr_t)b));
    return (int32_t)result;
}

#define VX_NTTBF_D_DEFINE(name, funct7, stage)                            \
static inline __attribute__((always_inline)) int32_t name(                \
    int32_t value, int32_t zeta) {                                       \
    intptr_t result;                                                     \
    __asm__ volatile (".insn r %[op], %[f3], %[f7], %[rd], %[rs1], %[rs2]" \
        : [rd] "=r" (result)                                             \
        : [op]  "i" (VX_PQC_EXT_OPCODE),                                \
          [f3]  "i" (stage),                                            \
          [f7]  "i" (funct7),                                           \
          [rs1] "r" ((intptr_t)value),                                  \
          [rs2] "r" ((intptr_t)zeta));                                  \
    return (int32_t)result;                                              \
}

VX_NTTBF_D_DEFINE(vx_nttbf_ct_d_xor1,  VX_PQC_F7_NTTBF_CT_D, VX_PQC_F3_NTTBF_XOR1)
VX_NTTBF_D_DEFINE(vx_nttbf_ct_d_xor2,  VX_PQC_F7_NTTBF_CT_D, VX_PQC_F3_NTTBF_XOR2)
VX_NTTBF_D_DEFINE(vx_nttbf_ct_d_xor4,  VX_PQC_F7_NTTBF_CT_D, VX_PQC_F3_NTTBF_XOR4)
VX_NTTBF_D_DEFINE(vx_nttbf_ct_d_xor8,  VX_PQC_F7_NTTBF_CT_D, VX_PQC_F3_NTTBF_XOR8)
VX_NTTBF_D_DEFINE(vx_nttbf_ct_d_xor16, VX_PQC_F7_NTTBF_CT_D, VX_PQC_F3_NTTBF_XOR16)
VX_NTTBF_D_DEFINE(vx_nttbf_gs_d_xor1,  VX_PQC_F7_NTTBF_GS_D, VX_PQC_F3_NTTBF_XOR1)
VX_NTTBF_D_DEFINE(vx_nttbf_gs_d_xor2,  VX_PQC_F7_NTTBF_GS_D, VX_PQC_F3_NTTBF_XOR2)
VX_NTTBF_D_DEFINE(vx_nttbf_gs_d_xor4,  VX_PQC_F7_NTTBF_GS_D, VX_PQC_F3_NTTBF_XOR4)
VX_NTTBF_D_DEFINE(vx_nttbf_gs_d_xor8,  VX_PQC_F7_NTTBF_GS_D, VX_PQC_F3_NTTBF_XOR8)
VX_NTTBF_D_DEFINE(vx_nttbf_gs_d_xor16, VX_PQC_F7_NTTBF_GS_D, VX_PQC_F3_NTTBF_XOR16)

#undef VX_NTTBF_D_DEFINE

// Collective contract: exactly 32 ALU lanes, both lanes of every XOR pair
// active, and the pair-low lane supplies the signed-16 twiddle in rs2.
#define VX_NTTBF_K_DEFINE(name, funct7, stage)                            \
static inline __attribute__((always_inline)) int16_t name(                \
    int16_t value, int16_t zeta) {                                       \
    intptr_t result;                                                     \
    const intptr_t lane_value = value;                                   \
    const intptr_t lane_zeta = zeta;                                     \
    __asm__ volatile (".insn r %[op], %[f3], %[f7], %[rd], %[rs1], %[rs2]" \
        : [rd] "=r" (result)                                             \
        : [op]  "i" (VX_PQC_EXT_OPCODE),                                \
          [f3]  "i" (stage),                                            \
          [f7]  "i" (funct7),                                           \
          [rs1] "r" (lane_value),                                       \
          [rs2] "r" (lane_zeta));                                       \
    return (int16_t)result;                                              \
}

VX_NTTBF_K_DEFINE(vx_nttbf_ct_k_xor1,  VX_PQC_F7_NTTBF_CT_K, VX_PQC_F3_NTTBF_XOR1)
VX_NTTBF_K_DEFINE(vx_nttbf_ct_k_xor2,  VX_PQC_F7_NTTBF_CT_K, VX_PQC_F3_NTTBF_XOR2)
VX_NTTBF_K_DEFINE(vx_nttbf_ct_k_xor4,  VX_PQC_F7_NTTBF_CT_K, VX_PQC_F3_NTTBF_XOR4)
VX_NTTBF_K_DEFINE(vx_nttbf_ct_k_xor8,  VX_PQC_F7_NTTBF_CT_K, VX_PQC_F3_NTTBF_XOR8)
VX_NTTBF_K_DEFINE(vx_nttbf_ct_k_xor16, VX_PQC_F7_NTTBF_CT_K, VX_PQC_F3_NTTBF_XOR16)
VX_NTTBF_K_DEFINE(vx_nttbf_gs_k_xor1,  VX_PQC_F7_NTTBF_GS_K, VX_PQC_F3_NTTBF_XOR1)
VX_NTTBF_K_DEFINE(vx_nttbf_gs_k_xor2,  VX_PQC_F7_NTTBF_GS_K, VX_PQC_F3_NTTBF_XOR2)
VX_NTTBF_K_DEFINE(vx_nttbf_gs_k_xor4,  VX_PQC_F7_NTTBF_GS_K, VX_PQC_F3_NTTBF_XOR4)
VX_NTTBF_K_DEFINE(vx_nttbf_gs_k_xor8,  VX_PQC_F7_NTTBF_GS_K, VX_PQC_F3_NTTBF_XOR8)
VX_NTTBF_K_DEFINE(vx_nttbf_gs_k_xor16, VX_PQC_F7_NTTBF_GS_K, VX_PQC_F3_NTTBF_XOR16)

#undef VX_NTTBF_K_DEFINE

#ifdef __cplusplus
}
#endif

#endif // __VX_NTT_H__
