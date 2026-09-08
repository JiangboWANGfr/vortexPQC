// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __VX_KECCAK_H__
#define __VX_KECCAK_H__

#include "vx_pqc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// KECCAKF -- permute the 25-word Keccak-f1600 state at `state`, in place.
//
// R-type, rd = x0, rs2 = x0. The only operand is the pointer, because the
// permutation has no parameters: 1600 bits in, 1600 bits out, 24 rounds. Only
// rs1 touches the register file; the 200-byte payload never does.
//
// Blocking. It returns when the state in memory has been permuted, which is
// what lets it sit inside mlk_keccak_f1600_x1_native without the library
// knowing anything happened. "memory" in the clobber list is doing real work:
// the compiler must not keep any of those 200 bytes in a register across it.
//
// Each active lane permutes the state ITS OWN rs1 names. On the library's x4
// path the four sub-states are four distinct pointers, so four lanes permute
// four states; on the x1 path every lane names its own stack copy of the same
// bytes and does the same work redundantly, exactly as the software does today.
static inline void vx_keccakf(uint64_t* state) {
    __asm__ volatile (".insn r %[op], %[f3], %[f7], x0, %[p], x0"
        :
        : [op] "i" (VX_PQC_EXT_OPCODE),
          [f3] "i" (VX_PQC_F3_KECCAKF),
          [f7] "i" (VX_PQC_FUNCT7),
          [p]  "r" (state)
        : "memory");
}

#ifdef __cplusplus
}
#endif

#endif // __VX_KECCAK_H__
