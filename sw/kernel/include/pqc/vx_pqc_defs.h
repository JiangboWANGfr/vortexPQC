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

#ifndef __VX_PQC_DEFS_H__
#define __VX_PQC_DEFS_H__

// Shared definitions for the PQC extension's kernel-side API.
//
// FIPS 203 / FIPS 204 constants are NOT mirrored here: mlkem-native and
// mldsa-native already define them, the tests build against those, and a third
// copy would be one more thing to drift. What belongs here is what the
// extension itself owns -- instruction encodings and the types its intrinsics
// exchange.

#include <vx_intrinsics.h>   /* RISCV_CUSTOM0, vx_intrinsics.h:34 */
#include <stdint.h>

// INST_EXT1. funct7 rows 0x00-0x04 are taken (TMC/WSPAWN/SPLIT group, VOTE+SHFL,
// TCU, DXA, load packing); 0x05 is the first free one.
#define VX_PQC_EXT_OPCODE   RISCV_CUSTOM0   /* 0x0B */
#define VX_PQC_FUNCT7       0x05

// funct3 within row 0x05.
//
// ONE instruction, and synchronous. This supersedes the asynchronous
// launch/handle/wait triple that next_steps_recipes.md Stage 1 specifies:
// pqc/docs/proposals/keccak_ise_proposal.md S3.2 rejects async because the
// library hook it has to live inside -- mlk_keccak_f1600_x1_native(uint64_t*) --
// is a synchronous C call with nowhere for a handle to survive, so a launch is
// always followed immediately by its wait. S2.0 makes the wider argument that
// Keccak-f1600 has no parameters at all: 1600 bits in, 1600 bits out, 24 rounds,
// no options, leaving only "where is the state" (rs1) to encode.
//
// funct3 = 1 (KECCAKF_U, execute once for the whole warp) is reserved but NOT
// implemented. S2.1 was corrected: every state reaching the x1 hook is a stack
// automatic and vx_start.S:95 gives each hart its own slab, so the pointers are
// distinct and a per-lane instruction is already correct on both paths. U would
// be a de-duplication optimisation for the redundant-SPMD x1 path, worth
// measuring before it is worth encoding.
#define VX_PQC_F3_KECCAKF   0   /* per-lane: permute the state each active lane names */

#define VX_PQC_KECCAK_LANES 25  /* uint64_t words in a Keccak-f1600 state */
#define VX_PQC_KECCAK_WAY   4   /* sub-states in the library's x4 hook */

#endif // __VX_PQC_DEFS_H__
