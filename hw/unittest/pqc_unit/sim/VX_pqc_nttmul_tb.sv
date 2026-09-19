`include "VX_define.vh"

module VX_pqc_nttmul_tb;
    import VX_gpu_pkg::*;

    localparam L = `VX_CFG_NUM_ALU_LANES;
    localparam M = `VX_CFG_NTT_MUL_LANES;
    localparam BURST = 12;
    localparam UNSTALLED = 3 * BURST;
    localparam N = UNSTALLED + 64;
    localparam STALL_CYCLES = 30 + 4 * (L / M);
    localparam logic [31:0] NTTMUL_K_INSTR = 32'h0a73228b;
    localparam logic [31:0] NTTMUL_D_INSTR = 32'h0a73328b;

    logic clk = 0;
    logic reset = 1;
    always #5 clk = ~clk;

    VX_fetch_if fetch_if();
    VX_decode_if decode_if();
    VX_decode_sched_if decode_sched_if();

    VX_decode decode (
        .clk(clk), .reset(reset), .fetch_if(fetch_if),
        .decode_if(decode_if), .decode_sched_if(decode_sched_if)
    );

    assign decode_if.ibuf_pop = '0;

    VX_dispatch_if dispatch_if [`VX_CFG_ISSUE_WIDTH]();
    VX_commit_if commit_if [`VX_CFG_ISSUE_WIDTH]();
    VX_branch_ctl_if branch_ctl_if [`VX_CFG_NUM_ALU_BLOCKS]();

    VX_alu_unit dut (
        .clk(clk), .reset(reset), .dispatch_if(dispatch_if),
        .commit_if(commit_if), .branch_ctl_if(branch_ctl_if)
    );

    commit_t expected [N];
    int cycle = 0;
    int received = 0;
    int accepted = 0;
    int accepted_cycle [N];
    int previous_accept_cycle = 0;
    int ct_ii_checks = 0;
    int gs_ii_checks = 0;
    int nttmul_ii_checks = 0;
    int backpressure_start = -1;
    bit [4:0] ct_stages = '0;
    bit [4:0] gs_stages = '0;
    bit producer_done = 0;
    bit saw_input_stall = 0;
    bit saw_output_stall = 0;
    bit saw_pending_stall = 0;
    bit saw_partial_pair_mask = 0;
    bit saw_low_half_mask = 0;
    bit saw_high_half_mask = 0;
    bit holding_output = 0;
    commit_t held_output;

    function automatic logic [31:0] nttbf_instr(
        input bit is_gs,
        input logic [2:0] stage
    );
        nttbf_instr = {is_gs ? 7'h09 : 7'h08,
                       5'd7, 5'd6, stage, 5'd5, 7'h0b};
    endfunction

    function automatic logic [31:0] nttbf_d_instr(
        input bit is_gs,
        input logic [2:0] stage
    );
        nttbf_d_instr = {is_gs ? 7'h0b : 7'h0a,
                         5'd7, 5'd6, stage, 5'd5, 7'h0b};
    endfunction

    function automatic logic signed [15:0] operand_a(input int index);
        case (index % 12)
             0: operand_a = -32768;
             1: operand_a =  32767;
             2: operand_a =  -3329;
             3: operand_a =   3329;
             4: operand_a =     -1;
             5: operand_a =      0;
             6: operand_a =      1;
             7: operand_a = -16384;
             8: operand_a =  16384;
             9: operand_a = -26632;
            10: operand_a =  26631;
            default: operand_a = 16'(index * 3121 + 977);
        endcase
    endfunction

    function automatic logic signed [15:0] operand_b(input int index);
        case (index % 12)
             0: operand_b =  -1664;
             1: operand_b =   1664;
             2: operand_b =     -1;
             3: operand_b =      0;
             4: operand_b =      1;
             5: operand_b =   1441;
             6: operand_b =  -3329;
             7: operand_b =   3329;
             8: operand_b = -32768;
             9: operand_b =  32767;
            10: operand_b = -12345;
            default: operand_b = 16'(index * 1663 + 313);
        endcase
    endfunction

    function automatic logic signed [15:0] montgomery_ref(
        input logic signed [15:0] a,
        input logic signed [15:0] b
    );
        logic signed [31:0] product;
        logic [31:0] inverted_product;
        logic [15:0] inverted;
        logic signed [15:0] factor;
        logic signed [31:0] difference;
        begin
            product = a * b;
            inverted_product = {16'b0, product[15:0]} * 32'd62209;
            inverted = inverted_product[15:0];
            factor = $signed(inverted);
            difference = product - factor * 32'sd3329;
            montgomery_ref = 16'($signed(difference) >>> 16);
        end
    endfunction

    function automatic logic signed [15:0] barrett_ref(
        input logic signed [15:0] value
    );
        logic signed [31:0] product;
        logic signed [31:0] rounded;
        logic signed [31:0] quotient;
        begin
            product = value * 32'sd20159;
            rounded = product + 32'sd33554432;
            quotient = rounded >>> 26;
            barrett_ref = 16'({{16{value[15]}}, value}
                            - quotient * 32'sd3329);
        end
    endfunction

    function automatic logic signed [15:0] add_wrap16(
        input logic signed [15:0] a,
        input logic signed [15:0] b
    );
        begin
            add_wrap16 = a + b;
        end
    endfunction

    function automatic logic signed [15:0] sub_wrap16(
        input logic signed [15:0] a,
        input logic signed [15:0] b
    );
        begin
            sub_wrap16 = a - b;
        end
    endfunction

    function automatic bit is_butterfly_request(input int request);
        is_butterfly_request = (request < 2 * BURST)
            || ((request >= UNSTALLED) && (request % 3 != 2));
    endfunction

    function automatic logic [2:0] butterfly_stage(input int request);
        butterfly_stage = 3'(request % 5);
    endfunction

    function automatic bit butterfly_is_gs(input int request);
        butterfly_is_gs = (request < UNSTALLED)
            ? (request >= BURST && request < 2 * BURST)
            : (request % 3 == 1);
    endfunction

    function automatic bit request_is_d(input int request);
        return request >= UNSTALLED && (request / 3) % 2 != 0;
    endfunction

    function automatic int request_beats(input int request);
        return (is_butterfly_request(request)
            && (!butterfly_is_gs(request) || request_is_d(request))) ? L / (2 * M) : L / M;
    endfunction

    function automatic logic signed [31:0] operand_d(input int seed);
        case (seed % 8)
            0: return -32'sd67043336;
            1: return  32'sd67043336;
            2: return -32'sd8380417;
            3: return  32'sd8380417;
            4: return -1;
            5: return  0;
            6: return  1;
            default: return 32'(seed * 345679 - 1234567);
        endcase
    endfunction

    function automatic logic signed [31:0] mont_d_ref(
        input logic signed [31:0] a, input logic signed [31:0] b
    );
        logic signed [63:0] product, difference;
        logic signed [31:0] factor;
        begin
            product = a * b;
            factor = 32'(product * 64'd58728449);
            difference = product - 64'(factor) * 64'sd8380417;
            return 32'(difference >>> 32);
        end
    endfunction

    function automatic logic signed [15:0] pair_twiddle(
        input int request,
        input int pair_low
    );
        pair_twiddle = operand_b(request * 7 + pair_low);
    endfunction

    function automatic logic [`VX_CFG_XLEN-1:0] noisy_operand(
        input logic signed [15:0] value,
        input int seed
    );
        logic [`VX_CFG_XLEN-1:0] result;
        begin
            for (int bit_idx = 0; bit_idx < `VX_CFG_XLEN; ++bit_idx) begin
                if (bit_idx < 16)
                    result[bit_idx] = value[bit_idx];
                else
                    result[bit_idx] = ((seed + bit_idx) % 2) != 0;
            end
            noisy_operand = result;
        end
    endfunction

    function automatic logic [L-1:0] request_mask(
        input int request,
        input bit is_butterfly,
        input logic [2:0] stage
    );
        logic [L-1:0] mask;
        int distance;
        int pair_low;
        int selected_pair;
        int pair_index;
        begin
            for (int lane = 0; lane < L; ++lane) begin
                if (is_butterfly) begin
                    distance = 1 << stage;
                    pair_low = lane & ~distance;
                    selected_pair = (request % L) & ~distance;
                    pair_index = ((pair_low >> (stage + 1)) << stage)
                               | (pair_low & (distance - 1));
                    case ((request / 5) % 5)
                        0: mask[lane] = 1;
                        1: mask[lane] = (pair_low == selected_pair);
                        2: mask[lane] = (pair_low != selected_pair);
                        3: mask[lane] = ((pair_index & 1) == (request & 1));
                        default: mask[lane] = (pair_low == 0)
                            || (pair_low == ((L - 1) & ~distance));
                    endcase
                end else begin
                    case (request % 6)
                        0: mask[lane] = 1;
                        1: mask[lane] = (lane < L / 2);
                        2: mask[lane] = (lane >= L / 2);
                        3: mask[lane] = (lane == 0);
                        4: mask[lane] = (lane == L - 1);
                        default: mask[lane] = ((lane & 1) == 0);
                    endcase
                end
            end
            request_mask = mask;
        end
    endfunction

    task automatic check_valid_decode(
        input logic [31:0] instruction,
        input logic [INST_OP_BITS-1:0] expected_op,
        input logic [19:0] expected_imm
    );
        @(negedge clk);
        fetch_if.data = '0;
        fetch_if.data.uuid = UUID_WIDTH'(17);
        fetch_if.data.wid = NW_WIDTH'(2);
        fetch_if.data.cta_id = NCTA_WIDTH'(1);
        fetch_if.data.tmask = '1;
        fetch_if.data.PC = PC_BITS'(32'h100);
        fetch_if.data.instr = instruction;
        fetch_if.valid = 1;
        #1;
        if (!fetch_if.ready || !decode_if.valid
         || decode_if.data.ex_type != EX_ALU
         || decode_if.data.op_type != expected_op
         || decode_if.data.op_args.alu.xtype != ALU_TYPE_ARITH
         || decode_if.data.op_args.alu.is_w
         || decode_if.data.op_args.alu.use_PC
         || decode_if.data.op_args.alu.use_imm
         || decode_if.data.op_args.alu.imm20 != expected_imm
         || !decode_if.data.wb
         || decode_if.data.rd != NUM_REGS_BITS'(5)
         || decode_if.data.rs1 != NUM_REGS_BITS'(6)
         || decode_if.data.rs2 != NUM_REGS_BITS'(7)
         || decode_if.data.used_rs != 3'b011)
            $fatal(1, "PQC NTT ALU decode mismatch instr=%h", instruction);
        @(posedge clk);
        @(negedge clk);
        fetch_if.valid = 0;
        if (!decode_sched_if.valid || !decode_sched_if.unlock
         || decode_sched_if.wid != NW_WIDTH'(2))
            $fatal(1, "PQC NTT ALU instruction unexpectedly stalls its warp");
    endtask

    task automatic check_decode;
        check_valid_decode(NTTMUL_K_INSTR, INST_ALU_NTTMUL_K, '0);
        check_valid_decode(NTTMUL_D_INSTR, INST_ALU_NTTMUL_K, 20'h10);
        for (int mode = 0; mode < 2; ++mode) begin
            for (int stage = 0; stage < 5; ++stage) begin
                check_valid_decode(
                    nttbf_instr(mode != 0, 3'(stage)),
                    INST_ALU_NTTBF_K,
                    {16'b0, mode[0], 3'(stage)});
                check_valid_decode(
                    nttbf_d_instr(mode != 0, 3'(stage)),
                    INST_ALU_NTTBF_K,
                    {15'b0, 1'b1, mode[0], 3'(stage)});
            end
        end

        @(negedge clk);
        fetch_if.data.instr = nttbf_instr(1'b0, 3'd5);
        fetch_if.valid = 1;
        #1;
        if (decode_if.data.op_type === INST_ALU_NTTBF_K
         || decode_if.data.used_rs === 3'b011)
            $fatal(1, "NTTBF invalid stage decoded");
        @(posedge clk);
        @(negedge clk);
        fetch_if.valid = 0;
    endtask

    task automatic send_requests;
        for (int request = 0; request < N; ++request) begin
            logic [L-1:0] mask;
            bit is_butterfly;
            bit is_gs;
            bit is_d;
            logic [2:0] stage;
            int distance;
            int wid;
            is_butterfly = is_butterfly_request(request);
            is_gs = butterfly_is_gs(request);
            is_d = request_is_d(request);
            stage = butterfly_stage(request);
            distance = 1 << stage;
            mask = request_mask(request, is_butterfly, stage);
            wid = request % `VX_CFG_NUM_WARPS;

            if (is_butterfly) begin
                for (int lane = 0; lane < L; ++lane) begin
                    if (mask[lane] != mask[lane ^ distance])
                        $fatal(1, "test generated an unpaired NTTBF mask");
                end
                if (mask != '1)
                    saw_partial_pair_mask = 1;
            end

            @(negedge clk);
            dispatch_if[0].data = '0;
            dispatch_if[0].data.uuid = UUID_WIDTH'(request + 1);
            dispatch_if[0].data.wis = wid_to_wis(NW_WIDTH'(wid));
            dispatch_if[0].data.cta_id = NCTA_WIDTH'(request % `VX_CFG_NUM_WARPS);
            dispatch_if[0].data.sid = '0;
            dispatch_if[0].data.tmask = mask;
            dispatch_if[0].data.PC = PC_BITS'(32'h200 + 4 * request);
            dispatch_if[0].data.wb = 1;
            dispatch_if[0].data.wr_xregs = NUM_XREGS'(request);
            dispatch_if[0].data.rd = NUM_REGS_BITS'(1 + request % 31);
            dispatch_if[0].data.bytesel = BYTESEL_BITS'(request);
            dispatch_if[0].data.op_type = is_butterfly
                ? INST_ALU_NTTBF_K : INST_ALU_NTTMUL_K;
            dispatch_if[0].data.op_args = '0;
            dispatch_if[0].data.op_args.alu.xtype = ALU_TYPE_ARITH;
            dispatch_if[0].data.op_args.alu.imm20 = is_butterfly
                ? {15'b0, is_d, is_gs, stage} : {15'b0, is_d, 4'b0};
            dispatch_if[0].data.sop = (request & 1) == 0;
            dispatch_if[0].data.eop = (request % 3) != 0;

            expected[request] = '0;
            expected[request].uuid = dispatch_if[0].data.uuid;
            expected[request].wid = NW_WIDTH'(wid);
            expected[request].cta_id = dispatch_if[0].data.cta_id;
            expected[request].sid = dispatch_if[0].data.sid;
            expected[request].tmask = mask;
            expected[request].PC = dispatch_if[0].data.PC;
            expected[request].wb = dispatch_if[0].data.wb;
            expected[request].wr_xregs = dispatch_if[0].data.wr_xregs;
            expected[request].rd = dispatch_if[0].data.rd;
            expected[request].bytesel = dispatch_if[0].data.bytesel;
            expected[request].sop = dispatch_if[0].data.sop;
            expected[request].eop = dispatch_if[0].data.eop;

            for (int lane = 0; lane < L; ++lane) begin
                logic signed [15:0] coefficient;
                logic signed [15:0] second_operand;
                logic signed [15:0] result;
                coefficient = operand_a(request * L + lane);

                if (is_butterfly) begin
                    int pair_low;
                    int pair_high;
                    logic signed [15:0] a;
                    logic signed [15:0] b;
                    logic signed [15:0] zeta;
                    logic signed [15:0] sum16;
                    logic signed [15:0] difference16;
                    logic signed [15:0] montgomery;
                    pair_low = lane & ~distance;
                    pair_high = pair_low | distance;
                    a = operand_a(request * L + pair_low);
                    b = operand_a(request * L + pair_high);
                    zeta = pair_twiddle(request, pair_low);
                    second_operand = ((lane & distance) == 0)
                        ? zeta : $signed(zeta ^ 16'h5a5a);
                    if (is_gs) begin
                        sum16 = add_wrap16(a, b);
                        difference16 = sub_wrap16(b, a);
                        montgomery = montgomery_ref(difference16, zeta);
                        result = ((lane & distance) == 0)
                            ? barrett_ref(sum16) : montgomery;
                    end else begin
                        montgomery = montgomery_ref(b, zeta);
                        result = ((lane & distance) == 0)
                            ? add_wrap16(a, montgomery)
                            : sub_wrap16(a, montgomery);
                    end
                end else begin
                    second_operand = operand_b(request * L + lane);
                    result = montgomery_ref(coefficient, second_operand);
                end

                dispatch_if[0].data.rs1_data[lane] = noisy_operand(
                    coefficient, request + lane);
                dispatch_if[0].data.rs2_data[lane] = noisy_operand(
                    second_operand, request + lane + 1);
                dispatch_if[0].data.rs3_data[lane] = 'x;
                expected[request].data[lane] = `VX_CFG_XLEN'($signed(result));
                if (is_d) begin
                    logic signed [31:0] a, b, zeta, product, result_d;
                    int low_lane;
                    low_lane = is_butterfly ? lane & ~distance : lane;
                    a = operand_d(request * L + low_lane);
                    b = operand_d(request * L + (low_lane | distance));
                    zeta = 32'((request * 1777 + low_lane * 3121) % 8380417) - 4190208;
                    if (!is_butterfly) begin
                        result_d = mont_d_ref(a, zeta);
                    end else if (is_gs) begin
                        product = mont_d_ref(a - b, zeta);
                        result_d = (lane == low_lane) ? a + b : product;
                    end else begin
                        product = mont_d_ref(b, zeta);
                        result_d = (lane == low_lane) ? a + product : a - product;
                    end
                    dispatch_if[0].data.rs1_data[lane] = `VX_CFG_XLEN'(
                        operand_d(request * L + lane)) ^ (`VX_CFG_XLEN'(32'ha5a5) << 32);
                    dispatch_if[0].data.rs2_data[lane] = `VX_CFG_XLEN'(
                        (lane == low_lane) ? zeta : zeta ^ 32'h5a5a);
                    expected[request].data[lane] = `VX_CFG_XLEN'(result_d);
                end
            end

            dispatch_if[0].valid = 1;
            do @(posedge clk); while (!dispatch_if[0].ready);
        end
        @(negedge clk);
        dispatch_if[0].valid = 0;
        producer_done = 1;
    endtask

    always @(negedge clk) begin
        if (!reset) begin
            if (accepted >= UNSTALLED && backpressure_start < 0)
                backpressure_start = cycle;
            commit_if[0].ready = (backpressure_start < 0)
                || !((cycle < backpressure_start + STALL_CYCLES)
                  || (cycle > backpressure_start + STALL_CYCLES + 6 && cycle % 11 >= 3));
        end
    end

    always @(posedge clk) begin
        if (reset) begin
            cycle = 0;
            received = 0;
            accepted = 0;
            ct_ii_checks = 0;
            gs_ii_checks = 0;
            nttmul_ii_checks = 0;
            ct_stages = '0;
            gs_stages = '0;
            saw_low_half_mask = 0;
            saw_high_half_mask = 0;
            saw_input_stall = 0;
            saw_output_stall = 0;
            saw_pending_stall = 0;
            holding_output = 0;
        end else begin
            ++cycle;

            if (dut.g_blocks[0].nttmul_unit.probe_request_valid
             && dut.g_blocks[0].nttmul_unit.probe_request_ready) begin
                if (accepted >= N)
                    $fatal(1, "duplicate PQC NTT unit request");
                if (accepted > 0 && accepted < UNSTALLED) begin
                    int expected_ii;
                    expected_ii = request_beats(accepted - 1);
                    if (cycle - previous_accept_cycle != expected_ii)
                        $fatal(1, "request %0d unit II=%0d expected %0d",
                            accepted, cycle - previous_accept_cycle, expected_ii);
                    if (accepted < BURST)
                        ++ct_ii_checks;
                    else if (accepted > BURST && accepted < 2 * BURST)
                        ++gs_ii_checks;
                    else if (accepted > 2 * BURST)
                        ++nttmul_ii_checks;
                end
                accepted_cycle[accepted] = cycle;
                previous_accept_cycle = cycle;
                if (is_butterfly_request(accepted)) begin
                    if (butterfly_is_gs(accepted))
                        gs_stages[butterfly_stage(accepted)] = 1;
                    else
                        ct_stages[butterfly_stage(accepted)] = 1;
                end else begin
                    if (dut.g_blocks[0].nttmul_unit.probe_request_mask == 32'h0000ffff)
                        saw_low_half_mask = 1;
                    if (dut.g_blocks[0].nttmul_unit.probe_request_mask == 32'hffff0000)
                        saw_high_half_mask = 1;
                end
                ++accepted;
            end
            if (dispatch_if[0].valid && !dispatch_if[0].ready)
                saw_input_stall = 1;
            if (!dut.g_blocks[0].nttmul_unit.advance
             && dut.g_blocks[0].nttmul_unit.pending)
                saw_pending_stall = 1;

            if (holding_output) begin
                if (!commit_if[0].valid || commit_if[0].data !== held_output)
                    $fatal(1, "result changed under backpressure");
            end
            if (commit_if[0].valid && !commit_if[0].ready) begin
                saw_output_stall = 1;
                held_output = commit_if[0].data;
                holding_output = 1;
            end else begin
                holding_output = 0;
            end

            if (commit_if[0].valid && commit_if[0].ready) begin
                if (received >= N)
                    $fatal(1, "duplicate PQC NTT ALU result");
                if (backpressure_start < 0) begin
                    int expected_latency;
                    expected_latency = 6 + request_beats(received);
                    if (cycle - accepted_cycle[received] != expected_latency)
                        $fatal(1, "request %0d ALU latency=%0d expected %0d",
                            received, cycle - accepted_cycle[received], expected_latency);
                end
                if (commit_if[0].data.uuid !== expected[received].uuid
                 || commit_if[0].data.wid !== expected[received].wid
                 || commit_if[0].data.cta_id !== expected[received].cta_id
                 || commit_if[0].data.sid !== expected[received].sid
                 || commit_if[0].data.tmask !== expected[received].tmask
                 || commit_if[0].data.PC !== expected[received].PC
                 || commit_if[0].data.wb !== expected[received].wb
                 || commit_if[0].data.wr_xregs !== expected[received].wr_xregs
                 || commit_if[0].data.rd !== expected[received].rd
                 || commit_if[0].data.bytesel !== expected[received].bytesel
                 || commit_if[0].data.sop !== expected[received].sop
                 || commit_if[0].data.eop !== expected[received].eop)
                    $fatal(1, "request %0d header mismatch", received);
                for (int lane = 0; lane < L; ++lane) begin
                    if (expected[received].tmask[lane]
                     && commit_if[0].data.data[lane] !== expected[received].data[lane])
                        $fatal(1, "request %0d lane %0d got %h expected %h",
                            received, lane, commit_if[0].data.data[lane],
                            expected[received].data[lane]);
                end
                ++received;
            end

            if (cycle > 2000 + N * (L / M))
                $fatal(1, "PQC NTT ALU timeout received=%0d", received);
        end
    end

    initial begin
        if (`VX_CFG_ISSUE_WIDTH != 1 || `VX_CFG_NUM_ALU_BLOCKS != 1 || L != 32)
            $fatal(1, "test requires one issue/block and 32 ALU lanes");
        fetch_if.valid = 0;
        fetch_if.data = '0;
        decode_if.ready = 1;
        dispatch_if[0].valid = 0;
        dispatch_if[0].data = '0;
        commit_if[0].ready = 0;
        repeat (4) @(negedge clk);
        reset = 0;

        check_decode();
        send_requests();
        wait (received == N);
        repeat (4) @(negedge clk);
        if (!producer_done || accepted != N || ct_ii_checks != BURST - 1
         || gs_ii_checks != BURST - 1 || nttmul_ii_checks != BURST - 1
         || !saw_input_stall || !saw_output_stall || !saw_partial_pair_mask
         || ((L / M > 2) && !saw_pending_stall)
         || !saw_low_half_mask || !saw_high_half_mask
         || ct_stages != '1 || gs_stages != '1)
            $fatal(1, "missing II/backpressure/mask coverage accepted=%0d ct=%0d gs=%0d mul=%0d in_stall=%0d out_stall=%0d pending_stall=%0d partial=%0d low=%0d high=%0d ct_stages=%b gs_stages=%b",
                accepted, ct_ii_checks, gs_ii_checks, nttmul_ii_checks,
                saw_input_stall, saw_output_stall, saw_pending_stall,
                saw_partial_pair_mask,
                saw_low_half_mask, saw_high_half_mask, ct_stages, gs_stages);
        $display("NTTMUL.K/D/NTTBF.K/D PASSED XLEN=%0d LANES=%0d MULTIPLIERS=%0d requests=%0d CT_checks=%0d GS_checks=%0d NTTMUL_checks=%0d",
            `VX_CFG_XLEN, L, M, received, ct_ii_checks, gs_ii_checks, nttmul_ii_checks);
        $finish;
    end

endmodule
