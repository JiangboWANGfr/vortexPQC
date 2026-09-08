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

// FIPS-202 backend: the KECCAKF instruction.
//
// Selected with KECCAK=pe, and it is a THIRD column beside the reference C and
// the PQRV assembly, taken on the same source tree with the same coins and the
// same permutation count. That is the whole point of routing it through the
// library's own hook: the three arms differ by one header and nothing else, so
// the difference between them is the backend and not the experiment.
//
// This is where the entire ISA extension touches the software. Two lines.

#ifndef VORTEX_KECCAK_PE_H
#define VORTEX_KECCAK_PE_H

#if !defined(__ASSEMBLER__)

#include "src/fips202/native/api.h"
#include <vx_pqc.h>

#define MLK_USE_NATIVE_FIPS202_X1
static MLK_INLINE int mlk_keccak_f1600_x1_native(uint64_t *state)
{
  vx_keccakf(state);
  return MLK_NATIVE_FUNC_SUCCESS;
}

// x4 is deliberately not hooked, for the reason the PQRV backend gives:
// mlk_keccakf1600x4_permute falls back to four x1 calls, so the batched path
// gets the instruction too, and leaving x4 alone keeps the permutation count
// identical across all three columns. Comparing arms that ran different numbers
// of permutations would not be comparing arms.
//
// It also keeps this first measurement honest about what is being measured. Each
// of those four x1 calls is one KECCAKF on one lane; whether four lanes should
// instead issue four in parallel is the KECCAKF_L question, and it belongs to a
// hooked x4, not here.

#endif /* !__ASSEMBLER__ */
#endif /* VORTEX_KECCAK_PE_H */
