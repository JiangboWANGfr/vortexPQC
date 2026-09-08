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

#pragma once

#include <cstdint>

// Keccak-f1600, the functional model behind PqcType::KECCAKF.
//
// It must agree bit for bit with two other implementations or the KATs fail:
// mlk_keccakf1600_permute_c in the pristine submodule, which is what the
// software baseline runs, and hw/rtl/pqc/VX_pqc_keccak_f1600.sv, which is what
// the hardware runs. All three use the same word order -- A[x][y] is state[x+5y]
// -- so the same 200 bytes mean the same thing everywhere and no permutation is
// needed at any boundary.
//
// Deliberately depends on nothing from sw/kernel or the submodules: this is the
// simulator's own reference, and a shared header would make the three agree by
// construction instead of by test.

namespace vortex {
namespace pqc {

inline void keccak_f1600(uint64_t state[25]) {
  static const uint64_t RC[24] = {
    0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull,
    0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
    0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull,
    0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
    0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull,
    0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
    0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull,
    0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull,
  };
  // rho offsets r[x][y], flattened x*5 + y -- the transpose of how FIPS 202
  // Table 2 prints them, matching the RTL's KS_RHO for the same reason.
  static const unsigned RHO[25] = {
     0, 36,  3, 41, 18,
     1, 44, 10, 45,  2,
    62,  6, 43, 15, 61,
    28, 55, 25, 21, 56,
    27, 20, 39,  8, 14,
  };
  auto rol = [](uint64_t v, unsigned n) -> uint64_t {
    return n ? ((v << n) | (v >> (64 - n))) : v;
  };
  for (unsigned round = 0; round < 24; ++round) {
    uint64_t c[5], d[5], t[25], b[25];
    // theta
    for (unsigned x = 0; x < 5; ++x) {
      c[x] = state[x] ^ state[x+5] ^ state[x+10] ^ state[x+15] ^ state[x+20];
    }
    for (unsigned x = 0; x < 5; ++x) {
      d[x] = c[(x+4)%5] ^ rol(c[(x+1)%5], 1);
    }
    for (unsigned x = 0; x < 5; ++x) {
      for (unsigned y = 0; y < 5; ++y) {
        t[x + 5*y] = state[x + 5*y] ^ d[x];
      }
    }
    // rho and pi: B[y][(2x+3y) mod 5] = ROL64(A[x][y], r[x][y])
    for (unsigned x = 0; x < 5; ++x) {
      for (unsigned y = 0; y < 5; ++y) {
        b[y + 5*((2*x + 3*y) % 5)] = rol(t[x + 5*y], RHO[x*5 + y]);
      }
    }
    // chi
    for (unsigned x = 0; x < 5; ++x) {
      for (unsigned y = 0; y < 5; ++y) {
        state[x + 5*y] = b[x + 5*y] ^ ((~b[((x+1)%5) + 5*y]) & b[((x+2)%5) + 5*y]);
      }
    }
    // iota, on A[0][0] only
    state[0] ^= RC[round];
  }
}

} // namespace pqc
} // namespace vortex
