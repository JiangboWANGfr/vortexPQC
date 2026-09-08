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

#include "pqc_unit.h"
#include "keccak_f1600.h"
#include "core.h"
#include "processor_impl.h"   // ProcessorImpl::ram() -- functional store
#include <cstring>

using namespace vortex;

void PqcUnit::execute(instr_trace_t* trace) {
  auto ram = core_->processor()->ram();
  auto& tmask = trace->tmask;
  auto& rs1_data = trace->src_data[0];

  for (uint32_t t = 0; t < VX_CFG_NUM_THREADS; ++t) {
    if (!tmask.test(t))
      continue;
    // rs1 is a byte address, 8-byte aligned, of 25 uint64_t. The library hands
    // it straight through from mlk_keccak_f1600_x1_native, so the word order is
    // the library's: A[x][y] at index x + 5y.
    uint64_t addr = rs1_data[t].u;
    uint64_t state[25];
    ram->read(state, addr, sizeof(state));
    pqc::keccak_f1600(state);
    ram->write(state, addr, sizeof(state));
  }
}

uint32_t PqcUnit::latency(const instr_trace_t* trace) const {
  uint32_t jobs = 0;
  for (uint32_t t = 0; t < VX_CFG_NUM_THREADS; ++t) {
    if (trace->tmask.test(t))
      ++jobs;
  }
  if (jobs == 0)
    return VX_CFG_PQC_MEM_LATENCY;
  const uint32_t engines = VX_CFG_PQC_NUM_ENGINES;
  const uint32_t passes = (jobs + engines - 1) / engines;
  return VX_CFG_PQC_MEM_LATENCY + VX_CFG_PQC_KECCAK_LATENCY * passes;
}
