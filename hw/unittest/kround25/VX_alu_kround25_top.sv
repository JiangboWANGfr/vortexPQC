// Copyright © 2026
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

`include "VX_define.vh"

module VX_alu_kround25_top import VX_gpu_pkg::*; (
    input  wire                              clk,
    input  wire                              reset,
    input  wire                              req_valid,
    input  wire [$bits(alu_execute_t)-1:0]   req_data,
    output wire                              req_ready,
    output wire                              rsp_valid,
    output wire [$bits(alu_result_t)-1:0]    rsp_data,
    input  wire                              rsp_ready
);
    VX_execute_if #(
        .data_t (alu_execute_t)
    ) execute_if ();

    VX_result_if #(
        .data_t (alu_result_t)
    ) result_if ();

    assign execute_if.valid = req_valid;
    assign execute_if.data = req_data;
    assign req_ready = execute_if.ready;

    assign rsp_valid = result_if.valid;
    assign rsp_data = result_if.data;
    assign result_if.ready = rsp_ready;

    VX_alu_kround25 kround25 (
        .clk        (clk),
        .reset      (reset),
        .execute_if (execute_if),
        .result_if  (result_if)
    );

endmodule
