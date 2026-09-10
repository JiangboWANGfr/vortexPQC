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

module VX_pqc_ntt_top import VX_gpu_pkg::*; (
    input  wire                              clk,
    input  wire                              reset,

    input  wire                              execute_valid,
    output wire                              execute_ready,
    input  wire [$bits(alu_execute_t)-1:0]   execute_data,

    output wire                              result_valid,
    input  wire                              result_ready,
    output wire [$bits(alu_result_t)-1:0]    result_data
);
    VX_execute_if #(
        .data_t (alu_execute_t)
    ) execute_if();

    VX_result_if #(
        .data_t (alu_result_t)
    ) result_if();

    assign execute_if.valid = execute_valid;
    assign execute_ready = execute_if.ready;
    assign execute_if.data = execute_data;

    assign result_valid = result_if.valid;
    assign result_if.ready = result_ready;
    assign result_data = result_if.data;

    VX_pqc_nttmul #(
        .INSTANCE_ID ("pqc_ntt_top"),
        .NUM_LANES   (`VX_CFG_NUM_ALU_LANES)
    ) pqc_ntt (
        .clk        (clk),
        .reset      (reset),
        .execute_if (execute_if),
        .result_if  (result_if)
    );

endmodule
