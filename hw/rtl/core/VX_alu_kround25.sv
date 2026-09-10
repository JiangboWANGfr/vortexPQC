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

module VX_alu_kround25 import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = ""
) (
    input wire          clk,
    input wire          reset,
    VX_execute_if.slave execute_if,
    VX_result_if.master result_if
);
    `UNUSED_SPARAM (INSTANCE_ID)
    `UNUSED_VAR ({execute_if.data.op_type,
                  execute_if.data.op_args.alu.__padding,
                  execute_if.data.op_args.alu.use_PC,
                  execute_if.data.op_args.alu.use_imm,
                  execute_if.data.op_args.alu.is_w,
                  execute_if.data.op_args.alu.xtype,
                  execute_if.data.op_args.alu.imm20[19:6],
                  execute_if.data.rs1_data[31:25], execute_if.data.rs2_data[31:25],
                  execute_if.data.rs3_data})

    `STATIC_ASSERT(`VX_CFG_XLEN == 32, ("KROUND25 requires RV32"))
    `STATIC_ASSERT(`VX_CFG_NUM_THREADS == 32, ("KROUND25 requires 32 threads"))
    `STATIC_ASSERT(`VX_CFG_SIMD_WIDTH == 32, ("KROUND25 requires SIMD width 32"))
    `STATIC_ASSERT(`VX_CFG_NUM_ALU_LANES == 32, ("KROUND25 requires 32 ALU lanes"))
    `RUNTIME_ASSERT(!execute_if.valid || &execute_if.data.header.tmask,
                    ("%t: %s: KROUND25 requires a full warp mask", $time, INSTANCE_ID))
    `RUNTIME_ASSERT(!execute_if.valid || execute_if.data.op_args.alu.imm20[4:0] < 24,
                    ("KROUND25 requires a round in 0..23"))

    localparam integer RHO [25] = '{
        0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
        25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14
    };
    localparam logic [63:0] RC [32] = '{
        64'h0000000000000001,
        64'h0000000000008082,
        64'h800000000000808a,
        64'h8000000080008000,
        64'h000000000000808b,
        64'h0000000080000001,
        64'h8000000080008081,
        64'h8000000000008009,
        64'h000000000000008a,
        64'h0000000000000088,
        64'h0000000080008009,
        64'h000000008000000a,
        64'h000000008000808b,
        64'h800000000000008b,
        64'h8000000000008089,
        64'h8000000000008003,
        64'h8000000000008002,
        64'h8000000000000080,
        64'h000000000000800a,
        64'h800000008000000a,
        64'h8000000080008081,
        64'h8000000000008080,
        64'h0000000080000001,
        64'h8000000080008008,
        64'b0, 64'b0, 64'b0, 64'b0, 64'b0, 64'b0, 64'b0, 64'b0
    };

    typedef struct packed {
        alu_header_t       header;
        logic              high_half;
        logic [4:0]        round;
        logic [24:0][63:0] state;
        logic [4:0][63:0]  parity;
    } parity_stage_t;

    typedef struct packed {
        alu_header_t       header;
        logic              high_half;
        logic [4:0]        round;
        logic [24:0][63:0] state;
    } state_stage_t;

    parity_stage_t parity_in, parity_out;
    state_stage_t theta_in, theta_out;
    state_stage_t rhopi_in, rhopi_out;
    alu_result_t result;
    wire parity_valid, parity_ready;
    wire theta_valid, theta_ready;
    wire rhopi_valid, rhopi_ready;

    assign parity_in.header = execute_if.data.header;
    assign parity_in.high_half = execute_if.data.op_args.alu.imm20[5];
    assign parity_in.round = execute_if.data.op_args.alu.imm20[4:0];
    for (genvar t = 0; t < 25; ++t) begin : g_input_state
        assign parity_in.state[t] = {execute_if.data.rs2_data[t], execute_if.data.rs1_data[t]};
    end
    for (genvar x = 0; x < 5; ++x) begin : g_parity
        assign parity_in.parity[x] = parity_in.state[x]
                                   ^ parity_in.state[x+5]
                                   ^ parity_in.state[x+10]
                                   ^ parity_in.state[x+15]
                                   ^ parity_in.state[x+20];
    end

    VX_pipe_buffer #(
        .DATAW ($bits(parity_stage_t))
    ) parity_buffer (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (execute_if.valid),
        .ready_in  (execute_if.ready),
        .data_in   (parity_in),
        .data_out  (parity_out),
        .ready_out (parity_ready),
        .valid_out (parity_valid)
    );

    assign theta_in.header = parity_out.header;
    assign theta_in.high_half = parity_out.high_half;
    assign theta_in.round = parity_out.round;
    for (genvar t = 0; t < 25; ++t) begin : g_theta
        localparam X = t % 5;
        wire [63:0] next_column = parity_out.parity[(X + 1) % 5];
        assign theta_in.state[t] = parity_out.state[t]
                                 ^ parity_out.parity[(X + 4) % 5]
                                 ^ {next_column[62:0], next_column[63]};
    end

    VX_pipe_buffer #(
        .DATAW ($bits(state_stage_t))
    ) theta_buffer (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (parity_valid),
        .ready_in  (parity_ready),
        .data_in   (theta_in),
        .data_out  (theta_out),
        .ready_out (theta_ready),
        .valid_out (theta_valid)
    );

    assign rhopi_in.header = theta_out.header;
    assign rhopi_in.high_half = theta_out.high_half;
    assign rhopi_in.round = theta_out.round;
    for (genvar t = 0; t < 25; ++t) begin : g_rhopi
        localparam X = t % 5;
        localparam Y = t / 5;
        localparam SRC = ((X + 3 * Y) % 5) + 5 * X;
        wire [63:0] word_in = theta_out.state[SRC];
        assign rhopi_in.state[t] = (word_in << RHO[SRC])
                                 | (word_in >> ((64 - RHO[SRC]) % 64));
    end

    VX_pipe_buffer #(
        .DATAW ($bits(state_stage_t))
    ) rhopi_buffer (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (theta_valid),
        .ready_in  (theta_ready),
        .data_in   (rhopi_in),
        .data_out  (rhopi_out),
        .ready_out (rhopi_ready),
        .valid_out (rhopi_valid)
    );

    assign result.header = rhopi_out.header;
    for (genvar t = 0; t < `VX_CFG_NUM_ALU_LANES; ++t) begin : g_result
        if (t < 25) begin : g_state
            localparam X = t % 5;
            localparam ROW = (t / 5) * 5;
            wire [63:0] chi = rhopi_out.state[t]
                            ^ (~rhopi_out.state[ROW + (X+1)%5]
                             & rhopi_out.state[ROW + (X+2)%5])
                            ^ ((t == 0) ? RC[rhopi_out.round] : 64'b0);
            assign result.data[t] = rhopi_out.high_half ? chi[63:32] : chi[31:0];
        end else begin : g_padding
            assign result.data[t] = '0;
        end
    end

    VX_pipe_buffer #(
        .DATAW ($bits(alu_result_t))
    ) result_buffer (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (rhopi_valid),
        .ready_in  (rhopi_ready),
        .data_in   (result),
        .data_out  (result_if.data),
        .ready_out (result_if.ready),
        .valid_out (result_if.valid)
    );

endmodule
