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

module VX_alu_unit import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = ""
) (
    input wire              clk,
    input wire              reset,

    // Inputs
    VX_dispatch_if.slave    dispatch_if [`VX_CFG_ISSUE_WIDTH],

    // Outputs
    VX_commit_if.master     commit_if [`VX_CFG_ISSUE_WIDTH],
    VX_branch_ctl_if.master branch_ctl_if [`VX_CFG_NUM_ALU_BLOCKS]
);

    `UNUSED_SPARAM (INSTANCE_ID)
    localparam BLOCK_SIZE   = `VX_CFG_NUM_ALU_BLOCKS;
    localparam NUM_LANES    = `VX_CFG_NUM_ALU_LANES;
    localparam PARTIAL_BW   = (BLOCK_SIZE != `VX_CFG_ISSUE_WIDTH) || (NUM_LANES != `VX_CFG_SIMD_WIDTH);
    localparam PE_COUNT     = 1 + `VX_CFG_EXT_M_ENABLED + `VX_CFG_EXT_KSG25_ENABLED
                                + `VX_CFG_EXT_KROUND25_ENABLED + `VX_CFG_EXT_NTT_ENABLED;
    localparam PE_SEL_BITS  = `CLOG2(PE_COUNT);
    localparam PE_IDX_INT   = 0;
    localparam PE_IDX_MDV   = PE_IDX_INT + `VX_CFG_EXT_M_ENABLED;
`ifdef VX_CFG_EXT_KSG25_ENABLE
    localparam PE_IDX_KSG25 = PE_IDX_MDV + `VX_CFG_EXT_KSG25_ENABLED;
`endif
`ifdef VX_CFG_EXT_KROUND25_ENABLE
    localparam PE_IDX_KROUND25 = PE_IDX_MDV + `VX_CFG_EXT_KSG25_ENABLED
                                           + `VX_CFG_EXT_KROUND25_ENABLED;
`endif
`ifdef VX_CFG_EXT_NTT_ENABLE
    localparam PE_IDX_NTT   = 1 + `VX_CFG_EXT_M_ENABLED + `VX_CFG_EXT_KSG25_ENABLED
                                + `VX_CFG_EXT_KROUND25_ENABLED;
`endif

    VX_execute_if #(
        .data_t (alu_execute_t)
    ) per_block_execute_if[BLOCK_SIZE]();

    VX_result_if #(
        .data_t (alu_result_t)
    ) per_block_result_if[BLOCK_SIZE]();

    VX_lane_dispatch #(
        .BLOCK_SIZE (BLOCK_SIZE),
        .NUM_LANES  (NUM_LANES),
        .OUT_BUF    (PARTIAL_BW ? 3 : 0)
    ) lane_dispatch (
        .clk        (clk),
        .reset      (reset),
        .dispatch_if(dispatch_if),
        .execute_if (per_block_execute_if)
    );

    for (genvar block_idx = 0; block_idx < BLOCK_SIZE; ++block_idx) begin : g_blocks

        VX_execute_if #(
            .data_t (alu_execute_t)
        ) pe_execute_if[PE_COUNT]();

        VX_result_if#(
            .data_t (alu_result_t)
        ) pe_result_if[PE_COUNT]();

        reg [`UP(PE_SEL_BITS)-1:0] pe_select;
        always @(*) begin
            pe_select = PE_IDX_INT;
            if (`VX_CFG_EXT_M_ENABLED && (per_block_execute_if[block_idx].data.op_args.alu.xtype == ALU_TYPE_MULDIV)) begin
                pe_select = PE_IDX_MDV;
            end
        `ifdef VX_CFG_EXT_KSG25_ENABLE
            if (per_block_execute_if[block_idx].data.op_args.alu.xtype == ALU_TYPE_OTHER
             && per_block_execute_if[block_idx].data.op_type >= INST_OP_BITS'(INST_KTHETA_L)
             && per_block_execute_if[block_idx].data.op_type <= INST_OP_BITS'(INST_KCHII_H)) begin
                pe_select = PE_IDX_KSG25;
            end
        `endif
        `ifdef VX_CFG_EXT_KROUND25_ENABLE
            if (per_block_execute_if[block_idx].data.op_args.alu.xtype == ALU_TYPE_OTHER
             && per_block_execute_if[block_idx].data.op_type == INST_OP_BITS'(INST_KROUND)) begin
                pe_select = PE_IDX_KROUND25;
            end
        `endif
        `ifdef VX_CFG_EXT_NTT_ENABLE
            if ((per_block_execute_if[block_idx].data.op_args.alu.xtype == ALU_TYPE_ARITH)
             && ((per_block_execute_if[block_idx].data.op_type == INST_ALU_NTTMUL_K)
              || (per_block_execute_if[block_idx].data.op_type == INST_ALU_NTTBF_K))) begin
                pe_select = PE_IDX_NTT;
            end
        `endif
        end

        VX_pe_switch #(
            .PE_COUNT    (PE_COUNT),
            .NUM_LANES   (NUM_LANES),
            .ARBITER     ("R"),
            .REQ_OUT_BUF (0),
            .RSP_OUT_BUF (PARTIAL_BW ? 1 : 3)
        ) pe_switch (
            .clk            (clk),
            .reset          (reset),
            .pe_sel         (pe_select),
            .execute_in_if  (per_block_execute_if[block_idx]),
            .result_out_if  (per_block_result_if[block_idx]),
            .execute_out_if (pe_execute_if),
            .result_in_if   (pe_result_if)
        );

        VX_alu_int #(
            .INSTANCE_ID (`SFORMATF(("%s-int%0d", INSTANCE_ID, block_idx))),
            .BLOCK_IDX (block_idx),
            .NUM_LANES (NUM_LANES)
        ) alu_int (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_INT]),
            .branch_ctl_if (branch_ctl_if[block_idx]),
            .result_if  (pe_result_if[PE_IDX_INT])
        );

    `ifdef VX_CFG_EXT_M_ENABLE
        VX_alu_muldiv #(
            .INSTANCE_ID (`SFORMATF(("%s-muldiv%0d", INSTANCE_ID, block_idx))),
            .NUM_LANES (NUM_LANES)
        ) muldiv_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_MDV]),
            .result_if  (pe_result_if[PE_IDX_MDV])
        );
    `endif
    `ifdef VX_CFG_EXT_KSG25_ENABLE
        VX_alu_ksg25 #(
            .INSTANCE_ID (`SFORMATF(("%s-ksg25%0d", INSTANCE_ID, block_idx)))
        ) ksg25_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_KSG25]),
            .result_if  (pe_result_if[PE_IDX_KSG25])
        );
    `endif
    `ifdef VX_CFG_EXT_KROUND25_ENABLE
        VX_alu_kround25 #(
            .INSTANCE_ID (`SFORMATF(("%s-kround25%0d", INSTANCE_ID, block_idx)))
        ) kround25_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_KROUND25]),
            .result_if  (pe_result_if[PE_IDX_KROUND25])
        );
    `endif

    `ifdef VX_CFG_EXT_NTT_ENABLE
        VX_pqc_nttmul #(
            .INSTANCE_ID (`SFORMATF(("%s-nttmul%0d", INSTANCE_ID, block_idx))),
            .NUM_LANES (NUM_LANES)
        ) nttmul_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_NTT]),
            .result_if  (pe_result_if[PE_IDX_NTT])
        );
    `endif
    end

    VX_lane_gather #(
        .BLOCK_SIZE (BLOCK_SIZE),
        .NUM_LANES  (NUM_LANES),
        .OUT_BUF    (PARTIAL_BW ? 3 : 0)
    ) lane_gather (
        .clk       (clk),
        .reset     (reset),
        .result_if (per_block_result_if),
        .commit_if (commit_if)
    );

endmodule
