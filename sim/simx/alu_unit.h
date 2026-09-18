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

#include "func_unit.h"

namespace vortex {

class AluUnit : public FuncUnit<VX_CFG_NUM_ALU_BLOCKS> {
public:
#if defined(VX_CFG_EXT_KSG25_ENABLE) || defined(VX_CFG_EXT_KROUND25_ENABLE)
  std::array<SimChannel<uint32_t>, VX_CFG_NUM_ALU_BLOCKS> DispatchRelease;
#endif
  AluUnit(const SimContext& ctx, const char* name, Core*);

protected:
  void on_reset() override;
  void on_tick() override;

private:
  // Per-unit functional execution. Called only from this unit's tick().
  // Reads operands from trace->src_data, populates trace->dst_data,
  // mutates warp.PC for branches.
  void execute(instr_trace_t* trace);

  uint32_t latency_of(const instr_trace_t* trace) const;

#if defined(VX_CFG_EXT_KSG25_ENABLE) || defined(VX_CFG_EXT_KROUND25_ENABLE)
  static constexpr uint32_t kNumPEs = 1 + VX_CFG_EXT_M_ENABLED
                                       + VX_CFG_EXT_KSG25_ENABLED
                                       + VX_CFG_EXT_KROUND25_ENABLED
                                       + VX_CFG_EXT_NTT_ENABLED;
  std::array<SimChannel<instr_trace_t*>, VX_CFG_NUM_ALU_BLOCKS> int_results_;
#ifdef VX_CFG_EXT_M_ENABLE
  std::array<SimChannel<instr_trace_t*>, VX_CFG_NUM_ALU_BLOCKS> mdv_results_;
#endif
#ifdef VX_CFG_EXT_KSG25_ENABLE
  std::array<SimChannel<instr_trace_t*>, VX_CFG_NUM_ALU_BLOCKS> ksg25_results_;
#endif
#ifdef VX_CFG_EXT_KROUND25_ENABLE
  std::array<SimChannel<instr_trace_t*>, VX_CFG_NUM_ALU_BLOCKS> kround25_results_;
#endif
#ifdef VX_CFG_EXT_NTT_ENABLE
  struct NttBeat {
    instr_trace_t* trace = nullptr;
    bool last = false;
  };
  std::array<std::array<NttBeat, 5>, VX_CFG_NUM_ALU_BLOCKS> ntt_pipeline_{};
  std::array<instr_trace_t*, VX_CFG_NUM_ALU_BLOCKS> ntt_pending_{};
  std::array<uint32_t, VX_CFG_NUM_ALU_BLOCKS> ntt_remaining_{};
  std::array<SimChannel<instr_trace_t*>, VX_CFG_NUM_ALU_BLOCKS> ntt_results_;
#endif
  std::array<uint32_t, VX_CFG_NUM_ALU_BLOCKS> next_pe_{};
#endif
#ifdef VX_CFG_EXT_NTT_ENABLE
  std::array<uint64_t, VX_CFG_NUM_ALU_BLOCKS> ntt_ready_cycle_{};
#endif
};

}
