`include "VX_define.vh"

module VX_alu_kround25_tb import VX_gpu_pkg::*; ();
    localparam COUNT = 192;
    localparam integer RHO [25] = '{
        0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3,
        10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14
    };

    logic clk;
    logic reset;
    always #5 clk = ~clk;

    VX_execute_if #(
        .data_t (alu_execute_t)
    ) execute_if ();

    VX_result_if #(
        .data_t (alu_result_t)
    ) result_if ();

    VX_alu_kround25 dut (
        .clk        (clk),
        .reset      (reset),
        .execute_if (execute_if),
        .result_if  (result_if)
    );

    alu_result_t expected [COUNT];
    int accepted_cycle [COUNT];
    logic [31:0] random_state;

    function automatic logic [31:0] random_word();
        random_state ^= random_state << 13;
        random_state ^= random_state >> 17;
        random_state ^= random_state << 5;
        return random_state;
    endfunction

    function automatic logic [63:0] round_constant(input int round_index);
        logic [7:0] lfsr;
        logic [63:0] value;
        lfsr = 8'h01;
        value = '0;
        for (int r = 0; r <= round_index; ++r) begin
            value = '0;
            for (int j = 0; j < 7; ++j) begin
                if (lfsr[0]) value[(1 << j) - 1] = 1;
                lfsr = (lfsr << 1) ^ (lfsr[7] ? 8'h71 : 8'h00);
            end
        end
        return value;
    endfunction

    task automatic prepare(input int index);
        logic [24:0][63:0] state_words;
        logic [24:0][63:0] rhopi_words;
        logic [4:0][63:0] columns;
        logic [63:0] next_column, value;
        int round_index;
        alu_execute_t request;
        request = '0;
        request.header.uuid = UUID_WIDTH'(index);
        request.header.wid = NW_WIDTH'(index % 8);
        request.header.cta_id = NCTA_WIDTH'(index);
        request.header.tmask = '1;
        request.header.sop = 1;
        request.header.eop = 1;
        request.header.PC = PC_BITS'(index * 4);
        request.header.wb = 1;
        request.header.rd = NUM_REGS_BITS'(index % 31 + 1);
        request.header.bytesel = BYTESEL_BITS'(index);
        request.op_type = INST_OP_BITS'(INST_KROUND);
        request.op_args.alu.xtype = ALU_TYPE_OTHER;
        round_index = index / 2 % 24;
        request.op_args.alu.imm20 = {14'b0, index[0], round_index[4:0]};
        if (index == 0 && $test$plusargs("bad_round")) begin
            request.op_args.alu.imm20[4:0] = 24;
        end
        if (index == 0 && $test$plusargs("bad_mask")) begin
            request.header.tmask[31] = 0;
        end
        state_words = '0;
        rhopi_words = '0;
        columns = '0;
        for (int t = 0; t < 32; ++t) begin
            request.rs1_data[t] = random_word();
            request.rs2_data[t] = random_word();
        end
        for (int t = 0; t < 25; ++t) begin
            state_words[t] = {request.rs2_data[t], request.rs1_data[t]};
            columns[t % 5] ^= state_words[t];
        end
        for (int t = 0; t < 25; ++t) begin
            next_column = columns[(t % 5 + 1) % 5];
            state_words[t] ^= columns[(t % 5 + 4) % 5]
                            ^ {next_column[62:0], next_column[63]};
        end
        for (int t = 0; t < 25; ++t) begin
            value = (state_words[t] << RHO[t])
                  | (state_words[t] >> ((64 - RHO[t]) % 64));
            rhopi_words[t / 5 + 5 * ((2 * (t % 5) + 3 * (t / 5)) % 5)] = value;
        end
        expected[index] = '0;
        expected[index].header = request.header;
        for (int t = 0; t < 25; ++t) begin
            value = rhopi_words[t]
                  ^ (~rhopi_words[(t % 5 + 1) % 5 + 5 * (t / 5)]
                   & rhopi_words[(t % 5 + 2) % 5 + 5 * (t / 5)]);
            if (t == 0) value ^= round_constant(round_index);
            expected[index].data[t] = index[0] ? value[63:32] : value[31:0];
        end
        execute_if.data = request;
    endtask

    initial begin
        int sent, received, cycle;
        bit pending, stalled;
        alu_result_t stalled_result;
        clk = 0;
        reset = 1;
        random_state = 32'h510e527f;
        sent = 0;
        received = 0;
        cycle = 0;
        pending = 0;
        stalled = 0;
        stalled_result = '0;
        execute_if.valid = 0;
        execute_if.data = '0;
        result_if.ready = 0;
        repeat (4) @(negedge clk);
        reset = 0;
        while (received < COUNT) begin
            @(negedge clk);
            if (!pending && sent < COUNT && (cycle < 32 || (random_word() % 4 != 0))) begin
                prepare(sent);
                pending = 1;
            end
            execute_if.valid = pending;
            result_if.ready = cycle < 32 || (random_word() % 3 != 0);
            @(posedge clk);
            if (stalled && (!result_if.valid || result_if.data !== stalled_result)) begin
                $fatal(1, "result changed under backpressure");
            end
            stalled = result_if.valid && !result_if.ready;
            stalled_result = result_if.data;
            if (result_if.valid && result_if.ready) begin
                if (received >= sent || result_if.data !== expected[received]) begin
                    $fatal(1, "result mismatch at transaction %0d", received);
                end
                if (received < 28 && cycle - accepted_cycle[received] != 4) begin
                    $fatal(1, "latency mismatch at transaction %0d: %0d", received,
                           cycle - accepted_cycle[received]);
                end
                received++;
            end
            if (execute_if.valid && execute_if.ready) begin
                accepted_cycle[sent] = cycle;
                sent++;
                pending = 0;
            end
            cycle++;
            if (cycle > 2000) begin
                $fatal(1, "timeout: sent=%0d received=%0d", sent, received);
            end
        end
        if (accepted_cycle[31] - accepted_cycle[0] != 31) begin
            $fatal(1, "expected one request per cycle");
        end
        $display("PASSED! KROUND25: %0d transactions, metadata, padding, stalls, latency=4, II=1", COUNT);
        $finish;
    end
endmodule
