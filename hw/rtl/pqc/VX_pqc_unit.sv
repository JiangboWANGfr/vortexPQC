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

`include "VX_define.vh"

// VX_pqc_unit: NUM_LANES Keccak-f1600 engines behind one per-lane handshake.
//
// The replication factor is the whole per-core versus per-lane argument, and it
// is ONE parameter rather than two RTL variants: NUM_LANES = 1 is a per-core PE,
// NUM_LANES = SIMD_WIDTH is a per-lane PE. Same idiom as VX_alu_unit.sv:31-32.
//
// The 1600-bit state buses are flat rather than packed 2D arrays. That is what
// the synthesis wrapper needs anyway (nothing optimised away), and it is what
// yosys 0.33 can parse -- its Verilog frontend rejects a packed 2D port.
//
// The instance name below is load-bearing. `grep -i pqc post_impl_util.rpt`
// matches on the INSTANCE path, not the module name, so the hierarchical area
// report can only separate the engines from the rest of the core if they are
// named g_pqc_lanes[*].pqc_pe.

module VX_pqc_unit #(
    parameter NUM_LANES = 1,
    parameter UNROLL    = 1
) (
    input  wire                             clk,
    input  wire                             reset,

    input  wire [NUM_LANES-1:0]             req_valid,
    input  wire [NUM_LANES*1600-1:0]        req_state,
    output wire [NUM_LANES-1:0]             req_ready,

    output wire [NUM_LANES-1:0]             rsp_valid,
    output wire [NUM_LANES*1600-1:0]        rsp_state,
    input  wire [NUM_LANES-1:0]             rsp_ready
);
    for (genvar i = 0; i < NUM_LANES; i = i + 1) begin : g_pqc_lanes
        VX_pqc_keccak_f1600 #(
            .UNROLL (UNROLL)
        ) pqc_pe (
            .clk       (clk),
            .reset     (reset),
            .req_valid (req_valid[i]),
            .req_state (req_state[1600*i +: 1600]),
            .req_ready (req_ready[i]),
            .rsp_valid (rsp_valid[i]),
            .rsp_state (rsp_state[1600*i +: 1600]),
            .rsp_ready (rsp_ready[i])
        );
    end

endmodule
