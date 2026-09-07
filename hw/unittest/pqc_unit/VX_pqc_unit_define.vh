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

// The isolated PQC PE's two sweep axes, as compile-time defines rather than
// VX_config.toml keys. That is deliberate: this DUT measures an ISOLATED PE and
// must not perturb the resolved config of any build that has already been
// measured. Integration into VX_core moves them to VX_CFG_NUM_PQC_LANES.
//
//   PQC_TOP_LANES  -- 1 is a per-core PE, SIMD_WIDTH is a per-lane PE.
//                     This one parameter is the whole per-core vs per-lane
//                     argument; there are not two RTL variants.
//   PQC_TOP_UNROLL -- Keccak rounds computed per cycle. Latency is 24/UNROLL
//                     cycles and the round logic is replicated UNROLL times,
//                     so this is the area/latency axis. Must divide 24.

`ifndef VX_PQC_UNIT_DEFINE_VH
`define VX_PQC_UNIT_DEFINE_VH

`ifndef PQC_TOP_LANES
`define PQC_TOP_LANES 1
`endif

`ifndef PQC_TOP_UNROLL
`define PQC_TOP_UNROLL 1
`endif

`endif // VX_PQC_UNIT_DEFINE_VH
