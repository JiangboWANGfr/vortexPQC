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
`include "VX_pqc_unit_define.vh"

// Synthesis wrapper for the isolated PQC PE, the shape hw/unittest/om_core does.
// Every interface signal is a flat port so nothing is optimised away, and the
// clock port MUST be named `clk`: hw/syn/xilinx/dut/project.tcl:169 emits
// `create_clock ... [get_ports clk]`.
//
// The two sweep axes live in VX_pqc_unit_define.vh.

module VX_pqc_unit_top #(
    parameter NUM_LANES = `PQC_TOP_LANES,
    parameter UNROLL    = `PQC_TOP_UNROLL
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
    VX_pqc_unit #(
        .NUM_LANES (NUM_LANES),
        .UNROLL    (UNROLL)
    ) pqc_unit (
        .clk       (clk),
        .reset     (reset),
        .req_valid (req_valid),
        .req_state (req_state),
        .req_ready (req_ready),
        .rsp_valid (rsp_valid),
        .rsp_state (rsp_state),
        .rsp_ready (rsp_ready)
    );

endmodule
