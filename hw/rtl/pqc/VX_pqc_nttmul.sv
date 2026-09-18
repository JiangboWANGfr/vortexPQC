// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

`include "VX_define.vh"

module VX_pqc_nttmul import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter NUM_LANES = 1
) (
    input wire clk,
    input wire reset,
    VX_execute_if.slave execute_if,
    VX_result_if.master result_if
);
    `UNUSED_SPARAM (INSTANCE_ID)
    `UNUSED_VAR (execute_if.data.rs3_data)

    localparam TAG_WIDTH = $bits(alu_header_t);
    localparam LANE_BITS = `UP(`CLOG2(NUM_LANES));
    localparam NUM_PAIRS = (NUM_LANES + 1) / 2;
    localparam NUM_MULTIPLIERS = `MIN(NUM_LANES, `VX_CFG_NTT_MUL_LANES);
    localparam NUM_BEATS = NUM_LANES / NUM_MULTIPLIERS;
    localparam PAIR_BEATS = NUM_PAIRS / NUM_MULTIPLIERS;
    localparam BEAT_BITS = `UP(`CLOG2(NUM_BEATS));
    localparam SAVED_LANES = `UP(NUM_LANES - NUM_MULTIPLIERS);
    localparam META_WIDTH = TAG_WIDTH + 1 + 1 + 1 + 3;
    localparam BEAT_WIDTH = META_WIDTH + BEAT_BITS + 1 + 32 * NUM_MULTIPLIERS;

    `STATIC_ASSERT (`VX_CFG_NTT_MUL_LANES > 0, ("NTT multiplier count must be positive"))
    `STATIC_ASSERT (NUM_MULTIPLIERS <= NUM_PAIRS && NUM_LANES % NUM_MULTIPLIERS == 0,
                    ("NTT multiplier count must divide lanes and not exceed pair count"))

    function automatic logic [31:0] sign16(input logic [15:0] value);
        return {{16{value[15]}}, value};
    endfunction

    function automatic logic [15:0] k_mont_factor(input logic [15:0] value);
        return value - (value << 8) - (value << 10) - (value << 11);
    endfunction

    function automatic logic [15:0] k_mont_finish(
        input logic [31:0] product, input logic [15:0] factor
    );
        logic signed [31:0] f;
        logic signed [31:0] fq;
        begin
            f = $signed(sign16(factor));
            fq = f + (f <<< 8) + (f <<< 10) + (f <<< 11);
            return 16'($signed(product - fq) >>> 16);
        end
    endfunction

    function automatic logic [15:0] k_barrett_quotient(input logic [31:0] product);
        return 16'(($signed(product) + 32'sd33554432) >>> 26);
    endfunction

    function automatic logic [15:0] k_barrett_finish(
        input logic [15:0] value, input logic [15:0] quotient
    );
        logic signed [31:0] q;
        logic signed [31:0] qq;
        begin
            q = $signed(sign16(quotient));
            qq = q + (q <<< 8) + (q <<< 10) + (q <<< 11);
            return 16'($signed(sign16(value)) - qq);
        end
    endfunction

    wire valid_out;
    alu_header_t header_out;
    wire [NUM_LANES-1:0][`VX_CFG_XLEN-1:0] data_out;
    wire advance = result_if.ready || ~valid_out;
    wire is_butterfly_in = execute_if.data.op_type == INST_ALU_NTTBF_K;
    wire is_d_in = execute_if.data.op_args.alu.imm20[4];
    wire is_gs_in = execute_if.data.op_args.alu.imm20[3];
    wire [2:0] stage_in = execute_if.data.op_args.alu.imm20[2:0];
    wire [BEAT_BITS-1:0] last_beat_in = BEAT_BITS'(
        (!is_butterfly_in || (!is_d_in && is_gs_in)) ? NUM_BEATS - 1 : PAIR_BEATS - 1);

    wire [NUM_LANES-1:0][31:0] request_a, request_b, request_coefficients;
    for (genvar i = 0; i < NUM_PAIRS; ++i) begin : g_operands
        wire [4:0][31:0] pair_a, pair_b, pair_zeta;
        for (genvar s = 0; s < 5; ++s) begin : g_stage
            localparam LO = (i & ((1 << s) - 1)) | ((i >> s) << (s + 1));
            localparam HI = LO | (1 << s);
            if (HI < NUM_LANES) begin : g_pair
                assign pair_a[s] = execute_if.data.rs1_data[LO][31:0];
                assign pair_b[s] = execute_if.data.rs1_data[HI][31:0];
                assign pair_zeta[s] = execute_if.data.rs2_data[LO][31:0];
            end else begin : g_unused
                assign pair_a[s] = '0;
                assign pair_b[s] = '0;
                assign pair_zeta[s] = '0;
            end
        end
        wire [31:0] a = pair_a[stage_in];
        wire [31:0] b = pair_b[stage_in];
        assign request_a[i] = is_d_in
            ? (is_butterfly_in ? (is_gs_in ? a - b : b)
                               : execute_if.data.rs1_data[i][31:0])
            : (is_butterfly_in ? sign16(is_gs_in ? b[15:0] - a[15:0]
                                                : b[15:0])
                               : sign16(execute_if.data.rs1_data[i][15:0]));
        assign request_b[i] = is_d_in
            ? (is_butterfly_in ? pair_zeta[stage_in]
                               : execute_if.data.rs2_data[i][31:0])
            : (is_butterfly_in ? sign16(pair_zeta[stage_in][15:0])
                               : sign16(execute_if.data.rs2_data[i][15:0]));
        assign request_coefficients[i] = is_d_in
            ? (is_gs_in ? a + b : a) : sign16(a[15:0]);
        if (i + NUM_PAIRS < NUM_LANES) begin : g_second
            assign request_coefficients[i + NUM_PAIRS] = sign16(a[15:0] + b[15:0]);
            assign request_a[i + NUM_PAIRS] = is_butterfly_in
                ? request_coefficients[i + NUM_PAIRS]
                : (is_d_in ? execute_if.data.rs1_data[i + NUM_PAIRS][31:0]
                           : sign16(execute_if.data.rs1_data[i + NUM_PAIRS][15:0]));
            assign request_b[i + NUM_PAIRS] = is_butterfly_in ? 32'd20159
                : (is_d_in ? execute_if.data.rs2_data[i + NUM_PAIRS][31:0]
                           : sign16(execute_if.data.rs2_data[i + NUM_PAIRS][15:0]));
        end
    end

    wire [SAVED_LANES-1:0][31:0] remaining_a, remaining_b, remaining_coefficients;
    if (NUM_BEATS > 1) begin : g_remaining
        assign remaining_a = request_a[NUM_LANES-1:NUM_MULTIPLIERS];
        assign remaining_b = request_b[NUM_LANES-1:NUM_MULTIPLIERS];
        assign remaining_coefficients = request_coefficients[NUM_LANES-1:NUM_MULTIPLIERS];
    end else begin : g_single
        assign remaining_a = '0;
        assign remaining_b = '0;
        assign remaining_coefficients = '0;
    end
    reg [BEAT_BITS-1:0] pending_beat, saved_last;
    reg [SAVED_LANES-1:0][31:0] saved_a, saved_b, saved_coefficients;
    reg [META_WIDTH-1:0] saved_meta;
    wire pending = pending_beat != 0;
    always @(posedge clk) begin
        if (reset) begin
            pending_beat <= 0;
        end else if (advance) begin
            if (pending) begin
                pending_beat <= (pending_beat == saved_last) ? 0 : pending_beat + 1'b1;
            end else if (execute_if.valid && last_beat_in != 0) begin
                pending_beat <= 1;
            end
        end
        if (advance) begin
            if (pending) begin
                saved_a <= saved_a >> (32 * NUM_MULTIPLIERS);
                saved_b <= saved_b >> (32 * NUM_MULTIPLIERS);
                saved_coefficients <= saved_coefficients >> (32 * NUM_MULTIPLIERS);
            end else if (execute_if.valid && last_beat_in != 0) begin
                saved_a <= remaining_a;
                saved_b <= remaining_b;
                saved_coefficients <= remaining_coefficients;
                saved_meta <= {execute_if.data.header, is_d_in, is_butterfly_in,
                               is_gs_in, stage_in};
                saved_last <= last_beat_in;
            end
        end
    end

    wire [NUM_MULTIPLIERS-1:0][31:0] factor_a = pending
        ? (32 * NUM_MULTIPLIERS)'(saved_a) : request_a[NUM_MULTIPLIERS-1:0];
    wire [NUM_MULTIPLIERS-1:0][31:0] factor_b = pending
        ? (32 * NUM_MULTIPLIERS)'(saved_b) : request_b[NUM_MULTIPLIERS-1:0];
    wire [BEAT_WIDTH-1:0] tag_in = pending
        ? {saved_meta, pending_beat, pending_beat == saved_last,
           (32 * NUM_MULTIPLIERS)'(saved_coefficients)}
        : {execute_if.data.header, is_d_in, is_butterfly_in, is_gs_in,
           stage_in, BEAT_BITS'(0), last_beat_in == 0,
           request_coefficients[NUM_MULTIPLIERS-1:0]};

    wire valid_operands;
    wire [BEAT_WIDTH-1:0] tag_operands;
    wire [NUM_MULTIPLIERS-1:0][31:0] operand_a, operand_b;
    VX_shift_register #(
        .DATAW (1 + BEAT_WIDTH + 64 * NUM_MULTIPLIERS),
        .DEPTH (1), .RESETW (1)
    ) operand_pipe (
        .clk (clk), .reset (reset), .enable (advance),
        .data_in ({pending || execute_if.valid, tag_in, factor_a, factor_b}),
        .data_out ({valid_operands, tag_operands, operand_a, operand_b})
    );

    wire valid_mul;
    alu_header_t header_mul;
    wire is_d_mul, is_butterfly_mul, is_gs_mul, last_mul;
    wire [2:0] stage_mul;
    wire [BEAT_BITS-1:0] beat_mul;
    wire [NUM_MULTIPLIERS-1:0][31:0] coefficients_mul;
    VX_shift_register #(
        .DATAW (1 + BEAT_WIDTH),
        .DEPTH (`LATENCY_IMUL - 1),
        .RESETW (1)
    ) tag_pipe (
        .clk (clk), .reset (reset), .enable (advance),
        .data_in ({valid_operands, tag_operands}),
        .data_out ({valid_mul, header_mul, is_d_mul, is_butterfly_mul, is_gs_mul,
                    stage_mul, beat_mul, last_mul, coefficients_mul})
    );

    wire probe_request_valid = execute_if.valid;
    wire probe_request_ready = execute_if.ready;
    wire [NUM_LANES-1:0] probe_request_mask = execute_if.data.header.tmask;
`ifdef SIMULATION
    wire [LANE_BITS-1:0] distance_in = LANE_BITS'(1) << stage_in;
    reg pair_mask_valid;
    always @(*) begin
        pair_mask_valid = 1'b1;
        for (integer i = 0; i < NUM_LANES; ++i) begin
            if (execute_if.data.header.tmask[i]
             != execute_if.data.header.tmask[LANE_BITS'(i) ^ distance_in]) begin
                pair_mask_valid = 1'b0;
            end
        end
    end
    `RUNTIME_ASSERT(
        ~(execute_if.valid && execute_if.ready && is_butterfly_in)
        || ((NUM_LANES == 32) && (stage_in <= 3'd4) && pair_mask_valid),
        ("invalid NTTBF configuration: lanes=%0d stage=%0d tmask=%b",
         NUM_LANES, stage_in, execute_if.data.header.tmask))
`endif

    wire [NUM_MULTIPLIERS-1:0][63:0] product_out;
    for (genvar i = 0; i < NUM_MULTIPLIERS; ++i) begin : g_multiplier
        VX_multiplier #(
            .A_WIDTH (32), .B_WIDTH (32), .R_WIDTH (64),
            .SIGNED (1), .LATENCY (`LATENCY_IMUL - 1)
        ) multiplier (
            .clk (clk), .enable (advance),
            .dataa (operand_a[i]), .datab (operand_b[i]),
            .result (product_out[i])
        );
    end

    wire valid_factor;
    alu_header_t header_factor;
    wire is_d_factor, is_butterfly_factor, is_gs_factor, last_factor;
    wire [2:0] stage_factor;
    wire [BEAT_BITS-1:0] beat_factor;
    wire [NUM_MULTIPLIERS-1:0][31:0] coefficients_factor, mont_factor;
    wire [NUM_MULTIPLIERS-1:0][31:0] k_factor;
    wire [NUM_MULTIPLIERS-1:0][63:0] product_factor;
    wire [NUM_MULTIPLIERS-1:0][31:0] mont_factor_in;
    wire [NUM_MULTIPLIERS-1:0][31:0] k_factor_in;
    for (genvar i = 0; i < NUM_MULTIPLIERS; ++i) begin : g_factor
        assign mont_factor_in[i] = product_out[i][31:0] * 32'd58728449;
        assign k_factor_in[i] = {k_barrett_quotient(product_out[i][31:0]),
                                 k_mont_factor(product_out[i][15:0])};
    end
    VX_shift_register #(
        .DATAW (1 + BEAT_WIDTH + 128 * NUM_MULTIPLIERS),
        .DEPTH (1), .RESETW (1)
    ) factor_pipe (
        .clk (clk), .reset (reset), .enable (advance),
        .data_in ({valid_mul, header_mul, is_d_mul, is_butterfly_mul, is_gs_mul,
                   stage_mul, beat_mul, last_mul, coefficients_mul, product_out,
                   mont_factor_in, k_factor_in}),
        .data_out ({valid_factor, header_factor, is_d_factor,
                    is_butterfly_factor, is_gs_factor, stage_factor, beat_factor, last_factor,
                    coefficients_factor, product_factor, mont_factor, k_factor})
    );

    wire valid_reduce;
    alu_header_t header_reduce;
    wire is_d_reduce, is_butterfly_reduce, is_gs_reduce, last_reduce;
    wire [2:0] stage_reduce;
    wire [BEAT_BITS-1:0] beat_reduce;
    wire [NUM_MULTIPLIERS-1:0][31:0] coefficients_reduce, montgomery;
    wire [NUM_MULTIPLIERS-1:0][31:0] montgomery_in, barrett_in, barrett_reduce;
    for (genvar i = 0; i < NUM_MULTIPLIERS; ++i) begin : g_reduce
        wire signed [63:0] tq = $signed(mont_factor[i]) * 64'sd8380417;
        wire signed [63:0] difference = $signed(product_factor[i]) - tq;
        assign montgomery_in[i] = is_d_factor ? difference[63:32]
            : sign16(k_mont_finish(product_factor[i][31:0], k_factor[i][15:0]));
        assign barrett_in[i] = sign16(k_barrett_finish(
            coefficients_factor[i][15:0], k_factor[i][31:16]));
    end
    VX_shift_register #(
        .DATAW (1 + BEAT_WIDTH + 64 * NUM_MULTIPLIERS),
        .DEPTH (1), .RESETW (1)
    ) reduction_pipe (
        .clk (clk), .reset (reset), .enable (advance),
        .data_in ({valid_factor, header_factor, is_d_factor, is_butterfly_factor,
                   is_gs_factor, stage_factor, beat_factor, last_factor,
                   coefficients_factor, montgomery_in, barrett_in}),
        .data_out ({valid_reduce, header_reduce, is_d_reduce, is_butterfly_reduce,
                    is_gs_reduce, stage_reduce, beat_reduce, last_reduce,
                    coefficients_reduce, montgomery, barrett_reduce})
    );

    reg [NUM_LANES-1:0][31:0] partial_results;
    wire [NUM_LANES-1:0][31:0] assembled_results;
    always @(posedge clk) begin
        if (advance && valid_reduce && !last_reduce) begin
            partial_results <= assembled_results;
        end
    end
    wire [NUM_LANES-1:0][`VX_CFG_XLEN-1:0] data_reduce;
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_result
        wire [4:0][31:0] butterfly;
        wire [4:0] butterfly_valid;
        for (genvar s = 0; s < 5; ++s) begin : g_stage
            localparam PAIR = (i & ((1 << s) - 1)) | ((i >> (s + 1)) << s);
            localparam BANK = PAIR % NUM_MULTIPLIERS;
            localparam HIGH = (i & (1 << s)) != 0;
            if ((i ^ (1 << s)) < NUM_LANES) begin : g_pair
                wire [31:0] low_d = is_gs_reduce ? coefficients_reduce[BANK]
                    : coefficients_reduce[BANK] + montgomery[BANK];
                wire [31:0] high_d = is_gs_reduce ? montgomery[BANK]
                    : coefficients_reduce[BANK] - montgomery[BANK];
                wire [31:0] low_k = is_gs_reduce ? barrett_reduce[BANK]
                    : sign16(coefficients_reduce[BANK][15:0] + montgomery[BANK][15:0]);
                wire [31:0] high_k = is_gs_reduce ? montgomery[BANK]
                    : sign16(coefficients_reduce[BANK][15:0] - montgomery[BANK][15:0]);
                assign butterfly[s] = HIGH ? (is_d_reduce ? high_d : high_k)
                                          : (is_d_reduce ? low_d : low_k);
                assign butterfly_valid[s] = beat_reduce == BEAT_BITS'(
                    PAIR / NUM_MULTIPLIERS + ((!HIGH && !is_d_reduce && is_gs_reduce) ? PAIR_BEATS : 0));
            end else begin : g_unused
                assign butterfly[s] = '0;
                assign butterfly_valid[s] = 0;
            end
        end
        wire update_lane = is_butterfly_reduce ? butterfly_valid[stage_reduce]
            : beat_reduce == BEAT_BITS'(i / NUM_MULTIPLIERS);
        wire [31:0] lane_result = is_butterfly_reduce ? butterfly[stage_reduce]
            : montgomery[i % NUM_MULTIPLIERS];
        assign assembled_results[i] = update_lane ? lane_result : partial_results[i];
        assign data_reduce[i] = `VX_CFG_XLEN'($signed(assembled_results[i]));
    end

    VX_shift_register #(
        .DATAW (1 + TAG_WIDTH + `VX_CFG_XLEN * NUM_LANES),
        .DEPTH (1), .RESETW (1)
    ) result_pipe (
        .clk (clk), .reset (reset), .enable (advance),
        .data_in ({valid_reduce && last_reduce, header_reduce, data_reduce}),
        .data_out ({valid_out, header_out, data_out})
    );

    assign execute_if.ready = advance && !pending;
    assign result_if.valid = valid_out;
    assign result_if.data.header = header_out;
    assign result_if.data.data = data_out;
endmodule
